#include "machine.h"
#include <iostream>
#include <bit>
static void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
static auto fixture(bool blocks,std::vector<std::uint32_t>& pcm) {
    auto m=std::make_unique<eii::Machine>(0);
    eii::PromLatch p;p.upper.fill(0);p.lower.fill(0x40);
    for(unsigned a=0;a<512;++a)if((a&256)&&((a>>5)&7)==2)p.lower[a]|=4;
    m->channelTimers.controllerTiming.loadProms(p);m->enableCpuDram();
    m->initializeDramPowerOn(1);m->enableProductionController();
    m->initializeControllerPowerOn(0xe1122026u);m->enableNormalRuntime();
    m->channelTimers.controllerTiming.enableBlockPublication(blocks);
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
    require(!verifier->productionControllerEnabled(),"block-publication option preserves default reference route");
    std::vector<std::uint32_t> pa,pb;
    auto a=fixture(false,pa),b=fixture(true,pb);
    for(unsigned step=0;step<50;++step) {
        a->run(20000);b->run(20000);
        require(a->report()==b->report(),"block-boundary report must exactly equal per-CPU publication");
        require(a->memory==b->memory && a->dramKnown==b->dramKnown &&
                a->scannerRam==b->scannerRam,"publication must not alter memory/firmware state");
        require(pa==pb,"publication must not alter PCM");
        require(a->main.cpu.pc==b->main.cpu.pc && a->main.pins==b->main.pins &&
                a->scanner.cpu.pc==b->scanner.cpu.pc && a->scanner.pins==b->scanner.pins,
                "both CPUs and WAIT/interrupt pins remain equal");
    }
    b->channelTimers.controllerTiming.driveCpuBus({false,false},{true,true});
    bool stopped=false;try{b->run(1);}catch(const std::runtime_error&){stopped=true;}
    require(stopped && b->channelTimers.controllerTiming.productionEngineInvalid(),
            "sparse observation cannot hide an invalid production engine");
    std::cout << "PASS: per-CPU vs block publication;1M ticks of boundary reports/memory/CPU pins/PCM exact; invalid-state stop retained\n";
}catch(const std::exception& e){std::cerr << e.what() << '\n';return 1;}
