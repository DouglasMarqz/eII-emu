#pragma once
// SSM2045 "model 2": derived independently from the SSMT 1983 datasheet block
// diagram (PDF1 Figure 1) and printed characteristics. See docs/SSM2045_MODEL2.md.
//
// Topology read from Figure 1 (rendered, zoomed):
//   * SIG IN (pin 2) is a summing node: 900R to GND, 8.1k from the 2-pole
//     buffer output (pin 1), the external source resistor, and the output of
//     the Q amplifier G_Q, whose (+) input is this same node and whose (-)
//     input is the 4-pole output (pin 8).
//   * G1: (+) node, (-) C1 (pin 3), output into C1      -> follower-integrator.
//   * G2: (-) C1, (+) GND, output into C2 (pin 4); B buffers C2 to pin 1.
//   * Section 2 repeats it: its node has 900R to GND, 8.1k from pin 1 and
//     8.1k from pin 8; G3/G4 with C3/C4; B buffers C4 to pin 8.
//   * One e^-x converter (pin 5) sets the four OTA tail currents.
//   * Q (pin 17) -> 1.8k -> grounded-base PNP; its collector is G_Q's tail.
//   * VCA2 linear control: pin 15 -> 450R -> PNP emitter, base = pin 14.
// Pin 1/8 are the buffered capacitor voltages (real pin levels); the
// "gain of 10 in the second filter stage" of PDF6 is the 8.1k:900R return.
// Bipolar OTAs: i = I*tanh(a*v/(2Vt)), gm = a*I/(2Vt); a = input attenuation.
// Numerics: trapezoidal (A-stable) implicit solve of 4 capacitor states plus
// the nonlinear node-1 voltage; Newton on (V3, V6, Vnode1), 3x3 Jacobian.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace eii::analog::ssm2 {
inline constexpr double kBoltzmannOverQ=8.617333262e-5; // V/K
inline constexpr double pi=3.14159265358979323846;

struct Params {
    // External (EII service sheet 7-45 defaults).
    double c1=20e-9,c2=3.9e-9,c3=20e-9,c4=1.8e-9;
    double rext=68000;        // R138: deglitch output -> SIG IN
    double rLoad8=4700;       // pin 8 external load to a virtual ground (R66 -> S3528 SIG IN)
    double rLoad1=1e12;       // pin 1 external load (not connected on 7-45)
    // Datasheet / Figure 1 internals.
    double rin=900,rfb=8100,rq=1800,rVca=450;
    double sinkCurrent=750e-6;    // PDF2: output sink current typ.
    double posSupply=15,negSupply=-5; // EII 7-45: pin 18 +15 V, pin 10 -5 V
    double railHeadroom=1.5;      // OTA output / follower compliance from each rail
    // Calibrated against datasheet anchors (docs/SSM2045_MODEL2.md, validation/).
    double expoMvPerOctave=19.3;  // PDF2 typ. at the datasheet conditions
    double dieTempC=25;           // datasheet conditions; PTAT scaling from here
    double tailAtZero=0;          // OTA tail current at V5=0 [A], set by calibrate()
    double a1=0.357,a2=0.357;     // G1/G3 and G2/G4 input attenuation
    double aQ=0.11;               // G_Q input attenuation
    double aVca=0.42;             // VCA cell input attenuation (8.17 S/A at 25 C)
    double pin5BiasCurrent=1e-6;  // PDF2 Fc input bias current typ., into pin 5
    double vebRefVolts=0.6,vebRefCurrent=10e-6; // PNP control inputs: "source above +600 mV"
    double inputNoise=23.7e-9;    // V/rtHz referred to G1 input (84 dB S/N re 150 mVpp, 20 kHz)
    bool noise=false;
    bool offsets=false;           // optional device offsets (datasheet limit-scaled typical)
    double vos[4]={2e-3,-2e-3,1.5e-3,-1.5e-3}; // OTA input offsets [V]
    double ios[4]={8e-9,-6e-9,8e-9,-6e-9};      // OTA output offset currents [A]
    double vcaFeedthrough=0.00275;              // VCA output offset / tail (41.5 dB control rejection)
};

inline double thermalVoltage(double tempC){return kBoltzmannOverQ*(tempC+273.15);}
// Rational tanh (Eigen generic_fast_tanh coefficients, evaluated in double):
// |error| < 3e-8 over the real line; odd, monotonic, saturates exactly at +/-1.
inline double fastTanh(double x){
    constexpr double c=7.90531110763549805;
    x=std::clamp(x,-c,c);const double x2=x*x;
    const double p=x*(4.89352455891786e-03+x2*(6.37261928875436e-04+x2*(1.48572235717979e-05+x2*(5.12229709037114e-08
        +x2*(-8.60467152213735e-11+x2*(2.00018790482477e-13+x2*(-2.76076847742355e-16)))))));
    const double q=4.89352518554385e-03+x2*(2.26843463243900e-03+x2*(1.18534705686654e-04+x2*1.19825839466702e-06));
    return p/q;
}

