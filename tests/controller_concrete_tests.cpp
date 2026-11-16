#include "controller_timing.h"
#include <iostream>
#include <random>
#include <stdexcept>

#include "controller_runtime_oracle.h"
int main() try {
    std::mt19937 rng(0x20261019);std::uint64_t accelerated=0;
    std::vector<std::uint8_t> memory(0x80000),known(memory.size(),255);
    for(auto& v:memory)v=std::uint8_t(rng());
    for(unsigned fixture=0;fixture<12;++fixture) {
        eii::PromLatch prom;
        for(auto& v:prom.upper)v=std::uint8_t(rng());
        for(auto& v:prom.lower)v=std::uint8_t(rng());
        if(fixture>=10){prom.lower.fill(0x4a);prom.upper.fill(fixture==10?0:0xc0);}
        auto a=std::make_unique<eii::ControllerTiming>(std::uint8_t(fixture));
        auto b=std::make_unique<eii::ControllerTiming>(std::uint8_t(fixture));
        a->loadProms(prom);b->loadProms(prom);
        a->attachCpuDram({&memory,&known});b->attachCpuDram({&memory,&known});
        eii::CpuRegisterStaging staging,other;
        if(fixture%2){a->initializePowerOn(fixture,staging);b->initializePowerOn(fixture,other);}
        a->reset(false);b->reset(false);b->enableConcreteRuntime();b->enablePortEvents();
        for(unsigned t=0;t<10000;++t) {
            if(t%37==0) {
                const bool inactive=rng()%2,read=rng()%2;const auto address=rng()%0x80000;
                const eii::KnownByte data{std::uint8_t(rng()),255};
                a->driveCpuBus({true,inactive},{true,read});b->driveCpuBus({true,inactive},{true,read});
                a->driveDramCpu(!inactive,address,data,read);b->driveDramCpu(!inactive,address,data,read);
            }
            if(t%43==0) {
                const auto port=std::uint8_t(0x48+(rng()%4)*8);staging.write(port,std::uint8_t(rng()));
                a->cpuRegisterWrite(staging,port==0x60);b->cpuRegisterWrite(staging,port==0x60);
            }
            if(t%103==0){const bool reset=rng()%2;a->reset(reset);b->reset(reset);}
            a->fallingClock();b->fallingClock();
            eii::ControllerRuntimeTestAccess::compare(*a,*b);
            std::array<eii::LogicLevel,8> requests;
            for(auto& v:requests)v={t%17!=0,bool(rng()%2)};
            a->risingClock(requests);b->risingClock(requests);
            if(t%3==0){a->cpuClock();b->cpuClock();}
            eii::ControllerRuntimeTestAccess::compare(*a,*b);
        }
        accelerated+=b->concreteSteps;
    }
    check(accelerated>1000);
    std::cout<<"PASS:120000 randomized/idle controller clocks; complete future state, SRAM, DRAM, DAC and counters match; concrete="<<accelerated<<"\n";
} catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
