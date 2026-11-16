#pragma once
#include "am6072.h"
#include "playback_analog.h"
#include "host_audio_float.h"
#include "chip_audio_worker.h"
#include "level_model.h"
#include <algorithm>
#include <atomic>
#include <array>
#include <functional>
#include <span>
#include <mutex>
#include <memory>
#include <vector>

namespace eii::host {
struct StereoFrame { float left=0,right=0; };
// Bounded transport: emulation never waits for an audio device. Consumer
// underruns are zero filled; overflow discards the oldest queued frames.
class AudioQueue {
public:
    static constexpr unsigned capacity=8192;
    static constexpr unsigned realtimeLimit=960; //20ms at48kHz, excluding device
    explicit AudioQueue(unsigned limit=realtimeLimit):limit_(limit) {
        if(!limit || limit>capacity)throw std::invalid_argument("Invalid audio queue limit");
    }
    void push(std::span<const StereoFrame> frames) {
        std::lock_guard lock(mutex_);
        for(auto f:frames) {
            if(size_==limit_) {head_=(head_+1)%capacity;--size_;++dropped_;}
            data_[(head_+size_)%capacity]=f;++size_;
            highWater_=std::max(highWater_,size_);
        }
    }
    void setOutputGain(float value) {if(!std::isfinite(value)||value<0||value>1)throw std::invalid_argument("Output gain requires0..1");gain_.store(value,std::memory_order_relaxed);}
    float outputGain()const noexcept{return gain_.load(std::memory_order_relaxed);}
    void pop(std::span<StereoFrame> frames) {
        std::lock_guard lock(mutex_);
        for(auto& f:frames) {
            if(size_) {f=data_[head_];head_=(head_+1)%capacity;--size_;}
            else {f={};++missing_;}
        }
        const auto gain=gain_.load(std::memory_order_relaxed);
        if(gain!=1)for(auto& f:frames){f.left*=gain;f.right*=gain;}
    }
    void setLimit(unsigned limit) {
        if(!limit || limit>capacity)throw std::invalid_argument("Invalid audio queue limit");
        std::lock_guard lock(mutex_);limit_=limit;
        if(size_>limit_) {const auto n=size_-limit_;head_=(head_+n)%capacity;size_=limit_;dropped_+=n;}
        highWater_=size_;
    }
    void clear() {std::lock_guard lock(mutex_);head_=size_=highWater_=0;}
    struct Stats {std::uint64_t dropped=0,missing=0;unsigned queued=0,limit=0,highWater=0;};
    Stats stats() const {std::lock_guard lock(mutex_);return {dropped_,missing_,size_,limit_,highWater_};}
private:
    std::atomic<float> gain_{1};
    mutable std::mutex mutex_;
    std::array<StereoFrame,capacity> data_{};
    unsigned head_=0,size_=0,highWater_=0;
    unsigned limit_;
    std::uint64_t dropped_=0,missing_=0;
};
}
namespace eii::analog {
enum class RuntimeAudioMode { DacMonitor, BasicVoice, NonlinearChip, NonlinearChip192, LegacyQ, EiiiCustom, Ssm2Chip, Ssm2Chip192 };
inline bool ssm2AudioMode(RuntimeAudioMode mode)noexcept{return mode==RuntimeAudioMode::Ssm2Chip||mode==RuntimeAudioMode::Ssm2Chip192;}
inline bool nonlinearAudioMode(RuntimeAudioMode mode)noexcept{return mode==RuntimeAudioMode::NonlinearChip||mode==RuntimeAudioMode::NonlinearChip192;}
inline bool workerAudioMode(RuntimeAudioMode mode)noexcept{return nonlinearAudioMode(mode)||mode==RuntimeAudioMode::LegacyQ||mode==RuntimeAudioMode::EiiiCustom||ssm2AudioMode(mode);}
// Provisional host-rate voice: existing zero-Q SSM2045 filter + linear VCA
// kernels, actual DAC/ACV/FCV inputs. Board CV calibration is still pending.
class BasicPlaybackVoice {
public:
    explicit BasicPlaybackVoice(double rate):filter_(rate),feedback_(rate),
        reference_(rate,2350e-6),coupling_(rate,10470e-6) {
        vca_.setControlCurrent(500e-6);reset();
    }
    // 7-45: R122=22.6k from FCV, R130=124k to-15V, R112=1k
    // to ground. Ideal static pin voltage; bias current/RC settling omitted.
    static constexpr double frequencyPinVolts(double fcv) noexcept {
        return (fcv/22600.0-15.0/124000.0)/(1.0/22600.0+1.0/124000.0+1.0/1000.0);
    }
    static double nominalPoleHz(double fcv) {
        // Keep the old provisional midpoint anchor, not its reversed slope.
        // Manufacturer typical pin5 sensitivity is -19.3mV/octave.
        constexpr double anchorHz=30.0*22.62741699796952; // FCV2.5V
        return anchorHz*Ssm2045OpenLoop::gmRatioForPin5Delta(
            frequencyPinVolts(fcv)-frequencyPinVolts(2.5));
    }
    // The active deglitch stage is bypassed dynamically, not at unity gain:
    // its existing switched kernel converges to -R34/R26 at DC (7-45).
    static constexpr double nominalDeglitchGain=-10000.0/200.0;
    double process(KnownByte code,KnownSignal acv,KnownSignal fcv,KnownByte reconstructionCode={},bool reconstructionEnabled=false) {
        if(code.known!=255 || !acv.known || !fcv.known) {reset();return 0;}
        if(!std::isfinite(acv.value)||acv.value<0||acv.value>5 ||
           !std::isfinite(fcv.value)||fcv.value<0||fcv.value>5)
            throw std::invalid_argument("Invalid basic voice CV");
        if(fcv.value!=lastFcv_) {
            constexpr double pi=3.14159265358979323846;
            // Provisional absolute anchor; f0 is the first section's pole,
            // not a calibrated EII -3dB cutoff. Cache until firmware changes CV.
            filter_.setTransconductance(2*pi*nominalPoleHz(fcv.value)*std::sqrt(20e-9*10*3.9e-9));
            lastFcv_=fcv.value;
        }
        const auto cv=reference_.process(acv);
        const double dac=-2490*Am6072::signedDecodeCurrent(code.value,cv.value/9400);
        // Fast route: nominal deglitch gain matched to the integrating hardware
        // route at 1 kHz, IC73 TL084 swing, VCF output at pin-8 volts (level_model.h).
        const double y=opampSwing(dac*nominalDeglitchGain*level_.fastRouteGain,level_.swing);
        const double filtered=filter_.process(y*(900.0/68900.0)).fourPole*level_.pinScale;
        double reconstructed=filtered;
        if(reconstructionEnabled){
            reconstructionActive_=true;
            // The provisional Legacy VCF runs at48k. Hold its output for two
            //96k reconstruction steps: no cutoff clamping at the host Nyquist.
            const double stage=opampSwing(filtered*s3528BoardInputGain,level_.s3528Swing);
            const auto a=reconstruction_.process({stage,true},reconstructionCode);
            const auto b=reconstruction_.process({stage,true},reconstructionCode);
            if(!a.known||!b.known){coupling_.resetKnownZero();feedback_.reset();return 0;}
            reconstructed=(a.value+b.value)*.5;
        }else{
            if(reconstructionActive_){reconstruction_.reset();reconstructionActive_=false;}
            if(level_.s3528OffInputStage)reconstructed=opampSwing(filtered*s3528BoardInputGain,level_.s3528Swing);
        }
        const double dc=coupling_.process({reconstructed,true}).value;
        const double out=opampSwing(feedback_.process(vca_.outputCurrent((reconstructed-dc)*(470.0/10470.0))),level_.s3528Swing)*level_.hostReference;
        return reconstructionEnabled?out*level_.s3528OnMatch:out;
    }
    void levelModel(const LevelModel& l)noexcept{level_=l;} // tests: LevelModel::ce2e306()
    bool mixStage()const noexcept{return level_.mixStage;}
    void reset() noexcept {
        filter_.reset();feedback_.reset();reference_.resetKnownZero();coupling_.resetKnownZero();reconstruction_.reset();reconstructionActive_=false;
    }
private:
    Ssm2045OpenLoop filter_;
    Ssm2045LinearVca vca_;
    Channel1Feedback feedback_;
    KnownLowpass reference_,coupling_;
    S3528Runtime reconstruction_{96000};bool reconstructionActive_=false;
    double lastFcv_=-1;LevelModel level_=LevelModel::hardware();
};
// No source-clock analog observer or sample-memory read. Channel0 is admitted
// only with known playback MODE; unknown input samples are muted at this boundary.
class RuntimeAudioEngine {
public:
    static constexpr unsigned rate=48000,cpuRate=4000000;
    using Sink=std::function<void(std::span<const host::StereoFrame>)>;
    using ChannelSink=ChipAudioWorker::ChannelSink;
    // channelSink: individual outputs J1..J8 (7-52), called before sink for the
    // same frames (zeros while channelOutputs(false), the default).
    explicit RuntimeAudioEngine(Sink sink,RuntimeAudioMode mode=RuntimeAudioMode::BasicVoice,ChannelSink channelSink={})
        :sink_(std::move(sink)),mode_(mode),channelSink_(std::move(channelSink)) {
        if(workerAudioMode(mode_))
            chipWorker_=std::make_unique<ChipAudioWorker>([sink=sink_](auto values){
                std::array<host::StereoFrame,ChipAudioWorker::blockFrames> frames;
                for(unsigned n=0;n<values.size();++n)frames[n]={values[n],values[n]};
                if(sink)sink(std::span(frames.data(),values.size()));
            },mode_==RuntimeAudioMode::NonlinearChip192||mode_==RuntimeAudioMode::Ssm2Chip192?4:2,mode_==RuntimeAudioMode::LegacyQ,mode_==RuntimeAudioMode::EiiiCustom,ssm2AudioMode(mode_),channelSink_);
    }
    void channelOutputs(bool on)noexcept{channels_=on;if(chipWorker_)chipWorker_->channelOutputs(on);}
    void analogDrift(bool on)noexcept{if(chipWorker_)chipWorker_->analogDrift(on);} // chip models only
    void calibrate(const FilterCalibration& c){c.validate();reconstructionOverride_=c[FilterParam::S3528Override]!=0;reconstructionOverrideCode_={std::uint8_t(c[FilterParam::S3528Code]),63};if(chipWorker_)chipWorker_->calibrate(c);}
    void reconstruction(bool enabled)noexcept{reconstruction_=enabled;}
    void nativeAudio(bool enabled,std::uint64_t halfEdge){native_.reset();if(enabled&&chipWorker_)native_=std::make_unique<NativeCollector>(halfEdge);}
    bool nativeAudioEnabled()const noexcept{return bool(native_);}
#if EII_PLAYBACK_TIMING_LAB
    void nativeFrameObserver(std::function<void(const NativeFrame&)> observer){nativeFrameObserver_=std::move(observer);}
#endif
    std::uint64_t nativeOverflows()const noexcept{return native_?native_->overflows():0;}
    std::uint64_t nativeOrderingErrors()const noexcept{return native_?native_->orderingErrors():0;}
    void nativeEvent(std::uint64_t edge,NativeEvent e){if(native_){auto sink=[this](const NativeFrame& f){sendNative(f);};native_->event(edge,e,sink);}}
    void nativeAdvance(std::uint64_t edge){if(native_&&native_->due(edge)){auto sink=[this](const NativeFrame& f){sendNative(f);};native_->advance(edge,sink);}}
    bool reconstructionEnabled()const noexcept{return reconstruction_&&mode_!=RuntimeAudioMode::DacMonitor;}
    RuntimeAudioMode mode()const noexcept{return mode_;}
    ChipAudioWorker::Stats chipStats()const{return chipWorker_?chipWorker_->stats():ChipAudioWorker::Stats{};}
    bool clock() noexcept {phase_+=rate;if(phase_<cpuRate)return false;phase_-=cpuRate;return true;}
    void sample(const PlaybackInputs& input,const std::array<KnownSignal,8>& frequencyCv={},bool channel0Playback=false,const std::array<KnownSignal,8>& resonanceCv={},const std::array<KnownByte,8>& reconstructionCode={}) {
        if(chipWorker_){chipWorker_->push({input,frequencyCv,resonanceCv,channel0Playback,reconstructionCode,reconstruction_});++frames_;return;}
        const host::AudioFloatScope floatScope;
        double sum=0;ChipAudioWorker::ChannelFrame voice{};
        for(unsigned ch=0;ch<8;++ch) {
            if(ch==0 && !channel0Playback)continue;
            const auto code=input.codes[ch];const auto cv=input.amplitudeCv[ch];
            if(mode_==RuntimeAudioMode::BasicVoice) {
                const double v=voices_[ch].process(code,cv,frequencyCv[ch],reconstructionOverride_?reconstructionOverrideCode_:reconstructionCode[ch],reconstruction_);
                sum+=v;if(channels_)voice[ch]=float(v);continue;
            }
            // Fast definite-code path; uncertain masks must not become sound.
            if(code.known!=255 || !cv.known)continue;
            if(!std::isfinite(cv.value)||cv.value<0||cv.value>5)
                throw std::invalid_argument("Invalid DAC monitor amplitude CV");
            sum+=-2490.0*Am6072::signedDecodeCurrent(code.value,cv.value/9400.0);
        }
        // Raw DAC mode uses5V/channel headroom (clamped). BasicVoice: 7-52 mix
        // amplifier with MC34004 rails and float host headroom (level_model.h).
        const bool mix=mode_==RuntimeAudioMode::BasicVoice&&voices_[0].mixStage();
        const float value=mix?float(mix_.process(sum,mc34004SwingVolts)):float(std::clamp(sum/(mode_==RuntimeAudioMode::DacMonitor?40.0:1.0),-1.0,1.0));
        channelPending_[used_]=voice;pending_[used_++]={value,value};++frames_;
        if(used_==pending_.size())flush();
    }
    void flush() {if(chipWorker_){chipWorker_->flush();return;}if(used_){if(channelSink_)channelSink_(std::span(channelPending_.data(),used_));if(sink_)sink_(std::span(pending_.data(),used_));used_=0;}}
    std::uint64_t frames() const noexcept {return frames_;}
private:
    Sink sink_;
    void sendNative(const NativeFrame& f){
#if EII_PLAYBACK_TIMING_LAB
        if(nativeFrameObserver_)nativeFrameObserver_(f);
#endif
        NonlinearVoiceBank::Input in;in.native=f;in.reconstructionEnabled=reconstruction_;chipWorker_->push(in);++frames_;
    }
#if EII_PLAYBACK_TIMING_LAB
    std::function<void(const NativeFrame&)> nativeFrameObserver_;
#endif
    std::unique_ptr<NativeCollector> native_;
    RuntimeAudioMode mode_;
    std::array<BasicPlaybackVoice,8> voices_{BasicPlaybackVoice(rate),BasicPlaybackVoice(rate),
        BasicPlaybackVoice(rate),BasicPlaybackVoice(rate),BasicPlaybackVoice(rate),BasicPlaybackVoice(rate),
        BasicPlaybackVoice(rate),BasicPlaybackVoice(rate)};
    bool reconstruction_=false,reconstructionOverride_=false;KnownByte reconstructionOverrideCode_{35,63};
    unsigned phase_=0,used_=0;
    std::uint64_t frames_=0;
    std::array<host::StereoFrame,256> pending_{};std::array<ChipAudioWorker::ChannelFrame,256> channelPending_{};
    ChannelSink channelSink_;bool channels_=false;MixAmplifier mix_{48000};
    std::unique_ptr<ChipAudioWorker> chipWorker_;
};
}
