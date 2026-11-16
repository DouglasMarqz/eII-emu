#include "machine.h"
#include <tuple>
#include <iostream>
using Event=std::tuple<std::uint64_t,unsigned,unsigned,unsigned,unsigned>;
static void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
static auto fixture(){
    auto m=std::make_unique<eii::Machine>(0);eii::PromLatch p;p.upper.fill(0);p.lower.fill(0x4a);
    for(unsigned a=256;a<512;++a)p.lower[a]=std::uint8_t(0x40|((((a>>5)&7)==2)?4:0));
    m->channelTimers.controllerTiming.loadProms(p);m->enableCpuDram();m->initializeDramPowerOn(1);
    m->enableProductionController();m->initializeControllerPowerOn(0xe1122026u);m->enableNormalRuntime();
    m->main.rom[0]=m->scanner.rom[0]=0x76;
    for(unsigned ch=0;ch<8;++ch){const unsigned base=ch<3?0x18:ch<6?0x10:8,reg=ch%3;
        m->channelTimers.write(base+3,std::uint8_t((reg<<6)|0x34));m->channelTimers.write(base+reg,std::uint8_t(180+ch*23));m->channelTimers.write(base+reg,0);}
    return m;
}
int main()try{
    auto sparse=fixture(),full=fixture(),bare=fixture();std::vector<Event> a,b;
    sparse->channelTimers.audioSink([&](auto edge,unsigned ch,eii::KnownByte code,std::uint8_t phase){a.emplace_back(edge,ch,code.value,code.known,phase);});
    std::array<eii::KnownByte,8> last{};std::array<unsigned,8> phases{};std::array<bool,8> sent{};
    auto capture=[&](std::uint64_t edge){for(unsigned ch=0;ch<8;++ch){const auto code=full->channelTimers.controllerTiming.channelDacCode(ch);
        const auto phase=eii::analog::nativePhases(full->channelTimers.channels[ch].phi1(),full->channelTimers.channels[ch].phi2());
        if(!sent[ch]||last[ch]!=code||phases[ch]!=phase){b.emplace_back(edge,ch,code.value,code.known,phase);last[ch]=code;phases[ch]=phase;sent[ch]=true;}}};
    capture(0);full->playbackTimingObserver([&](auto edge){capture(edge-1);});
    std::uint32_t random=0x13202070;
    for(unsigned n=0;n<400;++n){
        if(n>=100&&n%7==0){random=random*1664525+1013904223;const unsigned ch=(random>>16)&7;
            const unsigned base=ch<3?0x18:ch<6?0x10:8,reg=ch%3,reload=80+((random>>8)%700);
            for(auto* m:{sparse.get(),full.get(),bare.get()}){
                m->channelTimers.write(base+3,std::uint8_t((reg<<6)|0x34));
                m->channelTimers.write(base+reg,std::uint8_t(reload));m->channelTimers.write(base+reg,std::uint8_t(reload>>8));}
            capture(full->channelTimers.halfClocks);
        }
        sparse->run(n%3?1000:997);full->run(n%3?1000:997);bare->run(n%3?1000:997);capture(full->channelTimers.halfClocks);
        require(sparse->report()==full->report(),"sparse phase events must preserve digital report");}
    std::sort(a.begin(),a.end());std::sort(b.begin(),b.end());
    if(a!=b){unsigned i=0;while(i<a.size()&&i<b.size()&&a[i]==b[i])++i;
        std::cerr<<"events sparse="<<a.size()<<" oracle="<<b.size()<<" first="<<i<<'\n';
        if(i<a.size())std::apply([](auto...v){((std::cerr<<v<<' '),...);std::cerr<<'\n';},a[i]);
        if(i<b.size())std::apply([](auto...v){((std::cerr<<v<<' '),...);std::cerr<<'\n';},b[i]);}
    require(a==b,"sparse native DAC/phase timestamps must match50ns oracle exactly");
    require(sparse->memory==full->memory&&sparse->dramKnown==full->dramKnown,"memory equality");
    require(sparse->report()==bare->report()&&sparse->channelTimers.runtimeEvents==bare->channelTimers.runtimeEvents,"analog observer must not increase digital scheduler events");
    std::cout<<"PASS sparse native events vs complete50ns oracle: "<<a.size()<<" events, digital state unchanged; digital scheduler events="<<bare->channelTimers.runtimeEvents<<" with/without native observer\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
