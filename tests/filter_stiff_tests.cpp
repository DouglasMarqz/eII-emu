#include "nonlinear_voice.h"
#include <iostream>
#include <stdexcept>
using namespace eii::analog;
static void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
// Independent synthetic DAC/CV stimulus: no firmware, samples or bank files.
static FilterCalibration stiffProfile(){
 FilterCalibration c;using P=FilterParam;
 c[P::C1]=1.5;c[P::C2]=10.5;c[P::C3]=.4;c[P::C4]=38;
 c[P::Gm0]=54;c[P::Vtef]=17;c[P::FcBias]=3.5;c[P::VcaFollow]=1;
 return c;
}
int main()try{
 unsigned long long samples=0;
 for(unsigned rate:{96000,192000})for(double fc:{0.,.9,2.5}){
  NonlinearPlaybackVoice v(rate);v.calibrate(stiffProfile());
  for(unsigned phase=0;phase<4;++phase){
   double energy=0;
   for(unsigned n=0;n<rate/5;++n){
    const bool note=phase==0||phase==3;
    const auto code=std::uint8_t(phase==0?(n%2?0xff:0x7f):((n/100)%2?0x3f:0xbf));
    double y=v.process({code,255},{note?5.:0.,true},{phase==1?0:fc,true},{0,true});
    require(std::isfinite(y),"nonfinite output");energy+=y*y;
   }
   auto s=v.solverStats();
   std::cout<<rate<<" fc="<<fc<<" phase="<<phase<<" fallback="<<s.fallbacks<<" recovered="<<s.recoveries<<" held="<<s.held<<" energy="<<energy<<std::endl;
   require(s.fallbacks<100,"persistent stiff solver fallback");
   require(s.held<4,"persistent rejected integrator steps");
   if(phase==1||phase==2)require(energy<1e-6,"quiet tail did not settle");
   if(phase==3)require(energy>1e-12,"subsequent note failed after quiet tail");
  }
  samples+=v.solverStats().samples;
 }
 // Live extreme configuration changes retain state, but may need bounded
 // implicit recovery. An unsuccessful solve must never inject explicit energy.
 NonlinearPlaybackVoice v(96000);auto c=stiffProfile();using P=FilterParam;
 c[P::QOverride]=1;c[P::QManual]=250;c[P::FcOverride]=1;c[P::FcManual]=-50;v.calibrate(c);
 for(unsigned n=0;n<19200;++n)require(std::isfinite(v.process({std::uint8_t(n%2?0xff:0x7f),255},{5,true},{0,true},{0,true})),"extreme transition finite");
 v.calibrate(stiffProfile());auto before=v.solverStats();
 for(unsigned n=0;n<19200;++n)require(std::isfinite(v.process({0,255},{0,true},{0,true},{0,true})),"quiet transition finite");
 auto after=v.solverStats();require(after.fallbacks-before.fallbacks<100,"persistent fallback in silence");
 std::cout<<"PASS: stiff 96/192k excitation, quiet tail, retrigger; samples="<<samples<<" transition fallback="<<after.fallbacks-before.fallbacks<<'\n';
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
