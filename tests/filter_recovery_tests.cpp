#include "ssm2045/chip.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
// Synthetic signal only. No firmware, bank, samples or host queues.
static auto profile(unsigned rate){ssm2045::FilterConfig c;c.sampleRate=rate;
 c.caps={1.5038343479613583e-9,10.490592077764447e-9,.41181295051385264e-9,38.308928063841471e-9};
 c.gm0=54.177421405953297e-6;c.vtef=.010695;c.fcBiasVolts=-.01014;
 c.qOscillationCurrent=78.65e-6;c.w1=.8124;c.w8=3.0552;c.vlGa=1.2;c.vlG1=.145;c.vlG2=.099334018839065716;c.vlT=2.9936;c.offsets=false;c.fcSmoothingSeconds=0;return c;}
int main()try{unsigned long long recovered=0;
 for(unsigned rate:{96000,192000})for(double fcv:{.429688,.488281,.546875}){
  ssm2045::Filter f(profile(rate));f.setFcPinVoltsImmediate((fcv/22600-15./124000)/(1./22600+1./124000+1./1000));f.setQCurrent((3.125-.6)/22000);
  for(unsigned n=0;n<4000;++n){double u=n<2000?((n/(rate/48000))%2?.8:-.8):0;auto y=f.process(u);require(std::isfinite(y.fourPole)&&std::isfinite(y.twoPole),"nonfinite output");}
  auto s=f.solverStats();require(s.held==0,"failed sample recovery retains old state");require(s.fallbacks==s.recoveries,"incomplete recovery accounting");recovered+=s.recoveries;
  // Remove feedback after the excitation; stale failed state must not persist.
  f.setQCurrent(0);double tail=0;
  for(unsigned n=0;n<4000;++n){auto y=f.process(0);if(n>=3000)tail+=y.fourPole*y.fourPole;}
  require(tail<1e-9,"zero-Q quiet tail failed to decay");
  double peak=0;for(unsigned n=0;n<2000;++n)peak=std::max(peak,std::abs(f.process(.05*std::sin(n*.01)).fourPole));
  require(peak>1e-4,"filter did not respond after recovery");require(f.solverStats().held==0,"post-recovery state held");
  std::cout<<rate<<" fcv="<<fcv<<" recovered="<<s.recoveries<<" held="<<s.held<<" tail="<<tail<<'\n';
 }
 require(recovered>100,"recovery path not exercised");std::cout<<"PASS: high-Q discontinuities, quiet tail and retrigger; recovered="<<recovered<<'\n';
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
