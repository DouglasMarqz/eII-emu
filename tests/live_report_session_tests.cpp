#include "front_panel_session.h"
#include <fstream>
#include <iostream>
using namespace std::chrono_literals;
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>static auto until(eii::FrontPanelSession& session,F predicate){
    const auto deadline=std::chrono::steady_clock::now()+30s;
    for(;;){auto v=session.snapshot();if(!v.error.empty())throw std::runtime_error(v.error);
        if(predicate(v))return v;
        if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("worker timeout");
        std::this_thread::sleep_for(1ms);}
}
struct Fixture {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("eii-report-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture(){
        std::filesystem::create_directory(path);
        std::vector<std::uint8_t> main(8192),scanner(8192),upper(512),lower(512,0x40);
        // Actual Z80 loop emits a non-printable MIDI status byte on every OUT.
        const std::uint8_t code[]={0x3e,0x90,0xd3,4,0xc3,2,0};
        std::copy(std::begin(code),std::end(code),main.begin());scanner[0]=0x76;
        for(unsigned a=256;a<512;++a)if(((a>>5)&7)==2)lower[a]|=4;
        put("main.bin",main);put("scanner.bin",scanner);put("ic135.bin",upper);put("ic137.bin",lower);
    }
    void put(const char* name,const std::vector<std::uint8_t>& data){
        std::ofstream f(path/name,std::ios::binary);f.write(reinterpret_cast<const char*>(data.data()),data.size());
        if(!f)throw std::runtime_error("fixture write failed");
    }
    ~Fixture(){std::error_code ec;std::filesystem::remove_all(path,ec);}
};
int main() try {
#if EII_DIAGNOSTICS
    std::cout<<"SKIP: release publication test requires EII_DIAGNOSTICS=0.\n";return 0;
#else
    Fixture fixture;eii::FrontPanelSession session;eii::PanelBoot boot;boot.romFolder=fixture.path;boot.realTime=false;
    session.boot(boot);auto v=until(session,[](auto& s){return s.loaded;});
    const auto quietRevision=v.reportRevision;
    for(unsigned i=1;i<=5;++i){session.step(200000);v=until(session,[&](auto& s){return !s.running&&s.ticks==i*200000;});
        require(v.report==eii::host::PanelReportText::offMessage&&v.reportRevision==quietRevision,
                "serial-producing firmware must not grow/revise normal GUI text");}
    session.liveSerial(true);v=until(session,[&](auto& s){return s.reportRevision!=quietRevision;});
    require(v.ticks==1000000&&v.report.size()<2200&&v.report.find("\\x90")!=std::string::npos,
            "live opt-in bounded and does not advance firmware");
    session.captureState();v=until(session,[](auto& s){return s.report.find("MANUAL STATE CAPTURE")!=std::string::npos;});
    const auto frozen=v.report;const auto frozenRevision=v.reportRevision;
    require(frozen.size()>190000&&frozen.find("2651 TX (register-level capture")!=std::string::npos,
            "quiet/live policies retain complete UART history for manual capture");
    session.step(200000);v=until(session,[](auto& s){return !s.running&&s.ticks==1200000;});
    require(v.report==frozen&&v.reportRevision==frozenRevision,"manual capture stays frozen");
    session.liveSerial(true);v=until(session,[&](auto& s){return s.reportRevision!=frozenRevision;});
    require(v.report.starts_with("LIVE SERIAL TX:")&&v.report.size()<2200,"explicit live resumes after capture");
    const auto liveRevision=v.reportRevision;session.liveSerial(false);
    v=until(session,[&](auto& s){return s.reportRevision!=liveRevision;});
    require(v.report==eii::host::PanelReportText::offMessage,"live view can be disabled");
    session.boot(boot);v=until(session,[](auto& s){return s.loaded&&s.ticks==0;});
    require(v.report==eii::host::PanelReportText::offMessage&&v.reportRevision>frozenRevision,"reboot resets view only");
    std::cout<<"PASS: real synthetic Z80 TX flood; quiet revision constant; explicit live bounded; full frozen capture; reboot.\n";
#endif
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
