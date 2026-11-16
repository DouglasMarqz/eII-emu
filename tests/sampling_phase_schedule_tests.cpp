// Sampling observer must see ISMPL independently of unrelated timer events or
// Machine::run boundaries. Real timer/divider versus fully materialized oracle.
#include "channel_timers.h"
#include <cstdio>
using namespace eii;
int main(){
 unsigned cases=0,totalMismatches=0,totalPulses=0;
 for(unsigned seed:{1u,123u,98765u})for(unsigned period:{128u,360u,361u,480u})for(bool incidental:{false,true}){
  ChannelTimers cached,oracle;
  for(auto* p:{&cached,&oracle}){p->initializeOutputPowerOn(seed);p->enableEventTiming();p->write(0x1f,0x34);p->write(0x1c,period&255);p->write(0x1c,period>>8);}
  unsigned mismatches=0,pulses=0;std::uint64_t at=0,events=~0ull;
  for(unsigned tick=1;tick<=14400;++tick){
   cached.advanceCpuTick();oracle.advanceCpuTick();
   // Unrelated observations must not be necessary to discover sampling pulses.
   if(incidental&&tick%37==0)cached.flushEventTiming();
   if(tick%6)continue;
   if(cached.runtimeEvents!=events||cached.sourceClocks>=at){
    cached.flushEventTiming();const auto d=cached.channels[0].clocksUntilSamplingEvent();
    at=d==UINT64_MAX?d:cached.sourceClocks+d;events=cached.runtimeEvents;
   }
   oracle.flushEventTiming();const auto& a=cached.channels[0];const auto& b=oracle.channels[0];
   if(a.samplingPulse()!=b.samplingPulse()||a.phi1()!=b.phi1()||a.phi2()!=b.phi2())++mismatches;
   pulses+=b.samplingPulse().known&&b.samplingPulse().high;
  }
  if(mismatches||!pulses)std::printf("FAIL seed%u period%u incidental%d mismatches%u pulses%u\n",seed,period,incidental,mismatches,pulses);
  totalMismatches+=mismatches;totalPulses+=pulses;++cases;
 }
 std::printf("sampling phase schedule: %u cases, %u mismatches, %u observed-high samples\n",cases,totalMismatches,totalPulses);
 return totalMismatches||!totalPulses;
}
