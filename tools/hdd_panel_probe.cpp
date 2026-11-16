// Uses the same owner worker as WIN64. Private ROM/image inputs are caller-owned.
#include "front_panel_session.h"
#include <iostream>
using namespace std::chrono_literals;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static auto deadline(){return std::chrono::steady_clock::now()+4min;}
template<class F> static void waitFor(F condition){const auto until=deadline();while(!condition()){if(std::chrono::steady_clock::now()>until)throw std::runtime_error("worker timeout");std::this_thread::sleep_for(10ms);}}
int main(int argc,char**argv)try{
    if(argc!=4&&argc!=5){std::cerr<<"usage: hdd_panel_probe ROMDIR saved-bank02.img NEW-output.img [HD-OS.e2o]\n";return 2;}
    const auto original=eii::host::loadHddImage(argv[2]);
    eii::FrontPanelSession session;eii::PanelBoot config;config.romFolder=argv[1];config.hddImage=argv[2];config.realTime=false;
    session.boot(config);auto mounted=session.pauseForHdd().get();check(mounted.mounted&&!mounted.dirty,"HDD mounted clean");
    waitFor([&]{return session.snapshot().loaded;});
    auto step=[&](std::uint64_t ticks){auto target=session.snapshot().ticks+ticks;session.step(ticks);
        waitFor([&]{auto s=session.snapshot();if(!s.error.empty())throw std::runtime_error(s.error);return s.ticks>=target&&!s.running;});
        auto s=session.snapshot();std::cout<<"tick="<<s.ticks<<" LCD ["<<s.lcd[0]<<"] ["<<s.lcd[1]<<"] dirty="<<s.hdd.dirty<<std::endl;};
    step(20000000);
    const auto busy=session.pauseForHdd().get();check(!busy.idle,"cold boot DMA must be active at20m ticks");
    bool busyRejected=false;try{session.saveHddCopy(std::string(argv[3])+"-active").get();}catch(...){busyRejected=true;}
    check(busyRejected&&!std::filesystem::exists(std::string(argv[3])+"-active"),"active transfer export blocked");
    step(10000000);check(session.snapshot().lcd[0].starts_with("P01 NULL PRESET"),"HDD-only boot saved bank02");
    auto press=[&](eii::ScannerButton b,std::uint64_t ticks){session.pulse(b);step(ticks);};using B=eii::ScannerButton;
    press(B::Disk,4000000);press(B::Digit7,2000000);press(B::Digit0,2000000);press(B::Digit2,2000000);press(B::Digit9,10000000);
    auto dirty=session.pauseForHdd().get();check(dirty.dirty&&dirty.idle,"firmware bank save finished dirty and idle");
    // Existing output must fail without clearing dirty state.
    bool rejected=false;try{session.saveHddCopy(argv[2]).get();}catch(...){rejected=true;}
    check(rejected&&session.pauseForHdd().get().dirty,"failed copy preserves dirty state");
    const auto generation=session.snapshot().ticks;
    session.boot(config);waitFor([&]{return !session.snapshot().error.empty();});
    check(session.pauseForHdd().get().dirty&&session.snapshot().ticks==generation,"reset guard preserves old machine");
    auto invalid=config;invalid.discardHddChanges=true;invalid.hddImage="missing-hdd-file.img";
    session.boot(invalid);waitFor([&]{return session.snapshot().error.find("HDD image must")!=std::string::npos;});
    check(session.pauseForHdd().get().dirty,"failed replacement mount preserves old changes even with discard approved");
    session.saveHddCopy(argv[3]).get();check(!session.pauseForHdd().get().dirty,"successful copy clears dirty");
    check(eii::host::loadHddImage(argv[2])==original,"mounted source unchanged on host");
    check(eii::host::loadHddImage(argv[3]).size()==original.size(),"complete copy persisted");
    auto reopen=config;reopen.hddImage=argv[3];session.boot(reopen);check(session.pauseForHdd().get().mounted,"saved output reopens");
    waitFor([&]{return session.snapshot().ticks==0&&session.snapshot().error.empty();});
    eii::PanelBoot regular;regular.romFolder=argv[1];session.boot(regular);
    check(!session.pauseForHdd().get().mounted,"HDD disabled default retained");
    auto diagnostic=config;diagnostic.diagnostic=true;session.boot(diagnostic);
    waitFor([&]{return session.snapshot().error.find("disable Diagnostic")!=std::string::npos;});
    check(!session.pauseForHdd().get().mounted,"HD diagnostic conflict preserves default machine");
    if(argc==5){auto os=config;os.disk=argv[4];session.boot(os);check(session.pauseForHdd().get().mounted,"E2O mount");
        waitFor([&]{return session.snapshot().media.find("E2O import")!=std::string::npos;});}
    std::cout<<"PASS active transfer export rejection, diagnostic conflict, optional E2O import; WIN64 owner worker HDD boot, firmware save, pause/export, failed-save dirty retention, reset guard, transactional failed mount, source preservation, reopen and no-HDD boot\n";
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
