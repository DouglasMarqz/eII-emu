// Real owner-worker/firmware test. Input is never opened for writing; a NEW
// disposable HFE copy is mounted direct. Output is deliberately modified live.
#include "front_panel_session.h"
#include <iostream>
using namespace std::chrono_literals;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F>void waitFor(F f){const auto deadline=std::chrono::steady_clock::now()+5min;while(!f()){if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("worker timeout");std::this_thread::sleep_for(10ms);}}
int main(int argc,char**argv)try{
 if(argc!=5)return 2;const auto original=eii::readFile(argv[3]);eii::host::saveNewImage(argv[4],original);
 eii::FrontPanelSession s;eii::PanelBoot boot;boot.romFolder=argv[1];boot.hddImage=argv[2];boot.realTime=false;s.boot(boot);check(s.pauseForHdd().get().mounted,"HDD boot");waitFor([&]{return s.snapshot().loaded;});
 auto step=[&](std::uint64_t n){auto target=s.snapshot().ticks+n;s.step(n);waitFor([&]{auto p=s.snapshot();if(!p.error.empty())throw std::runtime_error(p.error);return p.ticks>=target&&!p.running;});};
 auto show=[&]{auto p=s.snapshot();std::cout<<p.ticks<<" ["<<p.lcd[0]<<"] ["<<p.lcd[1]<<"] floppyDirect="<<p.floppy.direct<<" dirty="<<p.floppy.dirty<<std::endl;};
 step(20000000);step(10000000);check(s.snapshot().lcd[0].starts_with("P01 NULL PRESET"),"source bank ready");
 s.floppyImage(argv[4],true).get();auto media=s.pauseForMedia().get();check(media.floppy.direct&&media.floppy.writable,"direct floppy mounted");
 step(2000000);using B=eii::ScannerButton;auto press=[&](B b,std::uint64_t n){s.pulse(b);step(n);show();};
 press(B::Disk,4000000);press(B::Digit7,4000000);press(B::Digit0,4000000);press(B::Digit1,6000000);press(B::Enter,6000000);press(B::Digit9,10000000);
 for(unsigned i=0;i<22&&!s.snapshot().lcd[0].starts_with("Disk [1-8]");++i){step(20000000);show();}
 check(s.snapshot().lcd[0].starts_with("Disk [1-8]")&&s.snapshot().lcd[1]=="                ","firmware save and verify complete without error");
 media=s.pauseForMedia().get();check(!media.floppy.dirty&&media.floppy.direct,"direct writes require no explicit save copy");
 const auto persisted=eii::readFile(argv[4]);check(persisted!=original&&eii::readFile(argv[3])==original,"live output changed and input fixture preserved");
 const auto payload=eii::HfeImage(persisted).extractEmuii();check(payload.size()==eii::EmuiiImage::imageBytes,"persisted HFE CRCs valid");
 s.floppyImage({},false).get();check(!s.pauseForMedia().get().floppy.mounted,"physical eject");
 eii::PanelBoot reload;reload.romFolder=argv[1];reload.disk=argv[4];reload.realTime=false;s.boot(reload);s.pauseForMedia().get();waitFor([&]{return s.snapshot().ticks==0&&s.snapshot().error.empty();});
 for(unsigned i=0;i<24;++i){step(20000000);show();if(s.snapshot().ticks>=30000000&&s.snapshot().lcd[0].starts_with("P01 NULL PRESET")&&s.snapshot().lcd[1]=="                ")break;}
 check(s.snapshot().lcd[0].starts_with("P01 NULL PRESET")&&s.snapshot().lcd[1]=="                ","fresh ROM boot reloads saved bank from persisted HFE");
 std::cout<<"PASS real worker direct HFE firmware save/verify -> eject -> cold boot/reload; no host Save copy; source unchanged\n";
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
