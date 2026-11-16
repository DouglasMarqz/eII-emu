// Engine-level slider probe: the real VST3 PluginEngine (owner thread, event
// queue, DSP worker, ring), paced in real time like a host (no GUI/host).
// usage: engine_slider_probe ROMDIR FLOPPY [MODE=7] [HZ=8] [RATE=96000] [BLOCK=256]
// Filter page 1 (Freq/Q/Env), then slider A square flicks for 3 s via
// queueVoltage, then logs LCD changes for 6 s after release.
#include "vst3/plugin_engine.h"
#include <chrono>
#include <cstdio>
#include <iostream>
#include <thread>
int main(int argc,char** argv)try{
    if(argc<3){std::cerr<<"usage: engine_slider_probe ROMDIR FLOPPY [MODE] [HZ] [RATE] [BLOCK]\n";return 2;}
    const unsigned mode=argc>3?unsigned(std::atoi(argv[3])):7u;const double hz=argc>4?std::atof(argv[4]):8.0;
    const unsigned rate=argc>5?unsigned(std::atoi(argv[5])):96000u;const int block=argc>6?std::atoi(argv[6]):256;
    eii::vst::PluginSettings s;s.romFolder=argv[1];s.floppy=argv[2];s.audioMode=eii::analog::RuntimeAudioMode(mode);s.sliderThrottle=getenv("THROTTLE")!=nullptr;
    eii::vst::PluginEngine engine;engine.setupProcessing(rate,block);engine.configure(s);engine.boot(s,false);engine.activate();
    std::vector<float> left(block),right(block);
    using clk=std::chrono::steady_clock;auto next=clk::now();const auto period=std::chrono::nanoseconds(std::int64_t(1e9*block/rate));
    const auto t0=clk::now();auto ms=[&]{return std::chrono::duration<double,std::milli>(clk::now()-t0).count();};
    auto tick=[&]{engine.render(left.data(),right.data(),block,false);next+=period;std::this_thread::sleep_until(next);};
    auto wait=[&](double msec){const auto end=ms()+msec;while(ms()<end)tick();};
    auto lcd=[&]{const auto st=engine.status();return st.panel.lcd[0]+"|"+st.panel.lcd[1];};
    for(int i=0;i<30000;++i){tick();const auto l=engine.status().panel.lcd[0];if(l.size()>=3&&l[0]=='P'&&std::isdigit((unsigned char)l[1]))break;}
    wait(1000);std::printf("BOOT %.0f ms '%s'\n",ms(),lcd().c_str());
    auto press=[&](eii::ScannerButton b){engine.button(b,true);wait(80);engine.button(b,false);wait(200);};
    press(eii::ScannerButton::Filter);wait(2500);
    const std::uint8_t on[3]{0x90,60,100},off[3]{0x80,60,0};engine.queueMidi(0,on,3);wait(300);engine.queueMidi(0,off,3);wait(1500);
    press(eii::ScannerButton::Enter);wait(500);if(getenv("ALLV")){press(eii::ScannerButton::Digit9);wait(300);press(eii::ScannerButton::Digit9);wait(300);}else{press(eii::ScannerButton::Digit7);wait(300);}press(eii::ScannerButton::Digit1);wait(500);
    std::printf("MENU '%s'\n",lcd().c_str());
    std::string last=lcd();unsigned during=0;const double start=ms();
    while(ms()-start<3000){const double ph=std::fmod((ms()-start)/1000.0*hz,1.0);{const double v=std::fmod((ms()-start)/1000.0*hz,1.0);engine.queueVoltage(0,0,5.0*(v<.5?2*v:2-2*v));}tick();const auto l=lcd();if(l!=last){last=l;++during;}}
    engine.queueVoltage(0,0,0.0);const double rel=ms();unsigned after=0;double lastAt=0;
    while(ms()-rel<6000){tick();const auto l=lcd();if(l!=last){last=l;++after;lastAt=ms()-rel;if(after<=30)std::printf("  +%6.0f ms '%s'\n",lastAt,l.c_str());}}
    const auto st=engine.status();
    std::printf("ENGINE mode %u %.0f Hz square flicks 3 s: LCD changes during=%u after=%u last %.0f ms after release; underruns %llu eventDrops %llu ownerLag %.2f ms machineLoad %.3f\n",
        mode,hz,during,after,lastAt,(unsigned long long)st.underruns,(unsigned long long)st.eventDrops,st.ownerLagMs,st.machineLoad);
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
