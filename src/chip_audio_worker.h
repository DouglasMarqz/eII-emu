#pragma once
#include "nonlinear_voice.h"
#include "host_audio_float.h"
#include "host_audio_schedule.h"
#include "host_thread_cpu.h"
#include "host_cpu_placement.h"
#include <condition_variable>
#include <thread>
#include <functional>
#include <mutex>
#include <span>
#include <chrono>
#include <atomic>

namespace eii::analog {
// Only immutable48kHz pin snapshots cross this boundary. No Machine pointer,
// no firmware work, and no wait-for-DSP in the producer or device callback.
class ChipAudioWorker {
public:
    using Input=NonlinearVoiceBank::Input;
    using Sink=std::function<void(std::span<const float>)>;
    // Individual outputs J1..J8 per frame (7-52). Called before Sink for the
    // same frames, always (zeros while disabled) so consumers stay aligned.
    using ChannelFrame=std::array<float,8>;
    using ChannelSink=std::function<void(std::span<const ChannelFrame>)>;
    static constexpr unsigned blockFrames=96,queueBlocks=8;
    explicit ChipAudioWorker(Sink sink,unsigned factor=2,bool legacyQ=false,bool eiii=false,bool ssm2=false,ChannelSink channelSink={}):sink_(std::move(sink)),channelSink_(std::move(channelSink)),factor_(checkedFactor(factor)),legacyQ_(legacyQ),eiii_(eiii),ssm2_(ssm2),thread_([this]{loop();}){}
    void channelOutputs(bool on)noexcept{channels_.store(on,std::memory_order_relaxed);}
    void analogDrift(bool on)noexcept{drift_.store(on,std::memory_order_relaxed);}
    ~ChipAudioWorker() {{std::lock_guard lock(mutex_);quit_=true;}wake_.notify_one();thread_.join();}
    ChipAudioWorker(const ChipAudioWorker&)=delete;
    void push(const Input& frame){pending_.frames[pending_.used++]=frame;if(pending_.used==blockFrames)flush();}
    void flush() {
        if(!pending_.used)return;
        {std::lock_guard lock(mutex_);
            if(size_==queueBlocks){dropped_+=queue_[head_].used;head_=(head_+1)%queueBlocks;--size_;gap_=true;}
            pending_.enqueued=std::chrono::steady_clock::now();pending_.calibration=calibration_;pending_.revision=calibrationRevision_;copyBlock(queue_[(head_+size_)%queueBlocks],pending_);++size_;pending_.used=0;
        }wake_.notify_one();
    }
    void calibrate(const FilterCalibration& c){c.validate();flush();calibration_=c;++calibrationRevision_;}
    struct Stats{unsigned queuedFrames=0,queuedBlocks=0;std::uint64_t rendered=0,dropped=0,failures=0,iterations=0,solverSamples=0,fallbacks=0,recoveries=0,held=0,clippedFrames=0,calibrationRevision=0;double load=0,blockMs=0,waitMs=0,maxWaitMs=0,outputDc=0;bool multimedia=false,highQos=false;};
    Stats stats()const {
        std::lock_guard lock(mutex_);Stats s=stats_;s.queuedBlocks=size_;s.rendered=rendered_;s.dropped=dropped_;s.failures=failures_;
        for(unsigned n=0;n<size_;++n)s.queuedFrames+=queue_[(head_+n)%queueBlocks].used;
        return s;
    }
private:
    static unsigned checkedFactor(unsigned n){if(n!=2&&n!=4)throw std::invalid_argument("DSP factor requires2or4");return n;}
    struct Block{std::array<Input,blockFrames> frames{};unsigned used=0;FilterCalibration calibration;std::uint64_t revision=0;std::chrono::steady_clock::time_point enqueued;};
    // Only [0, used) is consumed. Keep metadata on every transfer; unused tail
    // snapshots need not cross the queue mutex on partial host/event flushes.
    static void copyBlock(Block& to,const Block& from) {
        std::copy_n(from.frames.begin(),from.used,to.frames.begin());
        to.used=from.used;to.calibration=from.calibration;to.revision=from.revision;to.enqueued=from.enqueued;
    }
    void loop() {
        host::nameCurrentThread(L"EII SSM2045");host::placeCurrentThread(host::CpuRole::Dsp);
        const host::AudioFloatScope floatScope;const host::AudioThreadSchedule scheduling;
        NonlinearVoiceBank bank(factor_,legacyQ_,eiii_,ssm2_);std::uint64_t revision=~std::uint64_t(0);float last=0,recoveryStart=0;unsigned recovery=0;
        for(;;){
            Block block;bool gap;
            {std::unique_lock lock(mutex_);wake_.wait(lock,[this]{return quit_||size_;});
                if(quit_)return;
                copyBlock(block,queue_[head_]);head_=(head_+1)%queueBlocks;--size_;gap=gap_;gap_=false;
            }
            if(block.revision!=revision){bank.calibrate(block.calibration);revision=block.revision;}
            // Preserve nonlinear state across a dropped input interval. Resetting
            // all oscillators here repeatedly turned overload into hard transients.
            if(gap){recovery=64;recoveryStart=last;}
            const auto begin=std::chrono::steady_clock::now();
            std::array<float,blockFrames> out{};std::array<ChannelFrame,blockFrames> channelOut{};bool failed=false;
            const bool channels=channels_.load(std::memory_order_relaxed);bank.channelOutputs(channels);bank.analogDrift(drift_.load(std::memory_order_relaxed));
            try{for(unsigned n=0;n<block.used;++n){out[n]=bank.process(block.frames[n]);if(channels)channelOut[n]=bank.channelOutput();}}
            catch(...){failed=true;bank.reset();out.fill(0);channelOut.fill({});}
            for(unsigned n=0;n<block.used;++n){if(recovery){const float w=float(65-recovery)/64;out[n]=recoveryStart*(1-w)+out[n]*w;--recovery;}last=out[n];}
            const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
            {std::lock_guard lock(mutex_);rendered_+=block.used;failures_+=failed;
                auto ss=bank.solverStats();stats_.iterations=ss.iterations;stats_.solverSamples=ss.samples;stats_.fallbacks=ss.fallbacks;stats_.recoveries=ss.recoveries;stats_.held=ss.held;stats_.calibrationRevision=revision;stats_.outputDc=bank.outputDc();stats_.clippedFrames=bank.clippedFrames();
                stats_.waitMs=std::chrono::duration<double,std::milli>(begin-block.enqueued).count();stats_.maxWaitMs=std::max(stats_.maxWaitMs,stats_.waitMs);stats_.multimedia=scheduling.multimedia();stats_.highQos=scheduling.highQos();
                stats_.load=.95*stats_.load+.05*(elapsed*48000/block.used);stats_.blockMs=elapsed*1000;}
            if(channelSink_)channelSink_(std::span(channelOut.data(),block.used));
            if(sink_)sink_(std::span(out.data(),block.used));
        }
    }
    Sink sink_;ChannelSink channelSink_;std::atomic<bool> channels_{false},drift_{false};Block pending_;FilterCalibration calibration_;std::uint64_t calibrationRevision_=0;Stats stats_;
    mutable std::mutex mutex_;std::condition_variable wake_;
    std::array<Block,queueBlocks> queue_{};unsigned head_=0,size_=0;
    bool quit_=false,gap_=false;std::uint64_t rendered_=0,dropped_=0,failures_=0;
    unsigned factor_;bool legacyQ_=false,eiii_=false,ssm2_=false;
    std::thread thread_;
};
}
