// Private-input, unmodified firmware. No source-image writes or memory patches.
#include "vst3/plugin_engine.h"
#include <fstream>
#include <iostream>
using namespace eii;
int main(int argc,char** argv)try{
 if(argc!=4&&argc!=5)return 2;vst::PluginSettings s;s.romFolder=argv[1];s.hdd=argv[2];s.audioMode=analog::RuntimeAudioMode::NonlinearChip192;s.nativeAudio=true;s.reconstruction=true;s.calibration[analog::FilterParam::MixGain]=4;s.calibration[analog::FilterParam::NativeOutDb]=17.25;
 vst::PluginEngine e;e.setupProcessing(48000,512);e.configure(s);e.boot(s,false);e.activate();e.startSamplingTrace().get();std::array<float,512> l{},r{},in{};std::uint64_t pos=0;bool continuity=true;
 auto run=[&](unsigned blocks,const char* label){double energy=0,peak=0;
  for(unsigned b=0;b<blocks;++b){for(unsigned i=0;i<512;++i)in[i]=float(.05*std::sin((pos+i)*2*3.141592653589793*440/48000));e.pushInput(in.data(),in.data(),512,true);e.render(l.data(),r.data(),512,true);pos+=512;for(auto v:l){energy+=v*v;peak=std::max(peak,std::abs(double(v)));}}
  if(label){auto st=e.status();std::cout<<label<<" LCD="<<st.panel.lcd[0]<<"/"<<st.panel.lcd[1]<<" ticks="<<st.panel.ticks<<" rms="<<std::sqrt(energy/(blocks*512))<<" peak="<<peak<<" missing="<<st.inputMissing<<" drops="<<st.inputDrops<<" SAR="<<st.sampleReads<<" unknown="<<st.sampleUnknownReads<<" rails="<<st.sampleRailClips<<" dspdrops="<<st.dspDrops<<" error="<<st.panel.error<<std::endl;}}
 ;bool ready=false;for(unsigned i=0;i<7500;++i){run(1,nullptr);auto st=e.status();if(!st.panel.lcd[0].empty()&&st.panel.lcd[0][0]=='P'){ready=true;break;}}if(!ready)throw std::runtime_error("boot timeout");
 auto press=[&](ScannerButton b){e.button(b,true);run(19,nullptr);e.button(b,false);run(187,nullptr);};
 press(ScannerButton::Sample);run(282,"monitor1");press(ScannerButton::Digit9);run(94,"record1");if(argc==5){const auto before=e.rebases();
  for(unsigned b=0;b<94;++b){for(unsigned i=0;i<512;++i)in[i]=float(.05*std::sin((pos+i)*2*3.141592653589793*440/48000));e.pushInput(in.data(),in.data(),512,true);e.render(l.data(),r.data(),512,false);pos+=512;}
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  const auto delta=e.rebases()-before;continuity=delta==0;std::cout<<"induced callback burst rebase delta="<<delta<<std::endl;
 }
 press(ScannerButton::Digit0);run(188,"after-stop1");press(ScannerButton::Sample);run(94,"leave");press(ScannerButton::Sample);run(282,"monitor2");press(ScannerButton::Digit9);run(94,"record2");press(ScannerButton::Digit0);run(188,"after-stop2");
 auto future=e.samplingTrace();const auto csv=future.get();std::ofstream out(argv[3]);out<<csv;out.close();if(!out)throw std::runtime_error("trace write failed");return continuity?0:1;
}catch(std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}
