#pragma once
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace eii::host {
// Windows libc++ truncates timed CV waits to whole milliseconds. Its predicate
// retry loop consequently polls for the final fractional millisecond. A native
// auto-reset event retains notifications and rounds positive waits upward.
inline std::uint32_t deadlineWaitMilliseconds(std::chrono::nanoseconds remaining) noexcept {
    if(remaining<=std::chrono::nanoseconds::zero())return 0;
    const auto ms=std::chrono::ceil<std::chrono::milliseconds>(remaining).count();
    return static_cast<std::uint32_t>(std::min<std::int64_t>(ms,0xfffffffeLL));
}
class CommandWake {
public:
    struct Stats {std::uint64_t waits=0,timeouts=0;std::uint32_t lastMilliseconds=0;bool nativeEvent=false;};
    CommandWake() {
#ifdef _WIN32
        event_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!event_)throw std::system_error(int(GetLastError()),std::system_category(),"Create command wake event");
#endif
    }
    ~CommandWake(){
#ifdef _WIN32
        CloseHandle(event_);
#endif
    }
    CommandWake(const CommandWake&)=delete;CommandWake&operator=(const CommandWake&)=delete;
    void notify_one() noexcept {
#ifdef _WIN32
        SetEvent(event_);
#else
        cv_.notify_one();
#endif
    }
    template<class Predicate>void wait(std::unique_lock<std::mutex>& lock,Predicate ready) {
#ifdef _WIN32
        while(!ready())waitNative(lock,INFINITE);
#else
        cv_.wait(lock,ready);
#endif
    }
    template<class Clock,class Duration,class Predicate>
    bool wait_until(std::unique_lock<std::mutex>& lock,std::chrono::time_point<Clock,Duration> deadline,Predicate ready) {
#ifdef _WIN32
        while(!ready()) {
            const auto now=Clock::now();if(now>=deadline)return ready();
            const auto remaining=std::chrono::ceil<std::chrono::nanoseconds>(deadline-now);
            waitNative(lock,deadlineWaitMilliseconds(remaining));
        }
        return true;
#else
        return cv_.wait_until(lock,deadline,ready);
#endif
    }
    Stats stats()const noexcept{return stats_;} // owner-thread observation only
private:
    Stats stats_;
#ifdef _WIN32
    HANDLE event_=nullptr;
    void waitNative(std::unique_lock<std::mutex>& lock,DWORD milliseconds) {
        ++stats_.waits;stats_.lastMilliseconds=milliseconds;stats_.nativeEvent=true;
        lock.unlock();const auto result=WaitForSingleObject(event_,milliseconds);
        const auto error=result==WAIT_FAILED?GetLastError():ERROR_SUCCESS;lock.lock();
        if(result==WAIT_FAILED)throw std::system_error(int(error),std::system_category(),"Wait for command event");
        if(result==WAIT_TIMEOUT)++stats_.timeouts;
    }
#else
    std::condition_variable cv_;
#endif
};
}
