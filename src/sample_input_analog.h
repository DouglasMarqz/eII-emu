#pragma once
#include "am6072.h"
#include "s3528_runtime.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace eii::analog {
// External signal at +INPUT.V ("Atten'd input signal", output net list PDF124),
// i.e. after the panel GAIN pot. Host samples map +/-1.0 to +/-voltsFullScale
// (documented nominal choice: 1.0 V peak) times the pot setting 0..1.
// Cubic interpolation on emulated time; 0 V before start/after end (jack open).
// Any +INPUT.V source. volts() is called by the machine thread only, in
// emulated-time order; idle() lets a live source discard host audio while
// the converter is not sampling (it keeps no machine state).
struct SampleInputSource {
    virtual ~SampleInputSource()=default;
    virtual double volts(std::uint64_t tick,double frac=0)=0;
    virtual void idle(std::uint64_t) {}
    // Live sources may hold the machine until input for `ticks` exists.
    virtual bool ready(std::uint64_t /*now*/,std::uint64_t /*ticks*/) {return true;}
};
struct SampleInputSignal final:SampleInputSource {
    std::vector<float> samples;
    double rate=48000,voltsFullScale=1.0,gainPot=1.0;
    std::uint64_t startTick=0;
    double volts(std::uint64_t tick,double frac=0) noexcept override {
        if(samples.empty()||tick<startTick)return 0;
        const double t=(double(tick-startTick)+frac)/4e6*rate;
        const auto i=std::uint64_t(t);
        if(i+1>=samples.size())return 0;
        const double f=t-double(i);
        // 4-point cubic (Catmull-Rom), band-limited enough for 48 kHz -> SAR.
        const double a=samples[i?i-1:i],b=samples[i],c=samples[i+1],d=i+2<samples.size()?samples[i+2]:samples[i+1];
        return (b+0.5*f*(c-a+f*(2*a-5*b+4*c-d+f*(3*(b-c)+d-a))))*voltsFullScale*gainPot;
    }
};
// Output board 7-42 (PDF104) input analog path plus channel-0 IINT (7-44).
// Nominal component values, ideal op-amps, first-order RC networks:
//  IC86 S3528 input amplifier: SIG summing node, SIGFB output.
//   Rin = C95 1uF + R84 100k; +PGAIN2 (DG202 on at logic1) adds C86 4.7uF + R51 11k.
//   Rf = R49 1M || C76 10pF; -PGAIN1 high (DG202 on) adds R50 110k || C85 100pF.
//  IC86 filter = S3528Runtime (AMI1993 fit, 192 kHz) with the -CSWF0 code
//   shared with channel 0 (FD bus); BUFIN/BUFOUT R93/C104 pole 33.9 kHz.
//  C67 4.7uF into R48 10k; IC96 MC34004: -3.3 x BUF + 1.0 x IINT (C66 4.7uF,
//   R46 33k/R45 10k), rails +/-13.5 V. DG202 S/H onto C40 while +ISMPL is high
//   (settled, Ron x C40 = 0.27us). LF311: IDATA = V(+IDAC.V) > V(C40).
//  IINT: channel-0 3202 deglitch (R27 200/R35 10k, C75/C84 470pF) driven by
//   +IDAC.V with the channel-0 phi1/phi2 phases, evaluated settled per phase.
//  ACV0 reference RC (R173/R39 4.7k, C176 1uF) tau 2.35 ms.
//  IENV (7-42 IC44, read from the native 318 ppi scan): IC44a with R40/R10
//   100k, D2 (output->C27) and D3 (clamp) is a precision peak detector of the
//   NEGATIVE half of the C66-coupled IINT onto C27 1uF; C27 discharges
//   through R10 into the virtual ground (tau 0.1 s). D6 limits IC44a to
//   +5V+Vf so C27 <= 5 V; IC44d (R19, D4) with D1/D7 holds IENV >= 0.
//   IC44b buffers C27 -> +IENV.V -> R26 1k -> ADC0809 IN1 (7-8).
//   Ideal diodes/op-amps; diode drops cancel inside the precision loops.
// NOT modeled: RT1 offset (ideal 0), S3528 clock images, DG202 charge
// injection, op-amp slew/bandwidth, switch-open coupling-cap drift.
// The model starts "quiet" (all capacitors discharged, IINT = 0, ACV RC
// settled) at the first tick after IMODE=1 with a fully known DAC bus; this
// is a documented assumption (the deglitch leak forgets the start in ~36 ms).
class SampleInputAnalog {
public:
    struct Pins {
        LogicLevel pgain1N{},pgain2{},ismpl{},phi1{},phi2{};
        KnownByte filterCode{};
        KnownSignal acv0{};
    };
    std::uint64_t chainSteps=0,unknownComparisons=0,railClips=0;
    void reset(KnownSignal acv0,std::uint64_t now=0) noexcept {
        hp1_=hp2_=Hp{};gainOut_=buf_=0;bufHp_=Hp{};iintHp_=Hp{};
        filter_.reset();sample_=output_={0,true};held_={0,true};env_={0,true};
        acv_=acv0;chainKnown_=true;bufHpOut_=0;phi1Prev_=phi2Prev_=false;
        nextStep_=double(now);last_=now;gainKey_=-1;
    }
    KnownSignal referenceCv() const noexcept {return acv_;}
    KnownSignal iint() const noexcept {return output_;}
    KnownSignal held() const noexcept {return held_;}
    KnownSignal envelope() const noexcept {return env_;}
    double bufferVolts() const noexcept {return bufHpOut_;}
    // Evaluate at CPU tick `now` (the machine calls this on a 6-tick grid that
    // contains every C333K edge; +ISMPL and phi1/phi2 last >= 6 ticks, so each
    // pulse is observed). idac = +IDAC.V now; returns +IDATA.D.
    LogicLevel tick(const Pins& p,SampleInputSource* signal,std::uint64_t now,KnownSignal idac) {
        const std::uint64_t elapsed=now>last_?now-last_:0;last_=now;
        const bool grid=elapsed==6;
        // ACV0 reference RC (tau 2.35 ms).
        if(!p.acv0.known||!acv_.known)acv_=p.acv0;
        else acv_.value+=(p.acv0.value-acv_.value)*(grid?acvAlpha6:-std::expm1(-double(elapsed)*0.25e-6/2350e-6));
        // 192 kHz input chain steps due up to now (exact fractional times).
        while(nextStep_<=double(now)){
            const auto t=std::uint64_t(nextStep_);chainStep(p,signal,t,nextStep_-double(t));nextStep_+=125.0/6.0;}
        // Channel-0 3202 deglitch (7-44 R27/R35, C75/C84), settled per phase:
        // phi1 charges C75 to the R27/R35 divider (tau 0.16 us << 1.6 us) and
        // tracks the DAC while closed, phi2 dumps it into the IC75 integrator
        // (tau 0.07 us). Deglitch3202 per tick is the oracle (sample_analog_tests).
        if(!p.phi1.known||!p.phi2.known||(p.phi1.high&&p.phi2.high)){sample_=output_={};}
        else {
            if(p.phi1.high){
                if(!output_.known||!idac.known)sample_={};
                else sample_={(10000*idac.value+200*output_.value)/10200,true};}
            if(p.phi2.high&&!phi2Prev_){
                if(!sample_.known||!output_.known){sample_=output_={};}
                else {output_.value-=sample_.value;sample_.value=0;}}
        }
        phi1Prev_=p.phi1.known&&p.phi1.high;phi2Prev_=p.phi2.known&&p.phi2.high;
        // C66 coupling of IINT and the IENV detector.
        double iintHp=0;const bool iintKnown=output_.known;
        if(iintKnown)iintHp=iintHp_.step(output_.value,grid?iintAlpha6:0.202/(0.202+double(elapsed)*0.25e-6));else iintHp_=Hp{};
        if(!iintKnown)env_={};
        else {const double x=std::min(-iintHp,5.0);
            if(!env_.known)env_={std::max(0.0,x),true};
            else env_.value=std::max(0.0,x>env_.value?x:env_.value*(grid?envDecay6:std::exp(-double(elapsed)*0.25e-6/0.1)));}
        // IC96 summing amplifier and the C40 sample-and-hold.
        const bool known=iintKnown&&chainKnown_;
        double v=iintHp-3.3*bufHpOut_;
        if(v>13.5||v<-13.5){v=std::clamp(v,-13.5,13.5);++railClips;}
        if(!p.ismpl.known)held_={};
        else if(p.ismpl.high)held_={v,known};
        // LM311: pin2 IN+ = +IDAC.V, pin3 IN- = C40.
        if(!held_.known||!idac.known){++unknownComparisons;return {};}
        return {true,idac.value>held_.value};
    }
private:
    struct Hp {double y=0,x=0;double step(double in,double a) noexcept {y=a*(y+in-x);x=in;return y;}};
    static constexpr double chainDt=1.0/192000;
    static inline const double envDecay6=std::exp(-1.5e-6/0.1);      // R10 x C27
    static inline const double acvAlpha6=-std::expm1(-1.5e-6/2350e-6);
    static inline const double iintAlpha6=0.202/(0.202+1.5e-6);   // C66 x (R46+R45)
    static inline const double hp1Alpha=0.1/(0.1+chainDt);         // C95 x R84
    static inline const double hp2Alpha=0.0517/(0.0517+chainDt);   // C86 x R51
    static inline const double bufAlpha=-std::expm1(-chainDt/(10e3*470e-12));
    static inline const double c67Alpha=0.047/(0.047+chainDt);     // C67 x R48
    Hp hp1_{},hp2_{},bufHp_{},iintHp_{};
    double gainOut_=0,buf_=0,bufHpOut_=0;
    S3528Runtime filter_{192000};
    KnownSignal sample_{0,true},output_{0,true},held_{0,true},acv_{},env_{0,true};
    double nextStep_=0,gainAlpha_=0,gainRf_=0;std::uint64_t last_=0;int gainKey_=-1;
    bool chainKnown_=true,phi1Prev_=false,phi2Prev_=false;
    void chainStep(const Pins& p,SampleInputSource* signal,std::uint64_t tickIndex,double frac) {
        ++chainSteps;
        const double vin=signal?signal->volts(tickIndex,frac):0.0;
        const double i1=hp1_.step(vin,hp1Alpha)/100e3,i2=hp2_.step(vin,hp2Alpha)/11e3;
        if(!p.pgain1N.known||!p.pgain2.known){chainKnown_=false;filter_.reset();return;}
        const int key=p.pgain1N.high?1:0;
        if(key!=gainKey_){gainKey_=key;                              // cached per -PGAIN1 state
            gainRf_=key?1.0/(1.0/1e6+1.0/110e3):1e6;const double cf=key?110e-12:10e-12;
            gainAlpha_=-std::expm1(-chainDt/(gainRf_*cf));}
        const double i=i1+(p.pgain2.high?i2:0.0);
        gainOut_+=(-gainRf_*i-gainOut_)*gainAlpha_;
        const auto f=filter_.process({gainOut_,true},p.filterCode);
        if(!f.known){chainKnown_=false;return;}
        buf_+=(f.value-buf_)*bufAlpha;
        bufHpOut_=bufHp_.step(buf_,c67Alpha);
        chainKnown_=true;
    }
};
}
