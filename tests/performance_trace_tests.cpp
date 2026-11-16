#include "front_panel_session.h"
#include "performance_trace.h"
#include <iostream>
#include <limits>
#include <stdexcept>
static void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main()try{
    eii::host::PerformanceTrace trace;eii::PanelSnapshot s;
    trace.event(10,"off","ignored");trace.sample(10,s);require(trace.size()==0&&!trace.active(),"off must record nothing");
    trace.start(100);const auto initial=trace.size();trace.sample(110,s);require(trace.size()==initial,"pending worker opt-in must not yield fake metrics");
    s.trace.enabled=true;s.trace.machineGeneration=1;s.trace.audioGeneration=4;s.audioMode=eii::analog::RuntimeAudioMode::NonlinearChip;
    s.ticks=4000000;s.trace.ownerBusy=.5;s.trace.mainPc=0x38;s.trace.cv[2]=std::numeric_limits<double>::quiet_NaN();
    s.lcd[0]=std::string("A\t\n\"")+char(0xff);s.dsp.solverSamples=100;s.dsp.iterations=200;
    s.nativeAudio=true;s.nativeOverflow=7;s.nativeOrderErrors=3;s.reconstruction=true;s.trace.reconstructionCode[0]={35,63};
    trace.sample(350,s);s.trace.audioGeneration=5;s.dsp.solverSamples=3;trace.sample(600,s);
    trace.event(601,"threads","HOST THREAD\nname=[EII SSM2045] busy=-1");trace.stop(700);
    const auto frozen=trace.exportJsonl();trace.event(800,"late","ignored");trace.sample(900,s);
    require(frozen.find("\"native_dac\":1")!=std::string::npos&&frozen.find("\"native_overflow\":7")!=std::string::npos&&frozen.find("\"native_order_errors\":3")!=std::string::npos,"native route and transport errors exported");
    require(frozen.find("\"s3528\":1")!=std::string::npos&&frozen.find("\"s3528_codes\":[35,0")!=std::string::npos&&frozen.find("\"s3528_masks\":[63,0")!=std::string::npos,"S3528 state and known codes exported");
    require(trace.exportJsonl()==frozen&&!trace.active(),"stop must freeze all records");
    require(frozen.find("\"ms\":250")!=std::string::npos&&frozen.find("\"audio_generation\":5")!=std::string::npos,"relative timing and restart generation");
    require(frozen.find("\"solver_samples\":3")!=std::string::npos,"counter reset must remain explicit, not unsigned subtraction");
    require(frozen.find("\\u00ff")!=std::string::npos&&frozen.find("\\u000a")!=std::string::npos,"binary/LCD/thread text valid JSON escaping");
    require(frozen.find("\"cv\":[0,0,null")!=std::string::npos,"nonfinite numeric telemetry becomes null");
    trace.start(1000);for(unsigned i=0;i<10000;++i)trace.event(1000+i,"burst",std::string(8193,'x'));
    require(trace.size()<=trace.maxRows&&trace.bytes()<=trace.maxBytes&&trace.overwritten()>0,"long/burst trace stays bounded");
    const auto bounded=trace.exportJsonl();require(bounded.find("\"detail_truncated\":true")!=std::string::npos,"event truncation declared");
    trace.start(20000);require(trace.overwritten()==0&&trace.size()==1,"new recording clears old counters/data");
    std::cout<<"PASS: off/pending guards; relative time; restart generations; counter reset; JSON escaping/nulls; frozen stop; bounded recording/truncation; clean restart.\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
