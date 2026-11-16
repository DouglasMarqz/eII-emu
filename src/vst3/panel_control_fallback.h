#pragma once
#include <array>
#include <algorithm>
namespace eii::vst {
// Audio-thread-only handoff. UI changes stay in the atomic parameter values;
// host automation retains every sample offset. Hosts that do not echo UI edits
// get one fallback value at the next block boundary, through the SAME queue.
class PanelControlFallback {
public:
 static constexpr unsigned controls=6;
 template<class Read,class Emit>
 void append(const std::array<bool,controls>& hostPoints,Read&& read,Emit&& emit)const {
  for(unsigned i=0;i<controls;++i){const double volts=std::clamp(read(i),0.0,1.0)*5.0;
   if(!hostPoints[i]&&(!known_[i]||volts!=last_[i]))emit(i,volts);
  }
 }
 void queued(unsigned i,double volts)noexcept{if(i<controls){last_[i]=volts;known_[i]=true;}}
private:
 std::array<double,controls> last_{};std::array<bool,controls> known_{};
};
}
