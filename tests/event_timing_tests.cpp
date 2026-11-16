#include "channel_timers.h"
#include <cassert>
#include <iostream>
#include <random>

static unsigned currentTrial=0;
static void equal(const eii::ChannelTimers& a,const eii::ChannelTimers& b) {
    assert(a.halfClocks==b.halfClocks && a.sourceClocks==b.sourceClocks);
    for(unsigned i=0;i<9;++i) {
        const auto& x=a.counters[i];const auto& y=b.counters[i];
        assert(x.count==y.count && x.running==y.running && x.pendingLoad==y.pendingLoad && x.nullCount==y.nullCount);
        assert(x.outputKnown==y.outputKnown && x.output==y.output && x.risingEdges==y.risingEdges && x.fallingEdges==y.fallingEdges);
        if(i<8) {
            const auto& c=a.channels[i];const auto& d=b.channels[i];
            if(!(c.request==d.request && c.requestClocks==d.requestClocks && c.ambiguousRequestClocks==d.ambiguousRequestClocks && c.lastRequestClock==d.lastRequestClock)) {
                std::cerr<<"trial="<<currentTrial<<" ch="<<i<<" source="<<a.sourceClocks<<" request="<<c.request.known<<c.request.high<<"/"<<d.request.known<<d.request.high<<" clocks="<<c.requestClocks<<"/"<<d.requestClocks<<" ambiguous="<<c.ambiguousRequestClocks<<"/"<<d.ambiguousRequestClocks<<" last="<<c.lastRequestClock<<"/"<<d.lastRequestClock<<"\n";
                assert(false);
            }
            assert(c.dividerKnown()==d.dividerKnown() && c.firstCount()==d.firstCount() && c.secondLowBits()==d.secondLowBits());
            assert(c.phi1()==d.phi1() && c.phi2()==d.phi2() && c.samplingPulse()==d.samplingPulse());
        }
    }
    assert(a.serviceBus.ureq==b.serviceBus.ureq && a.serviceBus.acknowledge==b.serviceBus.acknowledge);
}
int main() {
    std::mt19937 rng(0xE112025);
    eii::ChannelTimers reference,events;events.enableEventTiming();
    const unsigned bases[]={0x18,0x10,0x08};
    for(unsigned trial=0;trial<1600;++trial) {
        currentTrial=trial;
        // Include unsupported modes, partial loads, zero/one reloads and
        // changing OUT phases. Every external write must flush old time first.
        const unsigned base=bases[rng()%3],counter=rng()%3;
        unsigned port=base+(rng()%4),value=rng()%256;
        if(trial%3==0){port=base+3;value=(counter<<6)|0x34;}
        reference.write(port,value);events.write(port,value);
        if(trial%5==0) {
            const unsigned ch=rng()%8;const eii::LogicLevel ack{bool(rng()%2),bool(rng()%2)};
            reference.driveControllerPins(ch,7,ack);events.driveControllerPins(ch,7,ack);
        }
        const auto clocks=1+rng()%1000;
        for(unsigned tick=0;tick<clocks;++tick){reference.advanceCpuTick();events.advanceCpuTick();}
        if(trial%7==0) {
            const auto p=base+(rng()%4),v=rng()%256;
            reference.write(p,v);events.write(p,v); // flush deferred time before OUT
        }
        if(trial%11==0) {
            const unsigned ch=rng()%8;const eii::LogicLevel ack{bool(rng()%2),bool(rng()%2)};
            reference.driveControllerPins(ch,7,ack);events.driveControllerPins(ch,7,ack);
        }
        events.flushEventTiming();equal(reference,events);
        if(trial%31==0) {
            reference.advanceCpuTick();events.advanceCpuTick(); // pending time at observer entry
            unsigned callbacks=0;
            std::array<std::array<eii::LogicLevel,16>,5> phases{};
            reference.advanceCpuTickObserved([&](auto){
                for(unsigned ch=0;ch<8;++ch){phases[callbacks][ch*2]=reference.channels[ch].phi1();phases[callbacks][ch*2+1]=reference.channels[ch].phi2();}
                ++callbacks;
            });callbacks=0;
            events.advanceCpuTickObserved([&](auto){
                for(unsigned ch=0;ch<8;++ch){assert(phases[callbacks][ch*2]==events.channels[ch].phi1());assert(phases[callbacks][ch*2+1]==events.channels[ch].phi2());}
                ++callbacks;
            });
            assert(callbacks==5);equal(reference,events);
        }
    }
    // Long low-OUT and stopped-divider intervals exercise algebraic bulk steps.
    for(unsigned base:bases)for(unsigned c=0;c<3;++c) {
        for(auto* m:{&reference,&events}){m->write(base+3,(c<<6)|0x34);m->write(base+c,0);m->write(base+c,0);}
    }
    const auto beforeEvents=events.runtimeEvents;
    for(unsigned i=0;i<300000;++i){reference.advanceCpuTick();events.advanceCpuTick();}
    events.flushEventTiming();equal(reference,events);
    assert(events.runtimeEvents>0 && events.runtimeEvents-beforeEvents<1000);
    std::cout<<"event timing: randomized writes/ACKs, unknown phases, reloads and observer fallback PASS; events="<<events.runtimeEvents<<" source-clocks="<<events.sourceClocks<<"\n";
}
