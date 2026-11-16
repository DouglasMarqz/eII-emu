#pragma once
#include <algorithm>
#include <cmath>
namespace eii::vst {
// Final host output gain range (dB). The state stores dB; 0..24 since 0.5 (was 0..12).
inline constexpr double postOutputMaxDb=24;
// Last plugin stage, after the machine/DSP, output ring, SRC and volume.
// Deliberately no limiter: deliver floating-point headroom to the host.
class PostOutputGain {
public:
 void process(float* left,float* right,int n,double db,double rate)noexcept{
  if(!left||n<=0)return;
  if(!std::isfinite(db))db=0;if(!std::isfinite(rate)||rate<=0)rate=48000;
  const float target=float(std::pow(10.,std::clamp(db,0.,postOutputMaxDb)/20));
  const float alpha=float(-std::expm1(-1/(.005*rate)));
  for(int i=0;i<n;++i){gain_+=alpha*(target-gain_);left[i]*=gain_;if(right&&right!=left)right[i]*=gain_;}
 }
private:float gain_=1;
};
}
