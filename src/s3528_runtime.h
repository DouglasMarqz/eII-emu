#pragma once
#include "s3528.h"

namespace eii::analog {
// EII board input of each S3528 (service sheet 7-45, PDF107, IC84 channel1):
// SSM2045 4P (pin8) -> R66 4.7k -> SIG IN (pin12, inverting input of the
// on-chip amplifier; non-inverting input is internal AGND, AMI 1993 PDF1/4)
// with R58 10k feedback from FB (pin10). C172 100pF loads FB to ground (no
// audio-band pole for an ideal amplifier). The amplifier output feeds the
// cosine prefilter, so the stage gain is -R58/R66 before the 0dB passband.
inline constexpr double s3528BoardInputGain=-10000.0/4700.0;
// The stage is applied with S3528 ON and OFF (bypass removes only the filter);
// the host level is common to both (level_model.h, hostOutputReference).
// Nominal audio-rate realization of the existing AMI1993 fitted prototype.
// Bilinear frequency mapping preserves the nominal cutoff at each renderer
// rate. TPT state-variable sections retain integrator states on code changes.
// No5MHz loop; not a switched-capacitor/divider-phase or clock-image model.
class S3528Runtime {
public:
    explicit S3528Runtime(unsigned rate):rate_(rate){if(rate!=96000&&rate!=192000)throw std::invalid_argument("S3528 runtime requires96/192k");}
    void reset()noexcept{for(auto& s:sections_){s.ic1=s.ic2=0;}ready_=false;lastCode_=64;}
    KnownSignal process(KnownSignal input,KnownByte control){
        if(!input.known||(control.known&63)!=63){reset();return {};}
        if(!std::isfinite(input.value))throw std::invalid_argument("Nonfinite S3528 runtime input");
        const unsigned code=control.value&63;if(code!=lastCode_)configure(code);
        double x=input.value;
        for(auto& s:sections_){
            const double v1=s.a1*(s.ic1+s.g*(x-s.ic2)),v2=s.ic2+s.g*v1;
            s.ic1=2*v1-s.ic1;s.ic2=2*v2-s.ic2;
            x=s.hp*(x-s.k*v1-v2)+s.bp*v1+s.lp*v2;
        }
        if(!std::isfinite(x)){reset();return {};}
        ready_=true;return {x,true};
    }
    double cutoffHz()const noexcept{return lastCode_<64?5e6/(40*S3528Control::divisors[lastCode_]):0;}
    bool ready()const noexcept{return ready_;}
private:
    struct Section{double g=0,k=0,a1=0,hp=0,bp=0,lp=0,ic1=0,ic2=0;};
    unsigned rate_,lastCode_=64;bool ready_=false;std::array<Section,4> sections_{};
    void configure(unsigned code){
        constexpr double pi=3.14159265358979323846;
        const double fc=5e6/(40*S3528Control::divisors[code]);
        const double scale=std::tan(pi*fc/rate_)/std::tan(pi/40);
        for(unsigned i=0;i<4;++i){
            const auto& c=s3528Ami1993Sos[i];auto& s=sections_[i];
            // Inverse bilinear prototype q=(1-s)/(1+s). Each section is then
            // realized with LP/BP/HP outputs of a linear TPT SVF.
            const double d0=1+c[3]+c[4],d1=2*(1-c[4]),d2=1-c[3]+c[4];
            const double w=std::sqrt(d0/d2);
            s.k=d1/std::sqrt(d0*d2);s.g=w*scale;s.a1=1/(1+s.g*(s.g+s.k));
            s.hp=(c[0]-c[1]+c[2])/d2;s.bp=2*(c[0]-c[2])/(d2*w);s.lp=(c[0]+c[1]+c[2])/d0;
        }
        lastCode_=code;
    }
};
}
