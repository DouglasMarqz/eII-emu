#include "host_dc_block.h"
#include "runtime_audio.h"
#include <iostream>
#include <stdexcept>
using namespace eii;
static void require(bool x,const char* why){if(!x)throw std::runtime_error(why);}
int main()try{
 host::DcBlock dc;
 for(unsigned n=0;n<48000;++n)require(dc.process(.356)==0,"constant startup DC passed");
 require(std::abs(dc.estimate()-.356)<1e-12,"DC estimate missing");
 // A DC change is AC-coupled, then decays. Preserve sustained bass/music.
 for(unsigned n=0;n<96000;++n)dc.process(-.2);
 require(std::abs(dc.process(-.2))<1e-9,"DC change did not settle");
 constexpr double pi=3.14159265358979323846;
 for(double hz:{20.,440.,10000.}){
  host::DcBlock f;double energy=0;
  for(unsigned n=0;n<96000;++n){double y=f.process(.356+.1*std::sin(2*pi*hz*n/48000));if(n>=48000)energy+=y*y;}
  const double k=std::tan(pi*hz/48000)/(pi*2/48000);
  const double expected=.1/std::sqrt(2)*k/std::sqrt(1+k*k);
  require(std::abs(std::sqrt(energy/48000)-expected)<1e-6,"AC frequency response changed");
 }
 // Offset-only chip voices plus repeated queue underruns must not produce
 // the large offset-to-zero pulses of an unconditioned PCM output.
 analog::FilterCalibration c;c[analog::FilterParam::VcaOffset]=-6;
 analog::NonlinearVoiceBank bank(2);bank.calibrate(c);analog::NonlinearVoiceBank::Input in;in.channel0Playback=true;
 for(unsigned ch=0;ch<8;++ch){in.playback.codes[ch]={0,255};in.playback.amplitudeCv[ch]={0,true};in.frequency[ch]={2.5,true};in.resonance[ch]={0,true};}
 host::AudioQueue q;double peak=0;
 for(unsigned n=0;n<144000;++n){float y=bank.process(in);if(n>96000){host::StereoFrame frame{y,y};q.push(std::span(&frame,1));std::array<host::StereoFrame,2> out;q.pop(out);for(auto f:out)peak=std::max(peak,double(std::abs(f.left)));}}
 require(peak<1e-6,"offset/underrun made audible PCM pulses");
 require(std::abs(bank.outputDc()+8*4700*6e-6*analog::hostOutputReference)<1e-6,"modeled offset was not retained in diagnostics"); // 7-52 mix amplifier inverts (level_model.h)
 require(q.stats().missing>0,"underrun test did not underrun");
 std::cout<<"PASS: DC rejection/estimate, steps settle,20/440/10000Hz response,8 offset-only voices with actual zero-filled underruns\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
