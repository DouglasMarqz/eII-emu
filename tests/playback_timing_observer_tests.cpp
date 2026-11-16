#include "machine.h"
#include "transport_fixture.h"
#include <iostream>
#include <memory>
#include <vector>
static void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
static auto fixture(bool production){
    auto m=std::make_unique<eii::Machine>(0);
    if(production){
        eii::PromLatch p;p.upper.fill(0);p.lower.fill(0x4a);
        for(unsigned a=256;a<512;++a)p.lower[a]=std::uint8_t(0x40|((((a>>5)&7)==2)?4:0));
        m->channelTimers.controllerTiming.loadProms(p);m->enableCpuDram();
        m->initializeDramPowerOn(1);m->enableProductionController();
        m->initializeControllerPowerOn(0xe1122026u);m->enableNormalRuntime();
    }else{
        m->channelTimers.controllerTiming.loadProms(transportProm());m->enableCpuDram();
    }
    m->main.rom[0]=0x76;m->scanner.rom[0]=0x76;
    m->channelTimers.write(0x1b,0x34);m->channelTimers.write(0x18,104);m->channelTimers.write(0x18,1);
    return m;
}
int main()try{
    for(bool production:{false,true}){
        auto a=fixture(production),b=fixture(production),c=fixture(production);
        std::vector<std::uint64_t> whole,chunked;
        const auto observe=[](eii::Machine& m,std::vector<std::uint64_t>& values,std::uint64_t edge){
            require(edge==m.channelTimers.halfClocks+1,"pre-edge50ns ordering");
            auto code=m.channelTimers.controllerTiming.channelDacCode(0);
            auto phi=m.channelTimers.channels[0].phi1();
            values.push_back((edge<<24)|(std::uint64_t(code.value)<<16)|(std::uint64_t(code.known)<<8)|(phi.known?2:0)|(phi.high?1:0));
        };
        b->playbackTimingObserver([&](auto edge){observe(*b,whole,edge);});
        c->playbackTimingObserver([&](auto edge){observe(*c,chunked,edge);});
        a->run(20000);b->run(20000);for(unsigned i=0;i<200;++i)c->run(100);
        require(whole.size()==100000&&whole==chunked,"exact interval count and block-invariant timeline");
        require(a->report()==b->report()&&a->report()==c->report(),"observer must preserve complete digital report");
        require(a->memory==b->memory&&a->dramKnown==b->dramKnown,"observer must preserve DRAM");
        b->playbackTimingObserver({});a->run(10000);b->run(10000);
        require(a->report()==b->report(),"detaching restores default scheduler without state change");
    }
    std::cout<<"PASS reference/production: observer off/on/chunked/detached digital state equal;100000 exact50ns intervals\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