class Filter {
public:
    struct Stats{std::uint64_t samples=0,iterations=0,fallbacks=0,resets=0;};
    struct Pins{double pin1=0,pin8=0,node1=0;};
    explicit Filter(double sampleRate=96000,Params p={}):p_(p){setSampleRate(sampleRate);reset();}
    void configure(const Params& p){p_=p;update();lastV5_=1e300;}
    const Params& params()const noexcept{return p_;}
    void setSampleRate(double fs){if(!(fs>0))throw std::invalid_argument("ssm2 rate");fs_=fs;h_=1/fs;update();lastV5_=1e300;}
    void reset(){x_.fill(0);hx_.fill(0);xn_=hn_=0;i_.fill(0);noiseState_=0x9E3779B97F4A7C15ull;resetPending_=true;}
    // Pin-5 voltage seen by the chip (external network Thevenin already applied;
    // the bias current drop is applied here through the supplied source resistance).
    void setFc(double pin5Volts,double sourceOhms=0){
        const double v=pin5Volts-p_.pin5BiasCurrent*sourceOhms;
        // 0.2 uV hysteresis (0.001 % in frequency) avoids exp/tan per sample while C136 settles.
        if(std::abs(v-lastV5_)<2e-7)return;lastV5_=v;
        const double vt=thermalVoltage(p_.dieTempC);
        const double expoVt=p_.expoMvPerOctave*1e-3/std::log(2.0)*vtScale_;
        tail_=p_.tailAtZero*std::exp(std::clamp(-v/expoVt,-60.0,60.0));
        // Discretisation limit (documented): keep the faster section's natural
        // frequency <= 0.25 fs (FCV below ~0.55 V at 96 kHz, ~0.09 V at 192 kHz),
        // where the EII response is already flat to ~20 kHz.
        const double gmTry=p_.a1*tail_/(2*vt),fFast=gmTry*std::max(std::sqrt(frac1_/(p_.c1*p_.c2)),std::sqrt(frac2_/(p_.c3*p_.c4)))/(2*pi);
        limited_=fFast>0.25*fs_;if(limited_)tail_*=0.25*fs_/fFast;
        // Prewarp: match section-1 natural frequency of the trapezoidal map.
        const double gm=p_.a1*tail_/(2*vt);
        const double w=gm*std::sqrt(frac1_/(p_.c1*p_.c2));
        const double f=std::min(w/(2*pi),0.45*fs_);
        warp_=f>0?std::tan(pi*f/fs_)/(pi*f/fs_):1;
    }
    double tailCurrent()const noexcept{return tail_;}
    bool cutoffLimited()const noexcept{return limited_;}
    void setQCurrent(double amps){if(!(amps>=0)||!std::isfinite(amps))throw std::invalid_argument("ssm2 Q current");iq_=amps;}
    // Source voltage behind rext (EII: deglitch output Y through R138).
    // Trapezoidal rule on C1..C4. V4 and V7 are explicit given V3 and V6, so
    // Newton runs on (V3, V6, Vnode1) with an analytic 3x3 Jacobian.
    Pins process(double source){
        if(!std::isfinite(source))throw std::invalid_argument("ssm2 input");
        const double vt=vt_,I=tail_*warp_,k1=p_.a1/(2*vt),k2=p_.a2/(2*vt),kq=p_.aQ/(2*vt);
        const double iext=source/p_.rext;
        const double en=p_.noise?gauss()*noiseScale_:0;
        const double* vo=p_.offsets?p_.vos:zero4_;const double* io=p_.offsets?p_.ios:zero4_;
        const double g0=h_/(2*p_.c1),g1=h_/(2*p_.c2),g2=h_/(2*p_.c3),g3=h_/(2*p_.c4);
        if(resetPending_){resetPending_=false;Point q;solvePoint(x_[0],x_[2],xn_,I,k1,k2,kq,iext,en,vo,io,0,0,0,0,0,0,0,0,q);i_=q.i;}
        const double b0=x_[0]+g0*i_[0],b1=x_[1]+g1*i_[1],b2=x_[2]+g2*i_[2],b3=x_[3]+g3*i_[3];
        double u0=2*x_[0]-hx_[0],u2=2*x_[2]-hx_[2],un=2*xn_-hn_; // linear predictor
        Point q;bool converged=false;
        solvePoint(u0,u2,un,I,k1,k2,kq,iext,en,vo,io,b0,b1,b2,b3,g0,g1,g2,g3,q);++stats_.iterations;
        // Real-time bound: at most 16 residual evaluations per sample.
        for(unsigned budget=15;budget>0;){
            if(std::abs(q.e0)<1e-12&&std::abs(q.e2)<1e-12&&std::abs(q.e4)*p_.rfb<1e-12){converged=true;break;}
            // Cramer's rule on the 3x3 Jacobian (rows E0,E2,E4; columns u0,u2,un).
            const double (&J)[3][3]=q.J;
            const double det=J[0][0]*(J[1][1]*J[2][2]-J[1][2]*J[2][1])-J[0][1]*(J[1][0]*J[2][2]-J[1][2]*J[2][0])+J[0][2]*(J[1][0]*J[2][1]-J[1][1]*J[2][0]);
            if(!(std::abs(det)>1e-300))break;
            const double r0=-q.e0,r1=-q.e2,r2=-q.e4;
            const double d0=(r0*(J[1][1]*J[2][2]-J[1][2]*J[2][1])-J[0][1]*(r1*J[2][2]-J[1][2]*r2)+J[0][2]*(r1*J[2][1]-J[1][1]*r2))/det;
            const double d1=(J[0][0]*(r1*J[2][2]-J[1][2]*r2)-r0*(J[1][0]*J[2][2]-J[1][2]*J[2][0])+J[0][2]*(J[1][0]*r2-r1*J[2][0]))/det;
            const double d2=(J[0][0]*(J[1][1]*r2-r1*J[2][1])-J[0][1]*(J[1][0]*r2-r1*J[2][0])+r0*(J[1][0]*J[2][1]-J[1][1]*J[2][0]))/det;
            // Converged by step size: a <10 nV correction is applied without
            // re-evaluating (capacitor-current error below 1e-10 A, < -140 dB).
            if(std::max({std::abs(d0),std::abs(d1),std::abs(d2)*p_.rfb*g1_})<1e-8){u0+=d0;u2+=d1;un+=d2;converged=true;break;}
            // Backtracking line search on the residual norm: stiff, saturated
            // sections near Nyquist make plain Newton cycle. The accepted trial
            // point is the next iterate's evaluation (no extra cost when lambda=1).
            const double norm=q.e0*q.e0+q.e2*q.e2+q.e4*q.e4*p_.rfb*p_.rfb;double lambda=1;Point t;
            for(unsigned ls=0;;++ls){
                solvePoint(u0+lambda*d0,u2+lambda*d1,un+lambda*d2,I,k1,k2,kq,iext,en,vo,io,b0,b1,b2,b3,g0,g1,g2,g3,t);++stats_.iterations;--budget;
                if(t.e0*t.e0+t.e2*t.e2+t.e4*t.e4*p_.rfb*p_.rfb<norm||ls==5||budget==0)break;
                lambda*=0.5;
            }
            u0+=lambda*d0;u2+=lambda*d1;un+=lambda*d2;q=t;
        }
        ++stats_.samples;
        if(!converged)++stats_.fallbacks;
        if(!std::isfinite(q.x1+q.x3+u0+u2+un)){reset();++stats_.resets;return {};}
        hx_=x_;hn_=xn_;
        x_={u0,q.x1,u2,q.x3};xn_=un;i_=q.i;
        return {q.v1,q.v8,xn_};
    }
    Stats stats()const noexcept{return stats_;}
    double vt()const noexcept{return vt_;}
    double fraction1()const noexcept{return frac1_;}
    double fraction2()const noexcept{return frac2_;}
private:
    static constexpr double zero4_[4]={0,0,0,0};
    void update(){
        vt_=thermalVoltage(p_.dieTempC);vtScale_=vt_/thermalVoltage(25);noiseScale_=p_.inputNoise*std::sqrt(fs_*0.5);
        g1_=1/p_.rext+1/p_.rin+1/p_.rfb;g2_=2/p_.rfb+1/p_.rin;
        frac1_=(1/p_.rfb)/g1_;frac2_=(1/p_.rfb)/g2_;
        vlim1_=std::min(p_.sinkCurrent/(2/p_.rfb+1/p_.rLoad1),-(p_.negSupply+p_.railHeadroom));
        vlim8_=std::min(p_.sinkCurrent/(1/p_.rfb+1/p_.rLoad8),-(p_.negSupply+p_.railHeadroom));
        hi_=p_.posSupply-p_.railHeadroom;lo_=p_.negSupply+p_.railHeadroom;
    }
    // Class-A follower: linear until the 750 uA sink saturates into its load.
    double buffer(double v,double vlim,double* d)const{
        double y=v,dy=1;
        const double w=0.04*vlim,x=v+vlim;
        if(x<20*w){const double e=std::exp(std::min(x/w,700.0));y=-vlim+w*std::log1p(e);dy=e/(1+e);}
        if(d)*d=dy;return y;
    }
    // OTA output (capacitor node) compliance: identity between the rails minus
    // headroom, smooth saturation (softplus, 0.1 V knee) beyond. Applied to the
    // trapezoidal update so the stored capacitor current stays consistent.
    double rail(double v,double* d)const{
        const double w=0.1;double y=v,dy=1;
        if(v>hi_-30*w){const double z=(hi_-v)/w,e=std::exp(-std::abs(z));const double sp=std::max(z,0.0)+std::log1p(e);
            y=hi_-w*sp;dy=z>=0?1/(1+e):e/(1+e);}
        else if(v<lo_+30*w){const double z=(v-lo_)/w,e=std::exp(-std::abs(z));const double sp=std::max(z,0.0)+std::log1p(e);
            y=lo_+w*sp;dy=z>=0?1/(1+e):e/(1+e);}
        if(d)*d=dy;return y;
    }
    struct Point{double e0=0,e2=0,e4=0,x1=0,x3=0,v1=0,v8=0;double J[3][3]{};std::array<double,4> i{},raw{};};
    void solvePoint(double u0,double u2,double un,double I,double k1,double k2,double kq,double iext,double en,const double* vo,const double* io,
                    double b0,double b1,double b2,double b3,double g0,double g1,double g2,double g3,Point& q)const{
        const double t1=fastTanh(k1*(un-u0+vo[0]+en)),t2=fastTanh(k2*(-u0+vo[1]));
        double r1=1;q.x1=rail(b1+g1*(I*t2+io[1]),&r1);const double dx1=-r1*g1*I*k2*(1-t2*t2);
        double d1=1;q.v1=buffer(q.x1,vlim1_,&d1);
        const double t4=fastTanh(k2*(-u2+vo[3]));
        double r3=1;q.x3=rail(b3+g3*(I*t4+io[3]),&r3);const double dx3=-r3*g3*I*k2*(1-t4*t4);
        double d8=1;q.v8=buffer(q.x3,vlim8_,&d8);
        const double rg=1/(p_.rfb*g2_),vn2=(q.v1+q.v8)*rg;
        const double t3=fastTanh(k1*(vn2-u2+vo[2]));
        const double tq=iq_>0?fastTanh(kq*(un-q.v8)):0;
        double r0=1,r2=1;const double y0=rail(b0+g0*(I*t1+io[0]),&r0),y2=rail(b2+g2*(I*t3+io[2]),&r2);
        q.e0=u0-y0;q.e2=u2-y2;q.e4=g1_*un-iext-q.v1/p_.rfb-iq_*tq;
        // Effective (rail-limited) capacitor currents for the trapezoidal history.
        q.raw={I*t1+io[0],I*t2+io[1],I*t3+io[2],I*t4+io[3]};
        if(g0>0){q.i[0]=(y0-b0)/g0;q.i[1]=(q.x1-b1)/g1;q.i[2]=(y2-b2)/g2;q.i[3]=(q.x3-b3)/g3;}else q.i=q.raw;
        const double s1=I*k1*(1-t1*t1),s3=I*k1*(1-t3*t3),sq=iq_*kq*(1-tq*tq);
        q.J[0][0]=1+r0*g0*s1;q.J[0][1]=0;q.J[0][2]=-r0*g0*s1;
        q.J[1][0]=-r2*g2*s3*rg*d1*dx1;q.J[1][1]=1-r2*g2*s3*(rg*d8*dx3-1);q.J[1][2]=0;
        q.J[2][0]=-d1*dx1/p_.rfb;q.J[2][1]=sq*d8*dx3;q.J[2][2]=g1_-sq;
    }
    double gauss(){ // xorshift64* + sum of 4 uniforms (deterministic, approx. Gaussian, unit variance)
        double s=0;for(int k=0;k<4;++k){noiseState_^=noiseState_>>12;noiseState_^=noiseState_<<25;noiseState_^=noiseState_>>27;
            s+=double((noiseState_*2685821657736338717ull)>>11)*(1.0/9007199254740992.0);}
        return (s-2)*std::sqrt(3.0);
    }
    Params p_;double fs_=96000,h_=1/96000.0,vt_=0.025693,vtScale_=1,g1_=0,g2_=0,frac1_=0.1,frac2_=1.0/11,vlim1_=3,vlim8_=2.2,hi_=13.5,lo_=-3.5;
    double tail_=0,warp_=1,iq_=0,lastV5_=1e300;bool limited_=false;double noiseScale_=0;std::array<double,4> x_{},i_{},hx_{};double xn_=0,hn_=0;bool resetPending_=true;
    std::uint64_t noiseState_=0x9E3779B97F4A7C15ull;Stats stats_{};
};

