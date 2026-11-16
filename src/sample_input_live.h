#pragma once
#include "sample_input_analog.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>

namespace eii::analog {
// Live host input for +INPUT.V: a jitter buffer between the capture clock
// (WASAPI, 48 kHz mono after the capture thread) and emulated time.
//
// The producer (capture thread) pushes blocks into a lock-free SPSC ring of
// 2^21 samples (43.7 s). The consumer (machine thread) is called in emulated
// time (CPU ticks); its read position advances by exactly 48000 samples per
// emulated second and the value is a 4-point cubic (Catmull-Rom)
// interpolation, so the 7-42 model sees continuous time and the real SAR
// samples it at its own clock.
//
// While the converter samples (consumer active) the input is consumed
// sample-continuously: NOTHING is discarded, skipped or re-prefilled. The
// machine is paced by input availability (ready()): emulated time cannot run
// ahead of the captured stream. If the machine is slower than real time the
// buffer grows (multi-second headroom) and the recording stays continuous.
//
// Counters (monotonic, any thread):
//  prefills    starts: 60 ms of input is buffered before the read position
//              starts (sampling mode entered, resume after a gap, reboot).
//  underruns   EVENTS (episodes) where the consumer needed an uncaptured
//              sample while running; it outputs 0 V for the missing samples
//              and keeps its timeline (a time-aligned gap, never a splice).
//              With ready() pacing this does not happen in steady state.
//  overruns    events: the ring was full (43.7 s behind) and a captured block
//              had to be dropped.
//  discarded   samples dropped QUIETLY while not sampling: the producer keeps
//              the fill near 60 ms so sampling starts at "now". Not an overrun.
//  paceHolds   machine quanta held because the input was not yet available.
// Host +/-1.0 = +/-voltsFullScale (nominal 1.0 V peak) x GAIN pot (0..1).
class LiveSampleInput final:public SampleInputSource {
public:
    static constexpr std::uint64_t capacity=1u<<21;
    static constexpr double rate=48000;
    static constexpr std::uint64_t target=2880,slack=480;          // 60 ms, +10 ms
    std::atomic<float> gainPot{1.0f},voltsFullScale{1.0f};
    std::atomic<std::uint64_t> pushed{0},consumed{0},underruns{0},overruns{0},producerDrops{0},discarded{0},prefills{0},paceHolds{0};
    std::atomic<std::int64_t> lastPushNs{0};
    // Host clock for the 200 ms capture-stopped rule; a probe replaying a
    // simulated capture timeline substitutes its own deterministic clock.
    std::int64_t (*clockNs)()=&steadyNs;
    LiveSampleInput():ring_(std::make_unique<float[]>(capacity)){}
    // ---- producer (capture thread) ----
    void push(std::span<const float> samples) noexcept {
        lastPushNs.store(nowNs(),std::memory_order_relaxed);
        const bool inactive=!active_.load(std::memory_order_acquire);
        auto head=head_.load(std::memory_order_relaxed);
        auto tail=tail_.load(std::memory_order_acquire);
        if(inactive&&head-tail+samples.size()>target+slack){trimTo(head,tail,samples.size());tail=tail_.load(std::memory_order_acquire);}
        if(head-tail+samples.size()>capacity){producerDrops.fetch_add(samples.size(),std::memory_order_relaxed);overruns.fetch_add(1,std::memory_order_relaxed);return;}
        for(std::size_t i=0;i<samples.size();++i)ring_[(head+i)&(capacity-1)]=samples[i];
        head_.store(head+samples.size(),std::memory_order_release);
        pushed.fetch_add(samples.size(),std::memory_order_relaxed);
    }
    std::uint64_t buffered() const noexcept {return head_.load(std::memory_order_acquire)-tail_.load(std::memory_order_acquire);}
    bool active() const noexcept {return active_.load(std::memory_order_relaxed);}
    double pos() const noexcept {return pos_;} // consumer thread only (tests/diagnostics)
    // ---- consumer (machine thread) ----
    // May the machine advance `ticks` from `now` without outrunning the input?
    // Not sampling, or a capture that stopped delivering (>200 ms), never holds.
    bool ready(std::uint64_t now,std::uint64_t ticks) noexcept override {
        if(!running_)return true;
        if(nowNs()-lastPushNs.load(std::memory_order_relaxed)>200'000'000)return true;
        const auto head=head_.load(std::memory_order_acquire);
        double need;
        if(prefill_)need=double(tailSeen_+target);
        else need=pos_+(double(now+ticks)-lastTick_)*(rate/4e6)+3;
        if(double(head)>=need)return true;
        paceHolds.fetch_add(1,std::memory_order_relaxed);return false;
    }
    double volts(std::uint64_t tick,double frac=0) noexcept override {
        const double now=double(tick)+frac;
        if(!running_||now<lastTick_||now-lastTick_>200000.0)restart();   // start, >50 ms gap, reboot
        else if(!prefill_)pos_+=(now-lastTick_)*(rate/4e6);
        lastTick_=now;
        const auto head=head_.load(std::memory_order_acquire);
        if(prefill_) {
            if(head-tailSeen_<target)return 0;   // prefilling: silence
            pos_=double(head-target);prefill_=false;
        }
        const auto i=std::uint64_t(pos_);
        if(i+2>=head){if(!starved_){starved_=true;underruns.fetch_add(1,std::memory_order_relaxed);}return 0;}
        starved_=false;
        const std::uint64_t keep=i?i-1:0;
        if(keep>tailSeen_){consumed.fetch_add(keep-tailSeen_,std::memory_order_relaxed);tailSeen_=keep;tail_.store(keep,std::memory_order_release);}
        const float a=at(i?i-1:i),b=at(i),c=at(i+1),d=at(i+2);
        const double t=pos_-double(i);
        const double x=b+0.5*t*(c-a+t*(2*a-5*b+4*c-d+t*(3*(b-c)+d-a)));
        return x*double(voltsFullScale.load(std::memory_order_relaxed))*double(gainPot.load(std::memory_order_relaxed));
    }
    // Not sampling: hand the ring to the producer, which keeps it near target.
    void idle(std::uint64_t) noexcept override {
        if(running_){running_=false;active_.store(false,std::memory_order_release);}
    }
private:
    std::unique_ptr<float[]> ring_;
    std::atomic<std::uint64_t> head_{0},tail_{0};
    std::atomic<bool> active_{false};
    // consumer-only
    bool running_=false,prefill_=true,starved_=false;double pos_=0,lastTick_=0;std::uint64_t tailSeen_=0;
    std::int64_t nowNs() const noexcept {return clockNs();}
    static std::int64_t steadyNs() noexcept {return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
    float at(std::uint64_t i) const noexcept {return ring_[i&(capacity-1)];}
    // Producer while inactive: drop the oldest beyond target, quietly (CAS so
    // a consumer that is just restarting re-reads the tail).
    void trimTo(std::uint64_t head,std::uint64_t tail,std::size_t incoming) noexcept {
        const std::uint64_t keep=target;
        const std::uint64_t want=head+incoming>keep?head+incoming-keep:0;
        if(want<=tail)return;
        auto expected=tail;const auto to=std::min(want,head);
        if(tail_.compare_exchange_strong(expected,to,std::memory_order_acq_rel))discarded.fetch_add(to-tail,std::memory_order_relaxed);
    }
    void restart() noexcept {
        running_=true;active_.store(true,std::memory_order_seq_cst);
        prefills.fetch_add(1,std::memory_order_relaxed);prefill_=true;starved_=false;
        tailSeen_=tail_.load(std::memory_order_seq_cst);
    }
};
}
