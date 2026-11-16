// Boot-order probe: HDD attached, floppy inserted, "floppy first" on/off.
// Reports LCD changes, HDD bytes read and floppy media until the OS prompt.
#include <bits/stdc++.h>
#define private public // probe-only peek at the machine
#include "vst3/plugin_engine.h"
#undef private
#include <chrono>
#include <cstdio>
#include <thread>
int main(int argc,char** argv){
    if(argc<5){std::fprintf(stderr,"usage: boot_order_probe LIBRARY HDD.img FLOPPY bootFloppy(0|1)\n");return 2;}
    eii::vst::PluginSettings s;s.library=argv[1];s.romFolder=s.library/"ROMS";s.hdd=argv[2];if(*argv[3])s.floppy=argv[3];s.bootFloppy=std::atoi(argv[4]);
    s.audioMode=eii::analog::RuntimeAudioMode(5);
    eii::vst::PluginEngine engine;engine.setupProcessing(48000,512);engine.configure(s);engine.boot(s,false);engine.activate();
    std::vector<float> l(512),r(512);std::string last;auto next=std::chrono::steady_clock::now();const auto period=std::chrono::nanoseconds(std::int64_t(1e9*512/48000));
    for(int i=0;i<int(100*48000/512);++i){engine.render(l.data(),r.data(),512,false);next+=period;std::this_thread::sleep_until(next);
        const auto st=engine.status();const auto lcd=st.panel.lcd[0]+"|"+st.panel.lcd[1];
        if(lcd!=last){last=lcd;const auto* h=engine.machine_?engine.machine_->hardDisk.get():nullptr;std::printf("%6.1f s '%s' hddRead %llu held %d cmds %zu media %s\n",st.panel.ticks/4e6,lcd.c_str(),h?(unsigned long long)h->bytesRead:0ull,int(engine.floppyHeld_),h?h->commands.size():0,st.panel.media.c_str());if(st.panel.lcd[0].size()>=3&&st.panel.lcd[0][0]=='P'&&std::isdigit((unsigned char)st.panel.lcd[0][1]))break;}
    }
    std::printf("END\n");
}
