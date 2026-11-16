// Private-input real-firmware smoke probe. Does not write the source media.
#include "vst3/plugin_engine.h"
#include <iostream>
int main(int argc,char** argv)try{
    if(argc!=3){std::cerr<<"usage: legacy_q_boot_probe ROM_FOLDER FLOPPY\n";return 2;}
    eii::vst::PluginSettings s;s.romFolder=argv[1];s.floppy=argv[2];s.audioMode=eii::analog::RuntimeAudioMode::LegacyQ;s.nativeAudio=true;s.reconstruction=true;s.directImageWrites=false;
    eii::vst::PluginEngine engine;engine.setupProcessing(48000,512);engine.configure(s);engine.boot(s,false);engine.activate();
    std::array<float,512> left{},right{};bool ready=false;
    for(unsigned n=0;n<7500;++n){engine.render(left.data(),right.data(),512,true);const auto st=engine.status();
        if(!st.panel.error.empty())throw std::runtime_error(st.panel.error);
        const auto& lcd=st.panel.lcd[0];if(lcd.size()>=3&&lcd[0]=='P'&&std::isdigit(static_cast<unsigned char>(lcd[1]))&&std::isdigit(static_cast<unsigned char>(lcd[2]))){ready=true;break;}}
    auto st=engine.status();std::cout<<"ready="<<ready<<" LCD="<<st.panel.lcd[0]<<" / "<<st.panel.lcd[1]<<" ticks="<<st.panel.ticks<<" native="<<st.panel.nativeAudio<<" s3528="<<st.panel.reconstruction<<" model="<<unsigned(st.panel.audioMode)<<'\n';
    if(!ready)return 1;
    const std::uint8_t note[3]{0x90,60,100};engine.queueMidi(0,note,3);double energy=0;float peak=0;
    for(unsigned n=0;n<188;++n){engine.render(left.data(),right.data(),512,true);for(float x:left){if(!std::isfinite(x))throw std::runtime_error("Nonfinite audio");energy+=x*x;peak=std::max(peak,std::abs(x));}}
    st=engine.status();std::cout<<"energy="<<energy<<" peak="<<peak<<" MIDIread="<<st.panel.midiRead<<" underruns="<<st.underruns<<" DSPdrops="<<st.dspDrops<<" errors="<<st.panel.error<<'\n';
    return energy>0&&st.panel.midiRead>0&&st.underruns==0&&st.dspDrops==0&&st.panel.error.empty()?0:1;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
