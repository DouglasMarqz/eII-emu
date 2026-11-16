#include "vst3/host_sample_input.h"
#include <iostream>
#include <vector>
#include <limits>
using eii::vst::HostSampleInput;
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);std::cout<<"PASS "<<s<<'\n';}
int main()try{
 { HostSampleInput off,on;on.peakDiagnostics.store(true);
   off.bind(1,48000,0,0,1);on.bind(1,48000,0,0,1);
   float values[]{.125f,-.25f,.5f,-.75f,.25f};
   off.push(1,0,values,nullptr,5,false);on.push(1,0,values,nullptr,5,false);
   bool equal=true;for(unsigned tick=84;tick<333;++tick)equal&=off.volts(tick)==on.volts(tick);
   check(equal,"peak diagnostics do not alter transferred input samples");
   check(off.peak.load()==0&&on.peak.load()==.75f,"input peak collection defaults off and enables explicitly");
 }

 for(double rate:{44100.,48000.,96000.,192000.,768000.}){
  HostSampleInput s;s.bind(1,rate,123400,123400,.5);double error=0;std::uint64_t pos=0;
  for(int block:{1,7,64,511,1024}){
   std::vector<float> l(block),r(block);for(int i=0;i<block;++i){l[i]=float((pos+i)%100*.005);r[i]=l[i]*.5f;}
   s.push(1,pos,l.data(),r.data(),block,true);
   for(int i=0;i<block;++i){const auto k=pos+i;if(k<2)continue;
    const double sample=double(k)-.25;const double cpu=sample*4000000/rate;
    const double expected=(double((k-2)%100)*.25+double((k-1)%100)*.75)*.005*.75*.5;
    error=std::max(error,std::abs(s.volts(std::uint64_t(cpu),cpu-std::floor(cpu))-expected));}
   pos+=block;
  }
  check(error<1e-7&&s.missing==0,"timestamp interpolation, gain and variable block boundaries at host rate");
 }
 {
  HostSampleInput s;float a[]{.25f,.5f,.75f};s.bind(1,48000,0,0,1);s.push(1,0,a,nullptr,3,false);
  check(std::abs(s.volts(125)-.375)<1e-12,"mono passes unchanged with one-sample causal interpolation");
  s.bind(2,48000,0,0,1);s.push(2,0,nullptr,nullptr,3,false);
  check(s.volts(125)==0,"epoch restart cannot reuse old input");
  s.push(2,6,a,nullptr,3,false);check(s.volts(400)==0,"missing input is silence, never spliced or stale audio");
 }
 {
  HostSampleInput s;s.bind(1,48000,0,0,1);float a[]{.8f,.8f,.8f};s.push(1,0,a,a,3,true,true,false);
  check(std::abs(s.volts(125)-.4)<1e-7,"per-channel silence flags preserve the other stereo channel");
 }
 {
  HostSampleInput s;s.bind(1,48000,0,0,1);float a[]{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),3};s.push(1,0,a,nullptr,3,false);
  check(s.volts(125)==0&&std::abs(s.volts(250)-1)<1e-12,"nonfinite input sanitized and host full-scale bounded");
 }
 {
  HostSampleInput s;s.bind(1,48000,0,0,1);std::vector<float> a((1u<<18)+17,.1f);s.push(1,0,a.data(),nullptr,int(a.size()),false);
  check(s.drops==17,"bounded queue reports overflow without overwriting unread samples");s.discard(a.size());
  s.push(1,a.size(),a.data(),nullptr,3,false);const double t=(a.size()+1.5)*4000000./48000;check(std::abs(s.volts(std::uint64_t(t),t-std::floor(t))-.1)<1e-6,"consumer catches up after queue overflow");
 }
 {
  HostSampleInput s;float a[]{.125f,.125f,.125f};s.bind(1,48000,0,0,std::pow(10.,12./20));s.push(1,0,a,nullptr,3,false);
  check(std::abs(s.volts(125)-.125*std::pow(10.,12./20))<1e-12,"input calibration +12dB applies before analog sampling without fullscale reclamp");
 }
 return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
