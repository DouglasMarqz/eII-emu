// Diagnostic only: synthetic DAC changes and phase offsets, not real firmware.
// Compare the production settled 6-tick model with the finite-RC deglitch oracle.
// Run: c++ -std=c++20 -O2 -Isrc tools/sampling_phase_probe.cpp -o /tmp/sampling_phase_probe
#include "sample_input_analog.h"
#include "deglitch.h"
#include <cstdio>
using namespace eii;using namespace eii::analog;
int main(){for(unsigned shift=0;shift<30;++shift){SampleInputAnalog a;a.reset({4.98,true});Deglitch3202 o;o.resetKnownZero();unsigned rng=12345;double idac=0,mx=0,energy=0;unsigned n=0;for(unsigned sc=0;sc<360*400;++sc){if(sc%30==0){rng=rng*1664525u+1013904223u;idac=(double(rng>>8)/16777216.-.5)*.8-.02*o.output().value;}unsigned ph=(sc+360-shift)%360;LogicLevel p1{true,ph>=100&&ph<116},p2{true,ph>=120&&ph<136};if(sc%15==0){SampleInputAnalog::Pins p{{true,true},{true,false},{true,false},p1,p2,{0x3b,255},{4.98,true}};a.tick(p,nullptr,sc*2/5,{idac,true});}o.advance(.1e-6,{idac,true},p1,p2);if(ph==359&&sc>3600){double d=a.iint().value-o.output().value;mx=std::max(mx,std::abs(d));energy+=d*d;++n;}}printf("shift=%u maxdiff=%.8f rmsdiff=%.8f\n",shift,mx,std::sqrt(energy/n));}}
