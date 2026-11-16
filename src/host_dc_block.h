#pragma once
#include <cmath>
#include <stdexcept>
namespace eii::host {
// Host AC coupling, not an additional SSM2045 device model. Before the PCM
// limiter/queue: a modeled constant offset must not consume digital headroom
// or become a large pulse whenever an underrun inserts digital zero.
class DcBlock {
public:
 explicit DcBlock(double rate=48000,double hz=2) {
  if(!std::isfinite(rate)||rate<=0||!std::isfinite(hz)||hz<=0||hz>=rate/2)
   throw std::invalid_argument("Invalid host DC blocker");
  const double g=3.14159265358979323846*hz/rate;
  gain_=1/(1+g);pole_=(1-g)/(1+g);
 }
 double process(double x)noexcept {
  if(!primed_){previous_=x;primed_=true;return 0;}
  output_=gain_*(x-previous_)+pole_*output_;previous_=x;return output_;
 }
 double estimate()const noexcept{return previous_-output_;}
 void reset()noexcept{previous_=output_=0;primed_=false;}
private:
 double gain_=0,pole_=0,previous_=0,output_=0;bool primed_=false;
};
}
