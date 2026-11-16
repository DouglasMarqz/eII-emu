#pragma once
#include "am6072.h"
#include "fir_ordered.h"
#include "legacy_q_filter.h"
#include "eiii_custom_filter.h"
#include <memory>
#include "legacy_q_vca.h"
#include "vca_board_control.h"
#include "filter_calibration.h"
#include "playback_analog.h"
#include "host_dc_block.h"
#include "s3528_runtime.h"
#include "native_audio.h"
#include "ssm2045_model2.h"
#include "level_model.h"
#include "thermal_drift.h"
#include "../vendor/ssm2045-chip/include/ssm2045/chip.h"
#include <array>
#include <algorithm>

namespace eii::analog {
// EII board adapter; chip's Figure-2 capacitors are NOT the EII values.
// Q pin +600mV emitter approximation and VCA bias are provisional static board assumptions.
class NonlinearPlaybackVoice {
public:
    explicit NonlinearPlaybackVoice(unsigned rate=96000,bool legacyQ=false,bool eiii=false,bool ssm2=false):chip_(config(rate),vcaConfig()),fast_(config(rate)),legacyQ_(legacyQ),reference_(rate,2350e-6),
        coupling_(rate,10470e-6),feedback_(rate),reconstruction_(rate),vcaControl_(rate),rate_(rate),fcAlpha_(1-std::exp(-1.0/(rate*fcResistance*3.3e-6))) {
        if(eiii)eiii_=std::make_unique<EiiiCustomFilter>(rate);
        if(ssm2)ssm2_=std::make_unique<Ssm2Chain>(rate);
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
    ssm2045::Filter::SolverStats solverStats()const {
        if(ssm2_){const auto s=ssm2_->filter.stats();ssm2045::Filter::SolverStats r;r.samples=s.samples;r.iterations=s.iterations;r.fallbacks=s.fallbacks;r.recoveries=s.resets;return r;}
        return legacyQ_?fast_.solverStats():chip_.filter().solverStats();}
    void calibrate(const FilterCalibration& c) {
        c.validate();calibration_=c;legacyVca_.calibrate(c);using P=FilterParam;
        auto f=config(rate_);f.caps={c[P::C1]*1e-9,c[P::C2]*1e-9,c[P::C3]*1e-9,c[P::C4]*1e-9};
        f.gm0=c[P::Gm0]*1e-6;f.vtef=c[P::Vtef]*1e-3;f.fcBiasVolts=c[P::FcBias]*1e-3;
        f.qOscillationCurrent=c[P::Qonset]*1e-6;f.w1=c[P::W1];f.w8=c[P::W8];f.vlGa=c[P::GaLimit];f.vlG1=c[P::G1Limit];f.vlG2=c[P::G2Limit];f.vlT=c[P::TLimit];f.nonlinear=c[P::Nonlinear]!=0;f.offsets=c[P::Offsets]!=0;
        f.vosg1=c[P::Vos1]*1e-3;f.vosg2=c[P::Vos2]*1e-3;f.vosg3=c[P::Vos3]*1e-3;f.vosg4=c[P::Vos4]*1e-3;
        f.iosn1=c[P::Ios1]*1e-9;f.iosn2=c[P::Ios2]*1e-9;f.iosn3=c[P::Ios3]*1e-9;f.iosn4=c[P::Ios4]*1e-9;
        chip_.filter().configure(f);fast_.configure(f);auto v=vcaConfig();v.vcaGain=c[P::VcaGain];v.vlVca=c[P::VcaRange];v.wmin=c[P::VcaFloor];v.vt=c[P::VcaVt]*1e-3;v.iosVca=c[P::VcaOffset]*1e-6;chip_.vca().configure(v);chip_.setExpoVolts(c[P::Expo]);
        fcAlpha_=c[P::FcCap]==0?1:1-std::exp(-1/(rate_*resistance()*c[P::FcCap]*1e-6));
        qAlpha_=c[P::Qtau]==0?1:1-std::exp(-1/(rate_*c[P::Qtau]*1e-3));
        reference_.setTimeConstant(rate_,c[P::ReferenceTau]*1e-3);coupling_.setTimeConstant(rate_,c[P::CouplingTau]*1e-3);
        if(ssm2_)ssm2_->configure(c);
    }
    static double frequencyPinVolts(double fcv) noexcept {
        return (fcv/22600.0-15.0/124000.0)/(1.0/22600.0+1.0/124000.0+1.0/1000.0);
    }
    // Datasheet (SSM 1983 PDF1 Figure1, PDF3 text): pin17 feeds a grounded-base
    // PNP emitter through an internal 1.8k; source must exceed +600mV. The
    // board R146 22k (7-45) is in series with that internal resistor.
    static constexpr double qInternalOhms=1800;
    static constexpr double qCurrent(double qcv) noexcept {return std::max(0.0,(qcv-.6)/(22000.0+qInternalOhms));}
    double process(KnownByte code,KnownSignal acv,KnownSignal fcv,KnownSignal qcv,KnownByte reconstructionCode={},bool reconstructionEnabled=false,KnownSignal deglitched={},bool native=false) {
        if(ssm2_)return processModel2(code,acv,fcv,qcv,reconstructionCode,reconstructionEnabled,deglitched,native);
        using P=FilterParam;const auto& c=calibration_;
        if(c[P::AcvOverride])acv={c[P::AcvManual],true};
        if(c[P::FcOverride])fcv={0,true};
        if(c[P::QOverride])qcv={0,true};
        if((native?!deglitched.known:code.known!=255)||!valid(acv)||!valid(fcv)||!valid(qcv)) {reset();return 0;}
        if(!known_) {
            fcPin_=fcTarget(fcv.value);known_=true;
            chip_.setFcPinVoltsImmediate(fcPin_*thermal_);chip_.reset();
        }
        // C1363.3uF across the FC node; ideal resistor Thevenin resistance.
        fcPin_+=fcAlpha_*(fcTarget(fcv.value)-fcPin_);
        qPin_+=qAlpha_*((c[P::QOverride]?c[P::QManual]*1e-6:std::min(c[P::Qmax]*1e-6,std::max(0.0,(qcv.value-c[P::Qemitter])/(c[P::Qres]*1000+qInternalOhms))*c[P::Qscale]))-qPin_);
        chip_.setFcPinVolts(fcPin_*thermal_);chip_.setQCurrent(qPin_);
        const double boardVcaFactor=vcaControl_.process(acv.value)/500e-6;
        const double vcaFactor=c[P::VcaFollow]?acv.value/5:boardVcaFactor;
        chip_.setLinearControlCurrents(c[P::I16]*1e-6*vcaFactor,c[P::I15]*1e-6*vcaFactor);
        double excitation;
        if(native)excitation=deglitched.value;
        else{const auto cv=reference_.process(acv);const double dac=-2490*Am6072::signedDecodeCurrent(code.value,cv.value/9400);excitation=opampSwing(dac*(-10000.0/200)*level_.fastRouteGain,level_.swing);}
        const double drive=excitation*(900.0/68900)*c[P::InputGain];
        // Vendor-y VCF output scaled to SSM2045 pin-8 volts (level_model.h).
        const double filtered=(eiii_?eiii_->process(drive,fcPin_*thermal_,qPin_,c):legacyQ_?fast_.process(drive,fcPin_*thermal_,qPin_).fourPole:chip_.filter().process(drive).fourPole)*level_.pinScale;
        double reconstructed=filtered;
        if(reconstructionEnabled){
            if(c[P::S3528Override])reconstructionCode={std::uint8_t(c[P::S3528Code]),63};
            // 7-45 R66/R58 stage = S3528 SIG/SIGFB amplifier on +/-5 V (S3528 swing).
            reconstructionActive_=true;const auto y=reconstruction_.process({opampSwing(filtered*s3528BoardInputGain,level_.s3528Swing),true},reconstructionCode);
            if(!y.known){coupling_.resetKnownZero();feedback_.reset();return 0;}
            reconstructed=y.value;
        }else{
            if(reconstructionActive_){reconstruction_.reset();reconstructionActive_=false;}
            // Bypass removes only the S3528 filter: the 7-45 R66/R58 stage stays.
            if(level_.s3528OffInputStage)reconstructed=opampSwing(filtered*s3528BoardInputGain,level_.s3528Swing);
        }
        // S3528 FILT9 feeds C93/R74/R82, then VCA; R154/C161 remains downstream.
        const double dc=coupling_.process({reconstructed,true}).value;
        const double vcaInput=(reconstructed-dc)*(470.0/10470);
        const double current=legacyQ_?legacyVca_.process(vcaInput,vcaFactor):chip_.vca().process(0,vcaInput).current;
        // CnOUT.V: S3528 BUFOUT amplifier with R154/C161 (+/-5 V swing), then trims.
        const double out=opampSwing(feedback_.process(current),level_.s3528Swing)*c[P::OutputGain]*level_.hostReference;
        return reconstructionEnabled?out*level_.s3528OnMatch:out;
    }
    // Estimated die-temperature drift (thermal_drift.h): exactly 1 when off.
    void thermalScale(double s)noexcept{thermal_=s;}
    double dieCelsius()const noexcept{return calibration_[FilterParam::Ssm2DieTemp];}
    // Tests only: LevelModel::ce2e306() reproduces the frozen oracles bit-exactly.
    void levelModel(const LevelModel& l)noexcept{level_=l;}
    const LevelModel& levelModel()const noexcept{return level_;}
    void reset() {
        if(ssm2_)ssm2_->reset();
        if(eiii_)eiii_->reset();chip_.reset();fast_.reset();reference_.resetKnownZero();coupling_.resetKnownZero();feedback_.reset();known_=false;qPin_=0;reconstruction_.reset();reconstructionActive_=false;vcaControl_.reset();
    }
private:
    // SSM2045 model 2 chain (docs/SSM2045_MODEL2.md): real pin levels, own Q/VCA
    // control transistors; deglitch swing and host level from level_model.h.
    struct Ssm2Chain{
        explicit Ssm2Chain(unsigned rate):filter(rate),vca(rate){configure(FilterCalibration{});}
        void configure(const FilterCalibration& c){
            using P=FilterParam;ssm2::Params p;
            p.c1=c[P::C1]*1e-9;p.c2=c[P::C2]*1e-9;p.c3=c[P::C3]*1e-9;p.c4=c[P::C4]*1e-9;
            p.dieTempC=c[P::Ssm2DieTemp];p.noise=c[P::Ssm2Noise]!=0;p.offsets=c[P::Ssm2Offsets]!=0;
            p.a1=p.a2=c[P::Ssm2OtaAtten];p.aQ=c[P::Ssm2QAtten];p.aVca=c[P::Ssm2VcaAtten];
            p.tailAtZero=c[P::Ssm2Tail]*1e-9;p.expoMvPerOctave=c[P::Ssm2Expo];p.sinkCurrent=c[P::Ssm2Sink]*1e-6;
            filter.configure(p);vca.configure(p);params=p;lastQcv=-1;
        }
        void reset(){filter.reset();vca.reset();}
        ssm2::Filter filter;ssm2::Vca vca;ssm2::Params params;double lastQcv=-1,lastQres=-1,qRaw=0;
    };
    double processModel2(KnownByte code,KnownSignal acv,KnownSignal fcv,KnownSignal qcv,KnownByte reconstructionCode,bool reconstructionEnabled,KnownSignal deglitched,bool native){
        using P=FilterParam;const auto& c=calibration_;auto& m=*ssm2_;
        if(c[P::AcvOverride])acv={c[P::AcvManual],true};
        if(c[P::FcOverride])fcv={0,true};
        if(c[P::QOverride])qcv={0,true};
        if((native?!deglitched.known:code.known!=255)||!valid(acv)||!valid(fcv)||!valid(qcv)) {reset();return 0;}
        if(!known_){fcPin_=fcTarget(fcv.value);known_=true;m.reset();}
        fcPin_+=fcAlpha_*(fcTarget(fcv.value)-fcPin_);
        // Pin-5 bias current (1 uA typ.) flows through the board Thevenin resistance.
        m.filter.setFc(fcPin_*thermal_,c[P::FcOverride]?0:resistance());
        if(qcv.value!=m.lastQcv||c[P::Qres]!=m.lastQres){m.lastQcv=qcv.value;m.lastQres=c[P::Qres];
            m.qRaw=ssm2::pnpControlCurrent(qcv.value,0,c[P::Qres]*1000+ssm2::Params{}.rq,m.params.dieTempC,m.params.vebRefVolts,m.params.vebRefCurrent);}
        const double qRaw=m.qRaw;
        qPin_+=qAlpha_*((c[P::QOverride]?c[P::QManual]*1e-6:std::min(c[P::Qmax]*1e-6,qRaw*c[P::Qscale]))-qPin_);
        m.filter.setQCurrent(qPin_);
        m.vca.control(acv.value,VcaBoardControl::biasVolts);
        double y;
        if(native)y=deglitched.value;
        else{const auto cv=reference_.process(acv);const double dac=-2490*Am6072::signedDecodeCurrent(code.value,cv.value/9400);y=opampSwing(dac*(-10000.0/200)*level_.fastRouteGain,level_.swing);}
        y*=c[P::InputGain];
        const double pin8=m.filter.process(y).pin8;
        double reconstructed=pin8;
        if(reconstructionEnabled){
            if(c[P::S3528Override])reconstructionCode={std::uint8_t(c[P::S3528Code]),63};
            reconstructionActive_=true;const auto r=reconstruction_.process({opampSwing(pin8*s3528BoardInputGain,level_.s3528Swing),true},reconstructionCode);
            if(!r.known){coupling_.resetKnownZero();feedback_.reset();return 0;}
            reconstructed=r.value;
        }else{
            if(reconstructionActive_){reconstruction_.reset();reconstructionActive_=false;}
            if(level_.s3528OffInputStage)reconstructed=opampSwing(pin8*s3528BoardInputGain,level_.s3528Swing);
        }
        const double dc=coupling_.process({reconstructed,true}).value;
        const double vcaInput=(reconstructed-dc)*(470.0/10470);
        const double out=opampSwing(feedback_.process(m.vca.cell(vcaInput)),level_.s3528Swing)*c[P::OutputGain]*level_.hostReference;
        return reconstructionEnabled?out*level_.s3528OnMatch:out;
    }
    LevelModel level_=LevelModel::hardware();
    std::unique_ptr<Ssm2Chain> ssm2_;
    double resistance()const {using P=FilterParam;return 1000/(1/calibration_[P::FcRin]+1/calibration_[P::FcRbias]+1/calibration_[P::FcRshunt]);}
    double fcTarget(double fcv)const {using P=FilterParam;const auto& c=calibration_;return c[P::FcOverride]?c[P::FcManual]*1e-3:(fcv/(c[P::FcRin]*1000)+c[P::FcSupply]/(c[P::FcRbias]*1000))*resistance();}
    static bool valid(KnownSignal v) noexcept {return v.known&&std::isfinite(v.value)&&v.value>=0&&v.value<=5;}
    std::unique_ptr<EiiiCustomFilter> eiii_;ssm2045::Chip chip_;LegacyQFilter fast_;LegacyQLinearVca legacyVca_;bool legacyQ_=false;
    KnownLowpass reference_,coupling_;
    Channel1Feedback feedback_;
    S3528Runtime reconstruction_;bool reconstructionActive_=false;
    VcaBoardControl vcaControl_;
    static constexpr double fcResistance=1/(1.0/22600+1.0/124000+1.0/1000);
    FilterCalibration calibration_;
    unsigned rate_;double fcAlpha_,qAlpha_=1,fcPin_=0,qPin_=0,thermal_=1;bool known_=false;
};

// 96/192k nonlinear solver and127-tap anti-alias decimation. The reference
// path holds48k observations; native packets integrate DAC/deglitch between
// exact hardware events. VCF/CV controls remain on the selected solver grid.
class PlaybackVoiceBank {
public:
    struct Input {
        PlaybackInputs playback;
        std::array<KnownSignal,8> frequency{},resonance{};
        bool channel0Playback=false;
        std::array<KnownByte,8> reconstructionCode{};
        bool reconstructionEnabled=false;
        NativeFrame native{};
    };
    explicit PlaybackVoiceBank(unsigned factor=2,bool legacyQ=false,bool eiii=false,bool ssm2=false):voices_{
        NonlinearPlaybackVoice(48000*factor,legacyQ,eiii,ssm2),NonlinearPlaybackVoice(48000*factor,legacyQ,eiii,ssm2),
        NonlinearPlaybackVoice(48000*factor,legacyQ,eiii,ssm2),NonlinearPlaybackVoice(48000*factor,legacyQ,eiii,ssm2),
        NonlinearPlaybackVoice(48000*factor,legacyQ,eiii,ssm2),NonlinearPlaybackVoice(48000*factor,legacyQ,eiii,ssm2),
        NonlinearPlaybackVoice(48000*factor,legacyQ,eiii,ssm2),NonlinearPlaybackVoice(48000*factor,legacyQ,eiii,ssm2)},factor_(factor) {
        if(factor!=2&&factor!=4)throw std::invalid_argument("Chip oversampling requires2or4");
        mix_.configure(48000.0*factor);
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
    // ESTIMATED analog drift, off by default (then every voice scale is exactly 1).
    // Stepped every 480 output frames = ThermalDrift::updateSeconds.
    void analogDrift(bool on){if(on==drift_)return;drift_=on;driftCount_=0;if(!on)for(auto& v:voices_)v.thermalScale(1);}
    const std::array<double,8>& driftKelvin()const noexcept{return thermal_.kelvin();}
    float process(const Input& in) {
        if(drift_&&driftCount_--==0){driftCount_=479;std::array<double,8> s;thermal_.step(voices_[0].dieCelsius(),s);for(unsigned ch=0;ch<8;++ch)voices_[ch].thermalScale(s[ch]);}
        const bool native=in.native.enabled;
        if(native!=nativeLast_||(native&&(nativeExpected_!=in.native.start||nativeInvalid_||!in.native.valid))){reset();nativeLast_=native;}
        nativeInvalid_=native&&!in.native.valid;
        NativePins pins=in.native.initial;unsigned event=0;std::array<unsigned,8> advanced{};
        std::array<double,8> areas{};std::array<bool,8> known{};known.fill(in.native.valid);
        auto cv=[&](unsigned bank,unsigned ch)->KnownSignal{const auto x=pins.cv[bank][ch];return {5.0*x.value/256,x.known==255};};
        auto advance=[&](unsigned ch,unsigned time){
            if(ch==0&&!pins.channel0){nativeDac_[ch].reset();advanced[ch]=time;return;}
            auto acv=cv(0,ch);if(nativeCalibration_[FilterParam::AcvOverride])acv={nativeCalibration_[FilterParam::AcvManual],true};
            auto y=nativeDac_[ch].advance(time-advanced[ch],pins.dac[ch],acv,pins.phases[ch],nativeCoefficients_,voices_[ch].levelModel().swing);
            // Channel0's physical IINT is already computed by the ADC feedback
            // loop. Monitor this node, independent of masked playback DAC writes.
            if(ch==0&&pins.monitorSampling)y={pins.monitorIint.value*(time-advanced[ch]),pins.monitorIint.known};
            areas[ch]+=y.value;known[ch]=known[ch]&&y.known;advanced[ch]=time;
        };
        for(unsigned sub=0;sub<factor_;++sub) {
            if(native){const unsigned end=(sub+1)*(nativeFrameTime/factor_);
                while(event<in.native.count&&in.native.events[event].offset<=end){const auto& e=in.native.events[event++];advance(e.channel&7,e.offset);applyNativeEvent(pins,e);}
                for(unsigned ch=0;ch<8;++ch)advance(ch,end);
            }
            double sum=0;std::array<double,8> voice{};
            for(unsigned ch=0;ch<8;++ch) {
                if(ch==0&&!(native?pins.channel0:in.channel0Playback))continue;
                if(native)sum+=voice[ch]=voices_[ch].process(pins.dac[ch],cv(0,ch),cv(1,ch),cv(2,ch),pins.sw[ch],in.reconstructionEnabled,{areas[ch]/(nativeFrameTime/factor_),known[ch]},true);
                else sum+=voice[ch]=voices_[ch].process(in.playback.codes[ch],in.playback.amplitudeCv[ch],in.frequency[ch],in.resonance[ch],in.reconstructionCode[ch],in.reconstructionEnabled);
            }
            if(native){areas={};known.fill(in.native.valid);}
            // 7-52 mix amplifier IC96 (sum of CnOUT.V through R164..R171, RT5/C148).
            history_[cursor_]=level_.mixStage?mix_.process(sum,mc34004SwingVolts):sum;
            if(channels_)for(unsigned ch=0;ch<8;++ch)channelHistory_[ch][cursor_]=voice[ch]; // J1..J8: CnOUT.V via R156..R163
            cursor_=(cursor_+1)%taps;
        }
        if(native)nativeExpected_=in.native.start+nativeFrameTime;
        const double out=orderedFir127(coefficients_,history_,cursor_);
        mixGain_+=(mixTarget_-mixGain_)*.001;
        const double ac=hostDc_.process(out*mixGain_);
        nativeGain_+=(nativeGainTarget_-nativeGain_)*.001;
        const double monitored=ac*(native?nativeGain_:1); // Post-chain monitor gain: never changes VCF/VCA drive.
        if(channels_)for(unsigned ch=0;ch<8;++ch)channelOut_[ch]=float(channelDc_[ch].process(orderedFir127(coefficients_,channelHistory_[ch],cursor_)*mixGain_)*(native?nativeGain_:1));
        clipped_+=std::abs(monitored)>1;
        // Hardware level model: MC34004 rails bound the mix; the host gets float
        // headroom (level_model.h). The ce2e306 model keeps its +/-1 clamp.
        return float(level_.mixStage?monitored:std::clamp(monitored,-1.0,1.0));
    }
    void reset(){for(auto& v:voices_)v.reset();for(auto& d:nativeDac_)d.reset();history_.fill(0);for(auto& h:channelHistory_)h.fill(0);for(auto& d:channelDc_)d.reset();channelOut_.fill(0);mix_.reset();cursor_=0;hostDc_.reset();nativeExpected_=0;}
    void levelModel(const LevelModel& l)noexcept{level_=l;for(auto& v:voices_)v.levelModel(l);}
    // Individual outputs J1..J8 (7-52): off by default, then no extra work.
    void channelOutputs(bool on)noexcept{if(on&&!channels_){for(auto& h:channelHistory_)h.fill(0);for(auto& d:channelDc_)d.reset();}channels_=on;if(!on)channelOut_.fill(0);}
    const std::array<float,8>& channelOutput()const noexcept{return channelOut_;}
    double outputDc()const noexcept{return hostDc_.estimate();}
    std::uint64_t clippedFrames()const noexcept{return clipped_;}
private:
    host::DcBlock hostDc_;std::uint64_t clipped_=0;
    static constexpr unsigned taps=127;
    std::array<NonlinearPlaybackVoice,8> voices_;
    std::array<NativeDacChannel,8> nativeDac_{};NativeDacCoefficients nativeCoefficients_;FilterCalibration nativeCalibration_;
    bool nativeLast_=false,nativeInvalid_=false;std::uint64_t nativeExpected_=0;
    std::array<double,taps> coefficients_{},history_{};unsigned cursor_=0,factor_=2;double mixGain_=1,mixTarget_=1,nativeGain_=1,nativeGainTarget_=1;
    LevelModel level_=LevelModel::hardware();MixAmplifier mix_{96000};
    ThermalDrift thermal_;bool drift_=false;unsigned driftCount_=0;
    bool channels_=false;std::array<std::array<double,taps>,8> channelHistory_{};std::array<host::DcBlock,8> channelDc_;std::array<float,8> channelOut_{};
};
using NonlinearVoiceBank=PlaybackVoiceBank; // retained source API; new bank has monitor-node transport

}
