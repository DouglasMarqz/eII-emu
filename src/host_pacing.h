#pragma once
#include <chrono>
#include <cstdint>
namespace eii::host {
// Wall-clock scheduling only. Never changes, skips or synthesizes CPU ticks.
class HostPacer {
public:
 using Clock=std::chrono::steady_clock;
 void start(Clock::time_point now,std::uint64_t tick){epoch_=now;tick_=tick;rebases_=0;debt_={};}
 Clock::time_point deadline(std::uint64_t tick)const {
  return epoch_+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(double(tick-tick_)/4000000.0));
 }
 double latenessMs(Clock::time_point now,std::uint64_t tick)const {return std::chrono::duration<double,std::milli>(now-deadline(tick)).count();}
 bool boundDebt(Clock::time_point now,std::uint64_t tick){
  const auto late=now-deadline(tick);
  if(late<=std::chrono::milliseconds(20))return false;
  // An old audio deadline cannot be replayed through a22ms FIFO. Discard
  // wall-time debt rather than pumping future quanta into that bounded FIFO.
  epoch_+=late;debt_+=late;++rebases_;return true;
 }
 std::uint64_t rebases()const{return rebases_;}
 double debtMs()const{return std::chrono::duration<double,std::milli>(debt_).count();}
private:
 Clock::time_point epoch_{};std::uint64_t tick_=0,rebases_=0;Clock::duration debt_{};
};
}
