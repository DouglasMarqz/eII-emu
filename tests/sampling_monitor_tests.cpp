#include "nonlinear_voice.h"
#include <bit>
#include <iostream>
using namespace eii;using namespace eii::analog;
int main()try{
 NonlinearVoiceBank a(2),b(2);FilterCalibration cal;a.calibrate(cal);b.calibrate(cal);double energy=0;
 for(unsigned n=0;n<5000;++n){NonlinearVoiceBank::Input x;x.native.enabled=x.native.valid=true;x.native.start=std::uint64_t(n)*nativeFrameTime;
  auto& p=x.native.initial;p.channel0=true;p.monitorSampling=true;p.monitorIint={.05*std::sin(n*.03),true};
  for(unsigned c=0;c<8;++c){p.cv[0][c]={std::uint8_t(c?0:250),255};p.cv[1][c]={128,255};p.cv[2][c]={0,255};p.dac[c]={127,255};p.phases[c]=12;}
  auto y=x;y.native.count=2;y.native.events[0]={500,NativeEvent::Pins,0,std::uint8_t(n),255,13};y.native.events[1]={1500,NativeEvent::Pins,0,std::uint8_t(n*7),255,14};
  const float l=a.process(x),r=b.process(y);if(std::bit_cast<std::uint32_t>(l)!=std::bit_cast<std::uint32_t>(r))throw std::runtime_error("masked playback DAC perturbed IINT monitor");energy+=l*l;
 }
 if(energy<1e-8)throw std::runtime_error("monitor was silent");
 NativePins pins{};NativeEvent event{0,NativeEvent::MonitorIint,0,1,255,0};event.analogBits=std::bit_cast<std::uint32_t>(.25f);applyNativeEvent(pins,event);
 if(!pins.monitorSampling||!pins.monitorIint.known||pins.monitorIint.value!=.25)throw std::runtime_error("monitor node event transport");
 event.value=0;event.known=0;applyNativeEvent(pins,event);if(pins.monitorSampling)throw std::runtime_error("monitor node release");
 std::cout<<"PASS monitor IINT independent of masked playback DAC events; node transport/release; energy "<<energy<<'\n';return 0;
}catch(std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
