#include "reference/ssm2045_054.h"
#include "ssm2045/chip.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
int main(){double maxError=0;unsigned long long total=0;
 for(double rate:{96000.,192000.})for(bool nl:{false,true})for(bool offsets:{false,true})for(double q:{0.,60e-6,120e-6,180e-6}){
  if(!nl && q>60e-6)continue; // Linear model above onset is unbounded; nonlinear cases cover all Q.
  ssm2045::FilterConfig c;c.sampleRate=rate;c.nonlinear=nl;c.offsets=offsets;c.caps={20e-9,3.9e-9,20e-9,1.8e-9};c.fcSmoothingSeconds=0;
  ssm2045_reference::FilterConfig r;r.sampleRate=rate;r.nonlinear=nl;r.offsets=offsets;r.caps={20e-9,3.9e-9,20e-9,1.8e-9};r.fcSmoothingSeconds=0;
  ssm2045::Filter a(c);ssm2045_reference::Filter b(r);a.setQCurrent(q);b.setQCurrent(q);
  for(unsigned n=0;n<2000;++n){double fc=n<1000?-.04:.02;a.setFcPinVolts(fc);b.setFcPinVolts(fc);double in=.001*std::sin(n*.17)+.003*std::cos(n*.013);auto x=a.process(in);auto y=b.process(in);double err=std::max(std::abs(x.fourPole-y.fourPole),std::abs(x.twoPole-y.twoPole));maxError=std::max(maxError,err);if(!std::isfinite(err)||err>1e-7*(1+std::abs(y.fourPole)+std::abs(y.twoPole))){std::cerr<<"FAIL diff="<<err<<" sample="<<n<<" q="<<q<<" nl="<<nl<<'\n';return 1;}++total;}
 }
 std::cout<<"PASS: exact-equation solver comparison samples="<<total<<" maxAbsError="<<maxError<<'\n';
}
