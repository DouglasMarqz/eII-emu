#include "machine.h"
#include <iostream>
#include <bit>
static void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
static auto fixture(bool spans,std::vector<std::uint32_t>& pcm) {
    auto m=std::make_unique<eii::Machine>(0);
    eii::PromLatch p;p.upper.fill(0);p.lower.fill(0x4a);
    for(unsigned a=256;a<512;++a)p.lower[a]=std::uint8_t(0x40|((((a>>5)&7)==2)?4:0));
    
    m->channelTimers.controllerTiming.loadProms(p);m->enableCpuDram();
    m->initializeDramPowerOn(1);m->enableProductionController();
    m->initializeControllerPowerOn(0xe1122026u);m->enableNormalRuntime();
    m->channelTimers.enableProductionSpans(spans);
    m->main.rom[0]=0x76;m->scanner.rom[0]=0x76;
    m->enableRuntimeAudio([ptr=m.get(),&pcm](auto frames){
        require(ptr->channelTimers.controllerTiming.risingEdges==(ptr->channelTimers.halfClocks+1)/2,
                "external PCM callback sees current production counters");
        for(auto f:frames)pcm.push_back(std::bit_cast<std::uint32_t>(f.left));
    });
    return m;
}
int main()try {
    auto verifier=std::make_unique<eii::Machine>(0);
    verifier->enableNormalRuntime();verifier->run(1000);
    require(!verifier->productionControllerEnabled(),"span option preserves default reference route");
    std::vector<std::uint32_t> pa,pb;
    auto a=fixture(false,pa),b=fixture(true,pb);
    for(unsigned step=0;step<80;++step) {
        a->run(20000);b->run(20000);
        require(a->report()==b->report(),"span and edge reports must match");
        require(a->memory==b->memory && a->dramKnown==b->dramKnown &&
                a->scannerRam==b->scannerRam,"spans must not alter memory/firmware state");
        require(pa==pb,"spans must not alter PCM");
        require(a->main.cpu.pc==b->main.cpu.pc && a->main.pins==b->main.pins &&
                a->scanner.cpu.pc==b->scanner.cpu.pc && a->scanner.pins==b->scanner.pins,
                "both CPUs and WAIT/interrupt pins remain equal");
    }
    require(b->channelTimers.controllerTiming.quietClocks>1000,"span scheduler must be exercised");
    // Even an unselected unknown request must invalidate production before
    // a quiet period can bypass the normal per-edge request validation.
    auto unknown=fixture(true,pa);
    for(unsigned k=0;k<10000 && !(unknown->channelTimers.controllerTiming.productionQuietCandidate() && !(unknown->channelTimers.halfClocks&1));++k)
        unknown->run(1);
    require(unknown->channelTimers.controllerTiming.productionQuietCandidate(),"unknown-request test reaches quiet candidate");
    unknown->channelTimers.channels[7].request={false,false};
    unknown->channelTimers.advanceCpuTick();
    require(unknown->channelTimers.controllerTiming.productionEngineInvalid(),"quiet span must preserve unknown-request rejection");
    b->channelTimers.controllerTiming.driveCpuBus({false,false},{true,true});
    bool stopped=false;try{b->run(1);}catch(const std::runtime_error&){stopped=true;}
    require(stopped && b->channelTimers.controllerTiming.productionEngineInvalid(),
            "sparse observation cannot hide an invalid production engine");
    std::cout << "PASS: production spans vs full edge scheduler;1.6M ticks of boundary reports/memory/CPU pins/PCM exact; invalid-state stop retained\n";
}catch(const std::exception& e){std::cerr << e.what() << '\n';return 1;}
