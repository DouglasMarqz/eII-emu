#include "front_panel_session.h"
#include <iomanip>
#include <iostream>
using namespace std::chrono_literals;
template<class F> static eii::PanelSnapshot until(eii::FrontPanelSession& session,F done) {
    const auto end=std::chrono::steady_clock::now()+15min;
    while(std::chrono::steady_clock::now()<end) {
        auto s=session.snapshot();
        if(!s.error.empty())throw std::runtime_error(s.error);
        if(done(s))return s;
        std::this_thread::sleep_for(5ms);
    }
    throw std::runtime_error("worker runtime probe timeout");
}
int main(int argc,char** argv) try {
    if(argc<3 || argc>4)throw std::invalid_argument("usage: worker_runtime_probe ROM-folder disk [CPU-ticks1..200M]");
    const std::string budget=argc==4?argv[3]:"24000000";
    if(budget.empty() || budget.find_first_not_of("0123456789")!=std::string::npos)
        throw std::invalid_argument("decimal CPU tick budget required");
    const auto ticks=std::stoull(budget);
    if(!ticks || ticks>200000000)throw std::invalid_argument("CPU tick budget1..200M");
    std::array<eii::PanelSnapshot,2> results;
    for(unsigned mode=0;mode<2;++mode) {
        eii::FrontPanelSession session;eii::PanelBoot boot;
        boot.romFolder=argv[1];boot.disk=argv[2];boot.realTime=false;boot.referenceExecution=mode==0;
        session.boot(boot);until(session,[](const auto& s){return s.loaded;});
        const auto start=std::chrono::steady_clock::now();
        std::uint64_t done=0;
        while(done<ticks) {
            const auto n=std::min<std::uint64_t>(20000000,ticks-done);session.step(n);done+=n;
            results[mode]=until(session,[&](const auto& s){return !s.running && s.ticks==done;});
        }
        const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        const auto& s=results[mode];
        std::cerr<<std::setprecision(10)<<(mode?"NORMAL":"REFERENCE")<<" ticks="<<s.ticks
                 <<" wall="<<seconds<<" realtime="<<double(ticks)/4000000/seconds
                 <<" timerEvents="<<s.timerEvents<<" concreteClocks="<<s.concreteClocks<<"\n"<<std::flush;
        if(s.referenceExecution!=(mode==0) || s.normalRuntimeEnabled!=(mode==1) ||
           (mode==0 && (s.timerEvents||s.concreteClocks)))throw std::runtime_error("worker routed to wrong engine");
    }
    const auto& a=results[0];const auto& b=results[1];
    if(a.report!=b.report || a.lcd!=b.lcd || a.leds!=b.leds || a.dma!=b.dma || a.dramReads!=b.dramReads)
        throw std::runtime_error("normal/reference worker final states differ");
    std::cout<<"PASS: same physical startup/ticks; actual GUI worker normal/reference final reports identical\n"
             <<b.report;
} catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
