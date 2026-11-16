#pragma once
// GUI-thread cost counters for the slider investigation (owner trace reads,
// GUI thread writes). Relaxed atomics only; no locks on either side.
#include <atomic>
#include <chrono>
#include <cstdint>
namespace eii::gui {
struct Stats {
    std::atomic<std::uint64_t> moves{0},voltages{0},paints{0},paintUs{0},paintMaxUs{0},moveUs{0},moveMaxUs{0};
};
inline Stats stats;
inline std::uint64_t nowUs()noexcept{return std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
inline void storeMax(std::atomic<std::uint64_t>& a,std::uint64_t v)noexcept{auto c=a.load(std::memory_order_relaxed);while(v>c&&!a.compare_exchange_weak(c,v,std::memory_order_relaxed)){}}
}
