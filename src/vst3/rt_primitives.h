#pragma once
// Real-time transport primitives for the VST3 plug-in. Header-only, no Machine
// access, no allocation after construction. The host audio thread only uses
// atomics, these bounded SPSC containers and Signal::set().
#include "runtime_audio.h"
#include <atomic>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <chrono>
#include <condition_variable>
#include <mutex>
#endif

namespace eii::vst {
// Auto-reset event. set() never takes a user-space lock on Windows (SetEvent),
// so the audio thread may wake the machine owner without priority inversion.
class Signal {
public:
#ifdef _WIN32
    Signal():event_(CreateEventW(nullptr,FALSE,FALSE,nullptr)){
        if(!event_)throw std::runtime_error("CreateEvent failed");
        timer_=CreateWaitableTimerExW(nullptr,nullptr,0x2/*CREATE_WAITABLE_TIMER_HIGH_RESOLUTION*/,TIMER_MODIFY_STATE|SYNCHRONIZE);
    }
    ~Signal(){if(timer_)CloseHandle(timer_);CloseHandle(event_);}
    void set()noexcept{SetEvent(event_);}
    bool wait(unsigned ms)noexcept{
        // Events remain the normal wakeup. A high-resolution one-shot bounds
        // short timeout fallbacks without depending on host focus/timeBeginPeriod.
        if(!ms||ms==INFINITE||!timer_)return WaitForSingleObject(event_,ms)==WAIT_OBJECT_0;
        if(WaitForSingleObject(event_,0)==WAIT_OBJECT_0)return true;
        LARGE_INTEGER due;due.QuadPart=-LONGLONG(ms)*10000;
        if(!SetWaitableTimer(timer_,&due,0,nullptr,nullptr,FALSE))return WaitForSingleObject(event_,ms)==WAIT_OBJECT_0;
        const HANDLE handles[]{event_,timer_};const auto result=WaitForMultipleObjects(2,handles,FALSE,INFINITE);
        CancelWaitableTimer(timer_);return result==WAIT_OBJECT_0;
    }
    bool highResolution()const noexcept{return timer_!=nullptr;}
private:
    HANDLE event_;
    HANDLE timer_=nullptr;
#else
    Signal()=default;
    bool highResolution()const noexcept{return false;}
    void set()noexcept{{std::lock_guard l(m_);flag_=true;}cv_.notify_one();}
    bool wait(unsigned ms){std::unique_lock l(m_);const bool ok=cv_.wait_for(l,std::chrono::milliseconds(ms),[&]{return flag_;});flag_=false;return ok;}
private:
    std::mutex m_;std::condition_variable cv_;bool flag_=false;
#endif
public:
    Signal(const Signal&)=delete;Signal& operator=(const Signal&)=delete;
};

// Reserve queue slots for a complete Machine::run, including a pre-existing
// partial DSP block and the final flush. Count blocks, not frames: MIDI event
// boundaries can leave many tiny blocks in the queue. Only the owner produces;
// the DSP consumer can only free slots while the owner checks/runs.
inline bool dspRunFits(unsigned queuedBlocks,std::uint64_t ticks)noexcept{
    constexpr std::uint64_t blockFrames=96,queueBlocks=8;
    const auto frames=(ticks*3+249)/250+1; // 48 kHz phase rounding
    const auto blocks=(frames+blockFrames-1)/blockFrames+1; // pending block
    return queuedBlocks<=queueBlocks&&blocks<=queueBlocks-queuedBlocks;
}

// Single-producer/single-consumer bounded queue (power-of-two capacity).
template<class T,std::size_t N> class SpscQueue {
    static_assert((N&(N-1))==0,"capacity must be a power of two");
public:
    bool push(const T& v)noexcept{
        const auto w=w_.load(std::memory_order_relaxed);
        if(w-r_.load(std::memory_order_acquire)>=N)return false;
        data_[w&(N-1)]=v;w_.store(w+1,std::memory_order_release);return true;
    }
    const T* front()noexcept{
        const auto r=r_.load(std::memory_order_relaxed);
        if(r==w_.load(std::memory_order_acquire))return nullptr;
        return &data_[r&(N-1)];
    }
    void pop()noexcept{r_.store(r_.load(std::memory_order_relaxed)+1,std::memory_order_release);}
private:
    std::array<T,N> data_{};
    alignas(64) std::atomic<std::uint64_t> w_{0};
    alignas(64) std::atomic<std::uint64_t> r_{0};
};

// 48 kHz stereo stream addressed by absolute frame index. Exactly one producer
// at a time (the machine owner, or the SSM2045 DSP worker it owns; ownership
// changes only after that worker is drained/joined). The consumer reads by
// index: a frame not yet produced is substituted by silence (realtime) and the
// late frame is discarded on arrival, so stream position never slips.
class FrameRing {
public:
    static constexpr std::uint64_t capacity=1u<<17; // 2.7 s at 48 kHz
    FrameRing():data_(capacity){}
    void push(std::span<const host::StereoFrame> frames)noexcept{
        auto w=w_.load(std::memory_order_relaxed);const auto r=r_.load(std::memory_order_acquire);
        for(const auto& f:frames){if(w<r||w-r<capacity)data_[w&(capacity-1)]=f;else ++overflow_;++w;}
        w_.store(w,std::memory_order_release);wakeConsumer();
    }
    void pushSilence(std::uint64_t n)noexcept{
        auto w=w_.load(std::memory_order_relaxed);const auto r=r_.load(std::memory_order_acquire);
        for(std::uint64_t i=0;i<n;++i,++w)if(w<r||w-r<capacity)data_[w&(capacity-1)]={};
        w_.store(w,std::memory_order_release);wakeConsumer();
    }
    std::uint64_t written()const noexcept{return w_.load(std::memory_order_acquire);}
    // Consumer side.
    bool read(std::uint64_t index,host::StereoFrame& out)noexcept{
        const auto w=w_.load(std::memory_order_acquire);
        const bool ok=index<w&&index+capacity>w;
        out=ok?data_[index&(capacity-1)]:host::StereoFrame{};
        r_.store(index+1,std::memory_order_release);return ok;
    }
    // Offline (non-realtime) consumer may block for production.
    bool waitWritten(std::uint64_t needed,unsigned timeoutMs){
        if(written()>=needed)return true;
        waiting_.store(true,std::memory_order_seq_cst);
        bool ok=false;
        for(unsigned waited=0;;waited+=5){
            if(written()>=needed){ok=true;break;}
            if(waited>=timeoutMs)break;
            signal_.wait(5);
        }
        waiting_.store(false,std::memory_order_relaxed);return ok;
    }
    std::uint64_t overflows()const noexcept{return overflow_;}
private:
    void wakeConsumer()noexcept{if(waiting_.load(std::memory_order_seq_cst))signal_.set();}
    std::vector<host::StereoFrame> data_;
    alignas(64) std::atomic<std::uint64_t> w_{0};
    alignas(64) std::atomic<std::uint64_t> r_{0};
    std::atomic<bool> waiting_{false};
    std::uint64_t overflow_=0;
    Signal signal_;
};
// Individual outputs J1..J8 (sheet 7-52), indexed like FrameRing. Written by the
// same single producer just before the main frames; alignTo() pads with silence
// when the main ring advanced without channel data (silence, pauses).
class ChannelRing {
public:
    using Frame=std::array<float,8>;
    static constexpr std::uint64_t capacity=FrameRing::capacity;
    ChannelRing():data_(capacity){}
    void alignTo(std::uint64_t index)noexcept{auto w=w_.load(std::memory_order_relaxed);for(;w<index;++w)data_[w&(capacity-1)]={};w_.store(w,std::memory_order_release);}
    void push(std::span<const Frame> frames)noexcept{auto w=w_.load(std::memory_order_relaxed);for(const auto& f:frames)data_[(w++)&(capacity-1)]=f;w_.store(w,std::memory_order_release);}
    bool read(std::uint64_t index,Frame& out)const noexcept{const auto w=w_.load(std::memory_order_acquire);const bool ok=index<w&&index+capacity>w;out=ok?data_[index&(capacity-1)]:Frame{};return ok;}
private:
    std::vector<Frame> data_;
    alignas(64) std::atomic<std::uint64_t> w_{0};
};

}
