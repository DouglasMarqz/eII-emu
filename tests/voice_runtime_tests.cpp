#include "controller_runtime_oracle.h"
#include <fstream>
#include <iostream>
#include <random>
int main(int argc,char** argv) try {
    std::mt19937 rng(0xe112038);std::uint64_t transactions=0;
    std::vector<std::uint8_t> memory(0x80000),known(memory.size(),255);
    for(auto& v:memory)v=std::uint8_t(rng());
    for(unsigned fixture=0;fixture<4;++fixture) {
        eii::PromLatch prom;prom.upper.fill(0);
        for(unsigned a=0;a<512;++a) {
            const auto phase=(a>>5)&7;
            prom.lower[a]=phase==0?0x6b:phase==1?std::uint8_t((a&16)?0xae:0x4a):
                phase==2?std::uint8_t((a&16)?0x0f:0x4a):0x4a;
            if(a&256)prom.lower[a]=std::uint8_t(rng()); // background remains the reference path
        }
        if(fixture>=2 && argc==3) {
            std::ifstream upper(argv[1],std::ios::binary),lower(argv[2],std::ios::binary);
            check(bool(upper.read(reinterpret_cast<char*>(prom.upper.data()),512)));
            check(bool(lower.read(reinterpret_cast<char*>(prom.lower.data()),512)));
        }
        auto a=std::make_unique<eii::ControllerTiming>(0),b=std::make_unique<eii::ControllerTiming>(0);
        a->loadProms(prom);b->loadProms(prom);a->attachCpuDram({&memory,&known});b->attachCpuDram({&memory,&known});
        eii::CpuRegisterStaging staging,other;
        if(fixture%2){a->initializePowerOn(fixture,staging);b->initializePowerOn(fixture,other);}
        a->reset(false);b->reset(false);b->enableConcreteRuntime();b->enablePortEvents();b->enableVoiceTransactions();
        for(unsigned t=0;t<50000;++t) {
            if(t%37==0) {
                const bool inactive=rng()%2,read=rng()%2;const auto address=rng()%0x80000;
                const eii::KnownByte data{std::uint8_t(rng()),255};
                const bool certain=t%407!=0;
                a->driveCpuBus({certain,inactive},{certain,read});b->driveCpuBus({certain,inactive},{certain,read});
                a->driveDramCpu(!inactive,address,data,read);b->driveDramCpu(!inactive,address,data,read);
            }
            if(t%43==0) {
                const auto port=std::uint8_t(0x48+(rng()%4)*8);staging.write(port,std::uint8_t(rng()));
                a->cpuRegisterWrite(staging,port==0x60);b->cpuRegisterWrite(staging,port==0x60);
            }
            if(t%2003==0){const bool reset=rng()%4==0;a->reset(reset);b->reset(reset);}
            if(t%5003==0){a->loadProms(prom);b->loadProms(prom);}
            if(t%71==0)b->enableVoiceTransactions(rng()%4!=0);
            if(t%997){a->fallingClock();b->fallingClock();} // Missing low aperture must cancel speculation.
            if(fixture%2==0 || t%31==0)eii::ControllerRuntimeTestAccess::compare(*a,*b);
            std::array<eii::LogicLevel,8> requests;
            for(auto& v:requests)v={t%17!=0,bool(rng()%2)};
            a->risingClock(requests);b->risingClock(requests);
            if(t%3==0){a->cpuClock();b->cpuClock();}
            check(a->dataPipelineValue()==b->dataPipelineValue() && a->sampledRfdValue()==b->sampledRfdValue() &&
                  a->aluOutputValue()==b->aluOutputValue() && a->delayedFlagValue()==b->delayedFlagValue() &&
                  a->delayedUcd4()==b->delayedUcd4());
            if(fixture%2==0 || t%31==0)eii::ControllerRuntimeTestAccess::compare(*a,*b);
        }
        eii::ControllerRuntimeTestAccess::compare(*a,*b);
        transactions+=b->voiceTransactions;
    }
    check(transactions>1000);
    std::cout<<"PASS:200000 randomized controller clocks; complete future state, SRAM/DRAM/DAC and counters match; voice transactions="<<transactions<<"\n";
} catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
