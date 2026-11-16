// Engine-level .EII import probe: boot from an .eii bank (OS area from
// <library>\DISKS\os-images), paced like a host, report boot wall time and LCD.
#include "vst3/plugin_engine.h"
#include <chrono>
#include <cstdio>
#include <thread>
int main(int argc,char** argv){
    if(argc<4){std::fprintf(stderr,"usage: eii_import_probe LIBRARY BANK.eii turbo(0|1)\n");return 2;}
    eii::vst::PluginSettings s;s.library=argv[1];s.romFolder=s.library/"ROMS";s.floppy=argv[2];
    s.audioMode=eii::analog::RuntimeAudioMode(argc>4?std::atoi(argv[4]):5);
    eii::vst::PluginEngine engine;engine.setupProcessing(48000,512);engine.configure(s);engine.boot(s,false);engine.activate();
    std::vector<float> l(512),r(512);using clk=std::chrono::steady_clock;auto next=clk::now();const auto t0=clk::now();
    const auto period=std::chrono::nanoseconds(std::int64_t(1e9*512/48000));
    std::string last;
    for(int i=0;i<30000;++i){engine.render(l.data(),r.data(),512,false);next+=period;std::this_thread::sleep_until(next);
        const auto st=engine.status();const auto lcd=st.panel.lcd[0]+"|"+st.panel.lcd[1];
        if(lcd!=last){last=lcd;std::printf("%7.0f ms ticks %.1f s '%s' media '%s' err '%s'\n",std::chrono::duration<double,std::milli>(clk::now()-t0).count(),st.panel.ticks/4e6,lcd.c_str(),st.panel.media.c_str(),st.panel.error.c_str());}
        if(st.panel.lcd[0].size()>=3&&st.panel.lcd[0][0]=='P'&&std::isdigit((unsigned char)st.panel.lcd[0][1])){std::printf("READY wall %.1f s emulated %.1f s underruns %llu rebases?\n",std::chrono::duration<double>(clk::now()-t0).count(),st.panel.ticks/4e6,(unsigned long long)st.underruns);
            for(int k=0;k<int(6*48000/512);++k){engine.render(l.data(),r.data(),512,false);next+=period;std::this_thread::sleep_until(next);}
            const auto s2=engine.status();std::printf("STEADY machineLoad %.3f dspLoad %.3f speed %.3f\n",s2.machineLoad,s2.panel.dsp.load,s2.panel.speedRatio);break;}
    }
}
