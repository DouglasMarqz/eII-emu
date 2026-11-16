#include "controller_upper_paths.h"
#include <iostream>
#include <set>
#include <stdexcept>
#include <tuple>
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
    // A deliberately feedback-dependent priority sequencer, not firmware.
    std::array<std::uint8_t,512> rom{};
    for(unsigned a=0;a<512;++a) {
        const unsigned phase=(a>>4)&7,feedback=a>>7;
        if(phase==0)rom[a]=(a&1)?128:((a&8)?0:64);
        if(phase==4)rom[a]=(a&8)?0:64;
        if(phase==1||phase==5)rom[a]=feedback==1?88:feedback==2?152:0;
        if(phase==2||phase==6)rom[a]=feedback==1?((a&4)?94:90):feedback==2?152:0;
    }
    eii::PromConsensus prom;prom.load(rom);
    eii::ControllerUpperPaths paths;
    paths.initialize({0,0},{0,0},{false,false},{},{});
    // Independent concrete Boolean automaton. Retain every possible
    // initial feedback/pipeline and every sampled unknown request.
    using State=std::tuple<unsigned,unsigned,unsigned,bool>; // upper,request,pipeline,done
    std::set<State> reference;
    for(unsigned u=0;u<4;++u)for(unsigned q=0;q<4;++q)for(unsigned r=0;r<2;++r)
        reference.emplace(u<<6,r,q,false);
    unsigned peak=0;
    for(unsigned t=0;t<192;++t) {
        const bool inactive=t<24 || t>=112;
        const bool readInactive=t<72;
        const bool capture=t%8==0;
        paths.driveCpu({true,inactive});
        paths.falling({64,255},{});
        paths.rising(prom,t%8,0,{true,capture},{false,false},false,
            {true,readInactive},{64,255},{},{});
        paths.cpuClock();
        std::set<State> next;
        for(auto [u,r,q,d]:reference) {
            if(inactive)d=false;
            const auto v=rom[((u&192)<<1)|((t%8)<<4)|((q&1)<<3)|((q&2)<<1)|r];
            const unsigned nq=unsigned(d||inactive)|(unsigned(readInactive)<<1);
            const bool nd=!inactive && (d||(!(u&2)&&(v&2)));
            for(unsigned nr=0;nr<2;++nr)if(capture||nr==r)next.emplace(v,nr,nq,nd);
        }
        reference=next;
        std::set<State> actual;
        for(const auto& b:paths.branches) {
            require(b.upper.known==255 && (b.pipeline.known&3)==3 && b.cpu.done.known,"clocked internal upper state remains concrete");
            require(b.cpu.waitQ.known && b.cpu.waitQ.high==(!inactive&&!b.cpu.done.high),"CPU wait samples branch completion");
            for(unsigned r=0;r<2;++r)if(!b.request.known||r==unsigned(b.request.high))
                actual.emplace(b.upper.value,r,b.pipeline.value&3,b.cpu.done.high);
        }
        require(actual==reference,"correlated states exactly match independently enumerated Boolean histories");
        peak=std::max(peak,unsigned(paths.branches.size()));
        if(t==71)require(paths.wait().known && paths.wait().high,"all request histories finish without choosing quiet startup");
    }
    require(peak<64,"equivalent histories coalesce instead of growing per clock");
    std::cout<<"PASS: correlated upper PROM/UREQ/feedback/pipeline/completion/WAIT equals independent concrete-history enumeration over192 clocks; peak branches="<<peak<<"\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