// PNP control input with grounded/biased base: I = (V - Vbase - Veb(I))/R,
// Veb(I) = Vt*ln(1 + I/Is). Solved by safeguarded Newton (control rate).
inline double pnpControlCurrent(double v,double vbase,double r,double tempC,double vebRef,double iRef){
    const double vt=thermalVoltage(tempC),is=iRef/std::expm1(vebRef/vt);
    double lo=0,hi=std::max(0.0,(v-vbase)/r),i=hi*0.5;
    if(hi<=0)return 0;
    for(unsigned n=0;n<40;++n){
        const double f=v-vbase-i*r-vt*std::log1p(i/is);
        if(f>0)lo=i;else hi=i;
        const double df=-r-vt/(is+i);double next=i-f/df;
        if(!(next>lo&&next<hi))next=0.5*(lo+hi);
        if(std::abs(next-i)<1e-15+1e-12*i){i=next;break;}i=next;
    }
    return i;
}

// EII VCA2 (pins 12/13/14/15): ACV -> R91 4k7 -> C112 1uF -> R100 4k7 -> pin 15,
// 450R + PNP (base = pin 14 at -BIAS.V) -> cell tail. Cell: i = I*tanh(a*v/(2Vt)).
class Vca {
public:
    explicit Vca(double sampleRate=96000,Params p={}):p_(p),fs_(sampleRate){}
    void configure(const Params& p){p_=p;}
    void reset(){vc_=0;}
    // Returns the cell tail current after advancing the loaded RC by one sample.
    double control(double acv,double biasVolts){
        // Unknowns: capacitor voltage v and tail current i (2x2 Newton).
        // c*(v-vp)/h = 0.5*[(acv-vp)/r91 - ip + (acv-v)/r91 - i]
        // v - vbias = i*(r100+450) + Vt*ln(1+i/Is)
        constexpr double r91=4700,c=1e-6;const double r=4700+p_.rVca,h=1/fs_;
        if(p_.dieTempC!=isTemp_||p_.vebRefVolts!=isV_||p_.vebRefCurrent!=isI_){isTemp_=p_.dieTempC;isV_=p_.vebRefVolts;isI_=p_.vebRefCurrent;vtV_=thermalVoltage(isTemp_);is_=isI_/std::expm1(isV_/vtV_);}
        const double vt=vtV_,is=is_;
        const double rhs=c*vc_/h+0.5*((acv-vc_)/r91-tail_+acv/r91);
        // v = (rhs - 0.5*i)/(c/h + 0.5/r91)
        const double den=c/h+0.5/r91;
        double i=tail_;
        for(unsigned n=0;n<30;++n){
            const double v=(rhs-0.5*i)/den;
            const double f=v-biasVolts-i*r-vt*std::log1p(i/is);
            const double df=-0.5/den-r-vt/(is+i);
            double next=i-f/df;if(next<0)next=0.5*i;
            if(std::abs(next-i)<1e-15+1e-10*i){i=next;break;}i=next;
        }
        if(!((rhs/den)-biasVolts>vt*std::log1p(0.0)+0)||i<0)i=0;
        const double v=(rhs-0.5*i)/den;
        // A PNP cannot reverse: if the node sits below threshold the solution is i=0.
        if(v-biasVolts<=0)i=0;
        vc_=(rhs-0.5*i)/den;tail_=i;return tail_;
    }
    double cell(double vin)const{
        const double vt=thermalVoltage(p_.dieTempC);
        const double off=p_.offsets?p_.vcaFeedthrough*tail_:0;
        return tail_*std::tanh(p_.aVca*vin/(2*vt))+off;
    }
    double capacitorVolts()const noexcept{return vc_;}
    double tail()const noexcept{return tail_;}
private:
    Params p_;double fs_;double vc_=0,tail_=0,isTemp_=-1e9,isV_=0,isI_=0,vtV_=0.0257,is_=1e-15;
};
}
