// Frozen 0.68 bypass oracle. Only class names and include path changed.
#pragma once
#include "am6072.h"
#include "filter_calibration.h"
#include "playback_analog.h"
#include "host_dc_block.h"
#include "../../vendor/ssm2045-chip/include/ssm2045/chip.h"
#include <array>
#include <algorithm>

namespace eii::analog {
// EII board adapter; chip's Figure-2 capacitors are NOT the EII values.
// Q pin +600mV emitter approximation and VCA bias are provisional static board assumptions.
class ReferenceNonlinearPlaybackVoice {
public:
    explicit ReferenceNonlinearPlaybackVoice(unsigned rate=96000):chip_(config(rate),vcaConfig()),reference_(rate,2350e-6),
        coupling_(rate,10470e-6),feedback_(rate),rate_(rate),fcAlpha_(1-std::exp(-1.0/(rate*fcResistance*3.3e-6))) {
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
    double process(KnownByte code,KnownSignal acv,KnownSignal fcv,KnownSignal qcv) {
        using P=FilterParam;const auto& c=calibration_;
        if(c[P::AcvOverride])acv={c[P::AcvManual],true};
        if(c[P::FcOverride])fcv={0,true};
        if(c[P::QOverride])qcv={0,true};
        if(code.known!=255||!valid(acv)||!valid(fcv)||!valid(qcv)) {reset();return 0;}
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
        const auto cv=reference_.process(acv);
        const double dac=-2490*Am6072::signedDecodeCurrent(code.value,cv.value/9400);
        const double filtered=chip_.filter().process(dac*(-10000.0/200)*(900.0/68900)*c[P::InputGain]).fourPole;
        // S3528 remains bypassed. Preserve C93/R74/R82 and R154/C161.
        const double dc=coupling_.process({filtered,true}).value;
        const double current=chip_.vca().process(0,(filtered-dc)*(470.0/10470)).current;
        return feedback_.process(current)*c[P::OutputGain];
    }
    void reset() {
        chip_.reset();reference_.resetKnownZero();coupling_.resetKnownZero();feedback_.reset();known_=false;qPin_=0;
    }
private:
    double resistance()const {using P=FilterParam;return 1000/(1/calibration_[P::FcRin]+1/calibration_[P::FcRbias]+1/calibration_[P::FcRshunt]);}
    double fcTarget(double fcv)const {using P=FilterParam;const auto& c=calibration_;return c[P::FcOverride]?c[P::FcManual]*1e-3:(fcv/(c[P::FcRin]*1000)+c[P::FcSupply]/(c[P::FcRbias]*1000))*resistance();}
    static bool valid(KnownSignal v) noexcept {return v.known&&std::isfinite(v.value)&&v.value>=0&&v.value<=5;}
    ssm2045::Chip chip_;
    KnownLowpass reference_,coupling_;
    Channel1Feedback feedback_;
    static constexpr double fcResistance=1/(1.0/22600+1.0/124000+1.0/1000);
    FilterCalibration calibration_;
    unsigned rate_;double fcAlpha_,qAlpha_=1,fcPin_=0,qPin_=0;bool known_=false;
};

// 2x/4x held-input rendering plus 127-tap windowed-sinc anti-alias decimation.
// Not a model of S3528 or the physical deglitch aperture.
class ReferenceNonlinearVoiceBank {
public:
    struct Input {
        PlaybackInputs playback;
        std::array<KnownSignal,8> frequency{},resonance{};
        bool channel0Playback=false;
    };
    explicit ReferenceNonlinearVoiceBank(unsigned factor=2):voices_{
        ReferenceNonlinearPlaybackVoice(48000*factor),ReferenceNonlinearPlaybackVoice(48000*factor),
        ReferenceNonlinearPlaybackVoice(48000*factor),ReferenceNonlinearPlaybackVoice(48000*factor),
        ReferenceNonlinearPlaybackVoice(48000*factor),ReferenceNonlinearPlaybackVoice(48000*factor),
        ReferenceNonlinearPlaybackVoice(48000*factor),ReferenceNonlinearPlaybackVoice(48000*factor)},factor_(factor) {
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
    void calibrate(const FilterCalibration& c){c.validate();for(auto& v:voices_)v.calibrate(c);mixTarget_=c[FilterParam::MixGain];}
    ssm2045::Filter::SolverStats solverStats()const {ssm2045::Filter::SolverStats s;for(auto& v:voices_){auto a=v.solverStats();s.samples+=a.samples;s.iterations+=a.iterations;s.fallbacks+=a.fallbacks;s.recoveries+=a.recoveries;s.held+=a.held;}return s;}
    float process(const Input& in) {
        for(unsigned sub=0;sub<factor_;++sub) {
            double sum=0;
            for(unsigned ch=0;ch<8;++ch) {
                if(ch==0&&!in.channel0Playback)continue;
                sum+=voices_[ch].process(in.playback.codes[ch],in.playback.amplitudeCv[ch],in.frequency[ch],in.resonance[ch]);
            }
            history_[cursor_]=sum;cursor_=(cursor_+1)%taps;
        }
        double out=0;unsigned pos=cursor_;
        for(unsigned n=0;n<taps;++n){pos=(pos+taps-1)%taps;out+=coefficients_[n]*history_[pos];}
        mixGain_+=(mixTarget_-mixGain_)*.001;
        const double ac=hostDc_.process(out*mixGain_);
        clipped_+=std::abs(ac)>1;return float(std::clamp(ac,-1.0,1.0)); //1V host reference after host AC coupling.
    }
    void reset(){for(auto& v:voices_)v.reset();history_.fill(0);cursor_=0;hostDc_.reset();}
    double outputDc()const noexcept{return hostDc_.estimate();}
    std::uint64_t clippedFrames()const noexcept{return clipped_;}
private:
    host::DcBlock hostDc_;std::uint64_t clipped_=0;
    static constexpr unsigned taps=127;
    std::array<ReferenceNonlinearPlaybackVoice,8> voices_;
    std::array<double,taps> coefficients_{},history_{};unsigned cursor_=0,factor_=2;double mixGain_=1,mixTarget_=1;
};
}
