#include "dram_cpu.h"
#include <random>
#include <iostream>
#include <cassert>
int main() {
    std::mt19937 random(0xd12a2026);
    std::vector<std::uint8_t> bytes(0x80000),known(0x80000,255);for(auto& b:bytes)b=std::uint8_t(random());
    eii::DramBackingView memory{&bytes,&known};eii::DramCpuPort reference,events;events.enableEventTiming();
    eii::KnownWord control{};eii::Known24 sum{};eii::LogicLevel delayed{};
    for(unsigned clock=0;clock<300000;++clock) {
        if(clock%13==0) {
            const bool active=random()%3!=0,read=random()%2;
            const auto address=random()%0x80000;
            const eii::KnownByte data{std::uint8_t(random()),std::uint8_t(clock%17?255:random())};
            reference.cpuBus(active,address,data,read);events.cpuBus(active,address,data,read);
        }
        if(clock%19==0) {
            control={std::uint16_t(random()),std::uint16_t(clock%5?65535:random())};
            sum={std::uint32_t(random()&0xffffff),std::uint32_t(clock%7?0xffffff:random()&0xffffff)};
            delayed={bool(random()%2),bool(random()%2)};
        }
        if(clock&1){reference.rising(control,sum,memory);events.rising(control,sum,memory);}
        else {reference.falling(control,delayed,memory);events.falling(control,delayed,memory);}
        for(unsigned repeat=0;repeat<3;++repeat){reference.settle(control,memory);events.settle(control,memory);}
        assert(reference.sameState(events));
        assert(reference.reads==events.reads && reference.writes==events.writes && reference.readCaptures==events.readCaptures && reference.sampleReads==events.sampleReads);
    }
    std::cout<<"PASS:300000 randomized DRAM clock/CPU/partial-control events and repeated settles vs edge oracle; complete port state and all diagnostics\n";
}
