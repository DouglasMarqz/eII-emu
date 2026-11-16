// Analog drift A/B: same input through two voice banks, drift off vs on.
// usage: analog_drift_probe [s|e|l] FC_CV_VOLTS
#include "nonlinear_voice.h"
#include <chrono>
#include <cstdio>
using namespace eii::analog;
int main(int argc,char**argv){const bool ssm2=argc>1&&argv[1][0]=='s',eiii=argc>1&&argv[1][0]=='e';const double fcv=argc>2?atof(argv[2]):1.0;
 PlaybackVoiceBank a(4,false,eiii,ssm2),b(4,false,eiii,ssm2);FilterCalibration c;a.calibrate(c);b.calibrate(c);b.analogDrift(true);
 PlaybackVoiceBank::Input in;for(unsigned ch=1;ch<8;++ch){in.playback.amplitudeCv[ch]={4.0,true};in.frequency[ch]={fcv,true};in.resonance[ch]={1.5,true};}
 const int N=48000*20;double d2=0,s2=0,pk=0;bool bad=false;double ta=0,tb=0;
 for(int n=0;n<N;++n){for(unsigned ch=1;ch<8;++ch)in.playback.codes[ch]={std::uint8_t((n*(ch+3)/4)&255),255};
  auto t0=std::chrono::steady_clock::now();float x=a.process(in);auto t1=std::chrono::steady_clock::now();float y=b.process(in);auto t2=std::chrono::steady_clock::now();
  ta+=std::chrono::duration<double>(t1-t0).count();tb+=std::chrono::duration<double>(t2-t1).count();
  if(!std::isfinite(y))bad=true;if(n>48000){d2+=double(x-y)*(x-y);s2+=double(x)*x;pk=std::max(pk,double(std::abs(y)));}}
 std::printf("%s fcv %.1f V: diff/sig %.1f dB, peak %.3f, nan %d, cpu off %.2f s on %.2f s (+%.2f%%), dT now %.3f K\n",ssm2?"ssm2":eiii?"eiii":"legacy",fcv,10*std::log10(d2/s2),pk,bad,ta,tb,100*(tb/ta-1),b.driftKelvin()[1]);}
