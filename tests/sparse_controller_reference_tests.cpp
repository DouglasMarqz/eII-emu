#include "controller_runtime_oracle.h"
#include "channel_timers.h"
#include "reference/channel_timers_038.h"
#include <fstream>
#include <iostream>
#include <random>

static unsigned fixtureNow=0,tickNow=0;
static void compare(eii::ReferenceChannelTimers& a,eii::ChannelTimers& b) {
    a.flushEventTiming();b.flushEventTiming();
    a.controllerTiming.materializeVoiceState();
    eii::ControllerRuntimeTestAccess::compare(a.controllerTiming,b.controllerTiming);
    const auto& x=a.controllerTiming;const auto& y=b.controllerTiming;
    check(x.channel==y.channel && x.microCount==y.microCount && x.shift==y.shift &&
          x.captured==y.captured && x.requestPipeline==y.requestPipeline &&
          x.control.value==y.control.value && x.control.known==y.control.known &&
          x.observedWait==y.observedWait && x.acknowledgeEnable==y.acknowledgeEnable);
    check(x.dataPipelineValue()==y.dataPipelineValue() && x.aluOutputValue()==y.aluOutputValue());
    check(a.sourceClocks==b.sourceClocks && a.halfClocks==b.halfClocks);
    for(unsigned i=0;i<9;++i) {
        const auto& x=a.counters[i];const auto& y=b.counters[i];
        check(x.count==y.count && x.pendingLoad==y.pendingLoad && x.output==y.output &&
              x.outputKnown==y.outputKnown && x.risingEdges==y.risingEdges && x.fallingEdges==y.fallingEdges);
        if(i<8) {
            const auto& c=a.channels[i];const auto& d=b.channels[i];
            check(c.request==d.request && c.requestClocks==d.requestClocks &&
                  c.ambiguousRequestClocks==d.ambiguousRequestClocks && c.lastRequestClock==d.lastRequestClock &&
                  c.firstCount()==d.firstCount() && c.secondLowBits()==d.secondLowBits());
        }
    }
    check(a.serviceBus.ureq==b.serviceBus.ureq && a.serviceBus.acknowledge==b.serviceBus.acknowledge);
}
int main(int argc,char** argv)try {
    std::mt19937 rng(0xe1141);std::uint64_t quiet=0;
    std::vector<std::uint8_t> bytes(0x80000),known(bytes.size(),255);
    for(auto& v:bytes)v=std::uint8_t(rng());
    for(unsigned fixture=0;fixture<5;++fixture) {
        fixtureNow=fixture;
        eii::PromLatch prom;prom.upper.fill(0);
        for(unsigned i=0;i<512;++i)prom.lower[i]=((i>>5)&7)==0?0x6b:0x4a;
        if(fixture>=2 && argc==3) {
            std::ifstream u(argv[1],std::ios::binary),l(argv[2],std::ios::binary);
            check(bool(u.read(reinterpret_cast<char*>(prom.upper.data()),512)));
            check(bool(l.read(reinterpret_cast<char*>(prom.lower.data()),512)));
        }
        // Also certify that a different PROM with an event in the supposedly
        // quiet tail cannot use the collapsed interval.
        if(fixture==1)prom.upper[(5<<4)|8]=0x80;
        auto a=std::make_unique<eii::ReferenceChannelTimers>(0);auto b=std::make_unique<eii::ChannelTimers>(0);
        auto setup=[&](auto* m) {
            auto& c=m->controllerTiming;c.loadProms(prom);c.attachCpuDram({&bytes,&known});
            eii::CpuRegisterStaging s;c.initializePowerOn(1,s);
            m->initializeOutputPowerOn(3);m->enableControllerTiming();
            const unsigned bases[]={0x18,0x10,0x08};
            for(unsigned base:bases)for(unsigned ch=0;ch<3;++ch) {
                m->write(base+3,(ch<<6)|0x34);m->write(base+ch,104);m->write(base+ch,1);
            }
        }
        ;setup(a.get());setup(b.get());
        a->enableEventTiming();a->controllerTiming.enableConcreteRuntime();
        a->controllerTiming.enablePortEvents();a->controllerTiming.enableVoiceTransactions();
        b->enableEventTiming();b->controllerTiming.enableConcreteRuntime();
        b->controllerTiming.enablePortEvents();b->controllerTiming.enableVoiceTransactions();
        if(fixture==4) {
            auto stopped=[&](auto* m) {
                for(auto& c:m->counters){c={};c.outputKnown=true;c.output=false;}
                for(auto& c:m->channels){c.timer({true,false});c.request={true,false};}
            }
            ;stopped(a.get());stopped(b.get());a->enableEventTiming();b->enableEventTiming();
            a->controllerTiming.driveCpuBus({true,true},{true,true});
            b->controllerTiming.driveCpuBus({true,true},{true,true});
            // Dense certified idle spans, including the end of voice7 and
            // the real ninth/background slot. Source clocks continue with
            // parked-low timer OUT inputs.
            for(unsigned t=0;t<4000;++t){tickNow=t;a->advanceCpuTick();b->advanceCpuTick();compare(*a,*b);}
            check(b->controllerTiming.quietClocks>100);
        }
        using Event=std::tuple<std::uint64_t,unsigned,std::uint8_t,std::uint8_t,std::uint8_t>;
        std::vector<Event> audioA,audioB;
        a->audioSink([&](auto t,auto ch,auto code,auto phase){audioA.emplace_back(t,ch,code.value,code.known,phase);});
        b->audioSink([&](auto t,auto ch,auto code,auto phase){audioB.emplace_back(t,ch,code.value,code.known,phase);});
        eii::CpuRegisterStaging staging;
        for(unsigned t=0;t<60000;++t) {
            tickNow=t;
            if(t%701==0){a->enableControllerSpans((t/701)%3!=1);b->enableControllerSpans((t/701)%3!=1);}
            if(t%137==0) {
                const bool certain=t%1507!=0,inactive=bool(rng()%3),read=bool(rng()%2);
                const unsigned address=rng()%bytes.size();const eii::KnownByte data{std::uint8_t(rng()),255};
                auto bus=[&](auto* m) {
                    m->controllerTiming.driveCpuBus({certain,inactive},{certain,read});
                    m->controllerTiming.driveDramCpu(!inactive,address,data,read);
                }
            ;bus(a.get());bus(b.get());}
            if(t%83==0) {
                const auto port=std::uint8_t(0x48+(rng()%4)*8);staging.write(port,std::uint8_t(rng()));
                a->controllerTiming.cpuRegisterWrite(staging,port==0x60);b->controllerTiming.cpuRegisterWrite(staging,port==0x60);
            }
            if(t%4001==0){a->controllerTiming.reset((t/4001)%7==1);b->controllerTiming.reset((t/4001)%7==1);}
            if(t%503==0){const unsigned p=0x18+(rng()%3),v=rng()%256;a->write(p,v);b->write(p,v);}
            // Interleaved writes must be visible to a later real CAS event.
            if(t%19==0)bytes[rng()%bytes.size()]=std::uint8_t(rng());
            a->advanceCpuTick();b->advanceCpuTick();
            if(t%11==0){compare(*a,*b);check(audioA==audioB);audioA.clear();audioB.clear();}
            if(t%997==0) {
                unsigned calls=0;
                a->advanceCpuTickObserved([&](auto){++calls;});
                b->advanceCpuTickObserved([](auto){});check(calls==5);compare(*a,*b);
            }
        }
        compare(*a,*b);check(audioA==audioB);
        if(fixture==1)check(b->controllerTiming.quietClocks==0);
        quiet+=b->controllerTiming.quietClocks;
    }
    check(quiet>1000);
    std::cout<<"PASS:304000 CPU ticks, full controller/DRAM/DAC/timer oracle, CPU writes, unknown inputs, reset, PROM rejection, source observers; collapsed clocks="<<quiet<<"\n";
}catch(const std::exception& e){std::cerr<<"fixture="<<fixtureNow<<" tick="<<tickNow<<" "<<e.what()<<"\n";return 1;}
