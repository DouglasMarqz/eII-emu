#pragma once
#include "am6072.h"
#include "filter_calibration.h"
#include "playback_analog.h"
#include "host_dc_block.h"
#include "s3528_runtime.h"
#include "native_audio.h"
#include "../vendor/ssm2045-chip/include/ssm2045/chip.h"
#include <array>
#include <algorithm>

namespace eii::analog {
// EII board adapter; chip's Figure-2 capacitors are NOT the EII values.
// Q pin +600mV emitter approximation and VCA bias are provisional static board assumptions.
class GoldenPlaybackVoice {
public:
    explicit GoldenPlaybackVoice(unsigned rate=96000):chip_(config(rate),vcaConfig()),reference_(rate,2350e-6),
        coupling_(rate,10470e-6),feedback_(rate),reconstruction_(rate),rate_(rate),fcAlpha_(1-std::exp(-1.0/(rate*fcResistance*3.3e-6))) {
        chip_.setLinearControlCurrents(0,500e-6);chip_.setExpoVolts(0);reset();
    }
    static ssm2045::FilterConfig config(unsigned rate=96000) {
        ssm2045::FilterConfig c;c.sampleRate=rate;
        c.caps={20e-9,3.9e-9,20e-9,1.8e-9};
        c.fcSmoothingSeconds=0; // Board C136 below, not the demo's500us.
        c.offsets=false; // No measured per-device DC offsets available.
        return c;
    }
    static ssm2045::VcaConfig vcaConfig() {
        ssm2045::VcaConfig c;c.iosVca=0;return c;
    }
    ssm2045::Filter::SolverStats solverStats()const {return chip_.filter().solverStats();}
    void calibrate(const FilterCalibration& c) {
        c.validate();calibration_=c;using P=FilterParam;
        auto f=config(rate_);f.caps={c[P::C1]*1e-9,c[P::C2]*1e-9,c[P::C3]*1e-9,c[P::C4]*1e-9};
        f.gm0=c[P::Gm0]*1e-6;f.vtef=c[P::Vtef]*1e-3;f.fcBiasVolts=c[P::FcBias]*1e-3;
        f.qOscillationCurrent=c[P::Qonset]*1e-6;f.w1=c[P::W1];f.w8=c[P::W8];f.vlGa=c[P::GaLimit];f.vlG1=c[P::G1Limit];f.vlG2=c[P::G2Limit];f.vlT=c[P::TLimit];f.nonlinear=c[P::Nonlinear]!=0;f.offsets=c[P::Offsets]!=0;
        f.vosg1=c[P::Vos1]*1e-3;f.vosg2=c[P::Vos2]*1e-3;f.vosg3=c[P::Vos3]*1e-3;f.vosg4=c[P::Vos4]*1e-3;
        f.iosn1=c[P::Ios1]*1e-9;f.iosn2=c[P::Ios2]*1e-9;f.iosn3=c[P::Ios3]*1e-9;f.iosn4=c[P::Ios4]*1e-9;
        chip_.filter().configure(f);auto v=vcaConfig();v.vcaGain=c[P::VcaGain];v.vlVca=c[P::VcaRange];v.wmin=c[P::VcaFloor];v.vt=c[P::VcaVt]*1e-3;v.iosVca=c[P::VcaOffset]*1e-6;chip_.vca().configure(v);chip_.setExpoVolts(c[P::Expo]);
        fcAlpha_=c[P::FcCap]==0?1:1-std::exp(-1/(rate_*resistance()*c[P::FcCap]*1e-6));
        qAlpha_=c[P::Qtau]==0?1:1-std::exp(-1/(rate_*c[P::Qtau]*1e-3));
        reference_.setTimeConstant(rate_,c[P::ReferenceTau]*1e-3);coupling_.setTimeConstant(rate_,c[P::CouplingTau]*1e-3);
    }
    static double frequencyPinVolts(double fcv) noexcept {
        return (fcv/22600.0-15.0/124000.0)/(1.0/22600.0+1.0/124000.0+1.0/1000.0);
    }
    // Datasheet: grounded-base PNP Q input, source must exceed+600mV.
    static constexpr double qCurrent(double qcv) noexcept {return std::max(0.0,(qcv-.6)/22000.0);}
    double process(KnownByte code,KnownSignal acv,KnownSignal fcv,KnownSignal qcv,KnownByte reconstructionCode={},bool reconstructionEnabled=false,KnownSignal deglitched={},bool native=false) {
        using P=FilterParam;const auto& c=calibration_;
        if(c[P::AcvOverride])acv={c[P::AcvManual],true};
        if(c[P::FcOverride])fcv={0,true};
        if(c[P::QOverride])qcv={0,true};
        if((native?!deglitched.known:code.known!=255)||!valid(acv)||!valid(fcv)||!valid(qcv)) {reset();return 0;}
        if(!known_) {
            fcPin_=fcTarget(fcv.value);known_=true;
            chip_.setFcPinVoltsImmediate(fcPin_);chip_.reset();
        }
        // C1363.3uF across the FC node; ideal resistor Thevenin resistance.
        fcPin_+=fcAlpha_*(fcTarget(fcv.value)-fcPin_);
        qPin_+=qAlpha_*((c[P::QOverride]?c[P::QManual]*1e-6:std::min(c[P::Qmax]*1e-6,std::max(0.0,(qcv.value-c[P::Qemitter])/(c[P::Qres]*1000))*c[P::Qscale]))-qPin_);
        chip_.setFcPinVolts(fcPin_);chip_.setQCurrent(qPin_);
        const double vcaFactor=c[P::VcaFollow]?acv.value/5:1;
        chip_.setLinearControlCurrents(c[P::I16]*1e-6*vcaFactor,c[P::I15]*1e-6*vcaFactor);
        double excitation;
        if(native)excitation=deglitched.value;
        else{const auto cv=reference_.process(acv);const double dac=-2490*Am6072::signedDecodeCurrent(code.value,cv.value/9400);excitation=dac*(-10000.0/200);}
        const double filtered=chip_.filter().process(excitation*(900.0/68900)*c[P::InputGain]).fourPole;
        double reconstructed=filtered;
        if(reconstructionEnabled){
            if(c[P::S3528Override])reconstructionCode={std::uint8_t(c[P::S3528Code]),63};
            reconstructionActive_=true;const auto y=reconstruction_.process({filtered,true},reconstructionCode);
            if(!y.known){coupling_.resetKnownZero();feedback_.reset();return 0;}
            reconstructed=y.value;
        }else if(reconstructionActive_){reconstruction_.reset();reconstructionActive_=false;}
        // S3528 FILT9 feeds C93/R74/R82, then VCA; R154/C161 remains downstream.
        const double dc=coupling_.process({reconstructed,true}).value;
        const double current=chip_.vca().process(0,(reconstructed-dc)*(470.0/10470)).current;
        return feedback_.process(current)*c[P::OutputGain];
    }
    void reset() {
        chip_.reset();reference_.resetKnownZero();coupling_.resetKnownZero();feedback_.reset();known_=false;qPin_=0;reconstruction_.reset();reconstructionActive_=false;
    }
private:
    double resistance()const {using P=FilterParam;return 1000/(1/calibration_[P::FcRin]+1/calibration_[P::FcRbias]+1/calibration_[P::FcRshunt]);}
    double fcTarget(double fcv)const {using P=FilterParam;const auto& c=calibration_;return c[P::FcOverride]?c[P::FcManual]*1e-3:(fcv/(c[P::FcRin]*1000)+c[P::FcSupply]/(c[P::FcRbias]*1000))*resistance();}
    static bool valid(KnownSignal v) noexcept {return v.known&&std::isfinite(v.value)&&v.value>=0&&v.value<=5;}
    ssm2045::Chip chip_;
    KnownLowpass reference_,coupling_;
    Channel1Feedback feedback_;
    S3528Runtime reconstruction_;bool reconstructionActive_=false;
    static constexpr double fcResistance=1/(1.0/22600+1.0/124000+1.0/1000);
    FilterCalibration calibration_;
    unsigned rate_;double fcAlpha_,qAlpha_=1,fcPin_=0,qPin_=0;bool known_=false;
};

// 96/192k nonlinear solver and127-tap anti-alias decimation. The reference
// path holds48k observations; native packets integrate DAC/deglitch between
// exact hardware events. VCF/CV controls remain on the selected solver grid.
class GoldenVoiceBank {
public:
    struct Input {
        PlaybackInputs playback;
        std::array<KnownSignal,8> frequency{},resonance{};
        bool channel0Playback=false;
        std::array<KnownByte,8> reconstructionCode{};
        bool reconstructionEnabled=false;
        NativeFrame native{};
    };
    explicit GoldenVoiceBank(unsigned factor=2):voices_{
        GoldenPlaybackVoice(48000*factor),GoldenPlaybackVoice(48000*factor),
        GoldenPlaybackVoice(48000*factor),GoldenPlaybackVoice(48000*factor),
        GoldenPlaybackVoice(48000*factor),GoldenPlaybackVoice(48000*factor),
        GoldenPlaybackVoice(48000*factor),GoldenPlaybackVoice(48000*factor)},factor_(factor) {
        if(factor!=2&&factor!=4)throw std::invalid_argument("Chip oversampling requires2or4");
        constexpr double pi=3.14159265358979323846;
        const double fc=20000.0/(48000*factor);
        double sum=0;
        for(unsigned n=0;n<taps;++n) {
            const int k=int(n)-int(taps/2);
            const double sinc=k?std::sin(2*pi*fc*k)/(pi*k):2*fc;
            const double window=.42-.5*std::cos(2*pi*n/(taps-1))+.08*std::cos(4*pi*n/(taps-1));
            coefficients_[n]=sinc*window;sum+=coefficients_[n];
        }
        for(auto& c:coefficients_)c/=sum;
    }
    void calibrate(const FilterCalibration& c){c.validate();for(auto& v:voices_)v.calibrate(c);mixTarget_=c[FilterParam::MixGain];nativeCalibration_=c;nativeCoefficients_.configure(c);nativeGainTarget_=std::pow(10.,c[FilterParam::NativeOutDb]/20);}
    ssm2045::Filter::SolverStats solverStats()const {ssm2045::Filter::SolverStats s;for(auto& v:voices_){auto a=v.solverStats();s.samples+=a.samples;s.iterations+=a.iterations;s.fallbacks+=a.fallbacks;s.recoveries+=a.recoveries;s.held+=a.held;}return s;}
    float process(const Input& in) {
        const bool native=in.native.enabled;
        if(native!=nativeLast_||(native&&(nativeExpected_!=in.native.start||nativeInvalid_||!in.native.valid))){reset();nativeLast_=native;}
        nativeInvalid_=native&&!in.native.valid;
        NativePins pins=in.native.initial;unsigned event=0;std::array<unsigned,8> advanced{};
        std::array<double,8> areas{};std::array<bool,8> known{};known.fill(in.native.valid);
        auto cv=[&](unsigned bank,unsigned ch)->KnownSignal{const auto x=pins.cv[bank][ch];return {5.0*x.value/256,x.known==255};};
        auto advance=[&](unsigned ch,unsigned time){
            if(ch==0&&!pins.channel0){nativeDac_[ch].reset();advanced[ch]=time;return;}
            auto acv=cv(0,ch);if(nativeCalibration_[FilterParam::AcvOverride])acv={nativeCalibration_[FilterParam::AcvManual],true};
            auto y=nativeDac_[ch].advance(time-advanced[ch],pins.dac[ch],acv,pins.phases[ch],nativeCoefficients_);
            areas[ch]+=y.value;known[ch]=known[ch]&&y.known;advanced[ch]=time;
        };
        for(unsigned sub=0;sub<factor_;++sub) {
            if(native){const unsigned end=(sub+1)*(nativeFrameTime/factor_);
                while(event<in.native.count&&in.native.events[event].offset<=end){const auto& e=in.native.events[event++];advance(e.channel&7,e.offset);applyNativeEvent(pins,e);}
                for(unsigned ch=0;ch<8;++ch)advance(ch,end);
            }
            double sum=0;
            for(unsigned ch=0;ch<8;++ch) {
                if(ch==0&&!(native?pins.channel0:in.channel0Playback))continue;
                if(native)sum+=voices_[ch].process(pins.dac[ch],cv(0,ch),cv(1,ch),cv(2,ch),pins.sw[ch],in.reconstructionEnabled,{areas[ch]/(nativeFrameTime/factor_),known[ch]},true);
                else sum+=voices_[ch].process(in.playback.codes[ch],in.playback.amplitudeCv[ch],in.frequency[ch],in.resonance[ch],in.reconstructionCode[ch],in.reconstructionEnabled);
            }
            if(native){areas={};known.fill(in.native.valid);}
            history_[cursor_]=sum;cursor_=(cursor_+1)%taps;
        }
        if(native)nativeExpected_=in.native.start+nativeFrameTime;
        double out=0;unsigned pos=cursor_;
        for(unsigned n=0;n<taps;++n){pos=(pos+taps-1)%taps;out+=coefficients_[n]*history_[pos];}
        mixGain_+=(mixTarget_-mixGain_)*.001;
        const double ac=hostDc_.process(out*mixGain_);
        nativeGain_+=(nativeGainTarget_-nativeGain_)*.001;
        const double monitored=ac*(native?nativeGain_:1); // Post-chain monitor gain: never changes VCF/VCA drive.
        clipped_+=std::abs(monitored)>1;return float(std::clamp(monitored,-1.0,1.0)); //1V host reference after host AC coupling.
    }
    void reset(){for(auto& v:voices_)v.reset();for(auto& d:nativeDac_)d.reset();history_.fill(0);cursor_=0;hostDc_.reset();nativeExpected_=0;}
    double outputDc()const noexcept{return hostDc_.estimate();}
    std::uint64_t clippedFrames()const noexcept{return clipped_;}
private:
    host::DcBlock hostDc_;std::uint64_t clipped_=0;
    static constexpr unsigned taps=127;
    std::array<GoldenPlaybackVoice,8> voices_;
    std::array<NativeDacChannel,8> nativeDac_{};NativeDacCoefficients nativeCoefficients_;FilterCalibration nativeCalibration_;
    bool nativeLast_=false,nativeInvalid_=false;std::uint64_t nativeExpected_=0;
    std::array<double,taps> coefficients_{},history_{};unsigned cursor_=0,factor_=2;double mixGain_=1,mixTarget_=1,nativeGain_=1,nativeGainTarget_=1;
};
}
