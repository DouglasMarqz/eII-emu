#include "vca_board_control.h"
#include "nonlinear_voice.h"
#include <iostream>
#include <stdexcept>
using namespace eii::analog;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
int main()try{
 for(unsigned rate:{96000u,192000u}){
  VcaBoardControl control(rate);double ref=0,maxError=0;
  for(unsigned n=0;n<20000;++n){
   const double thr=VcaBoardControl::threshold,acv=n<4000?5:n<8000?0:n<12000?thr-.01:n<16000?thr+.01:2.8;
   const auto derivative=[&](double v){return ((acv-v)/4700-std::max(0.0,(v-thr)/5150))/1e-6;};
   const double dt=1.0/(rate*32);
   for(unsigned k=0;k<32;++k){const double a=derivative(ref),b=derivative(ref+a*dt/2),c=derivative(ref+b*dt/2),d=derivative(ref+c*dt);ref+=dt*(a+2*b+2*c+d)/6;}
   const double current=control.process(acv);maxError=std::max(maxError,std::abs(control.capacitorVolts()-ref));
   check(current>=0&&current<=500e-6,"board current range");
  }
  check(maxError<1e-7,"loaded capacitor differs from independent RK4");
  control.reset();check(control.process(0)==0,"cold zero must close VCA");
  for(unsigned n=0;n<rate/5;++n)control.process(5);
  check(std::abs(control.process(5)-(5-VcaBoardControl::threshold)/9850)<1e-15,"physical steady-state current (-BIAS shifted threshold)");
  for(unsigned n=0;n<rate/10;++n)control.process(0);
  check(control.process(0)==0,"released capacitor must close current exactly");
  std::cout<<"PASS board rate="<<rate<<" RK4 max-voltage-error="<<maxError<<'\n';
  for(unsigned route=0;route<2;++route)for(unsigned model=0;model<3;++model){
   NonlinearPlaybackVoice v(rate,model==1,model==2);FilterCalibration c;v.calibrate(c);
   double open=0,closed=0,reopened=0;
   for(unsigned n=0;n<rate/5;++n){const auto y=v.process({std::uint8_t(n%17?0x7f:0xff),255},{4.9,true},{2.5,true},{5,true},{},false,{n%17?.02:-.02,true},route!=0);check(std::isfinite(y),"finite open");if(n>rate/10)open+=y*y;}
   for(unsigned n=0;n<rate/10;++n){const auto y=v.process({0,255},{0,true},{2.5,true},{5,true},{},false,{0,true},route!=0);check(std::isfinite(y),"finite release");if(n>rate/20)closed=std::max(closed,std::abs(y));}
   for(unsigned n=0;n<rate/10;++n){const auto y=v.process({0,255},{4.9,true},{2.5,true},{5,true},{},false,{0,true},route!=0);check(std::isfinite(y),"finite reopen");if(n>rate/20)reopened+=y*y;}
   std::cout<<"rate="<<rate<<" route="<<route<<" model="<<model<<" open-energy="<<open<<" closed-peak="<<closed<<" reopened-energy="<<reopened<<'\n';
   check(open>1e-12,"must exercise audible resonant voice");
#ifndef VCA_EXPECT_OLD_BUG
   check(closed<1e-12,"resonant voice must mute after ACV release");
#else
   check(closed>1e-6,"old implementation must reproduce leaking selfoscillation");
#endif
   check(reopened>1e-12,"VCF selfoscillation must survive VCA closure");
   v.reset();check(v.process({0,255},{0,true},{2.5,true},{5,true})==0,"reset closes voice");
  }
 }
 std::cout<<"PASS VCA board and all3 resonant models close/reopen at both rates\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
