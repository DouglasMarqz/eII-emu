#include <memory>
#include "filter_calibration.h"
#include "nonlinear_voice.h"
#include "chip_audio_worker.h"
#include <iostream>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <stdexcept>
#include <future>
#include <atomic>
#include <vector>
using namespace eii::analog;
void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int main()try{
 FilterCalibration a;require(FilterCalibration::parse(a.serialize())==a,"roundtrip");
 static_assert(legacyFilterParameterCount==54);
 auto tuned=a;tuned[FilterParam::VcaGain]=9;tuned[FilterParam::NativeOutDb]=20;tuned[FilterParam::NativeCsample]=680;
 const auto old=FilterCalibration::parse(tuned.serialize(true));
 require(old[FilterParam::VcaGain]==9&&old[FilterParam::NativeOutDb]==0,"V1 migrates chip tuning with nominal native defaults");
 require(tuned.importProfile(old.serialize(true))[FilterParam::NativeOutDb]==20,"importing V1 preserves existing native tuning");
 auto restored=tuned;restored.resetNativeDefaults();
 require(restored[FilterParam::VcaGain]==9&&restored[FilterParam::NativeOutDb]==0&&restored[FilterParam::NativeCsample]==470,"native reset preserves chip/VCA tuning");
 tuned[FilterParam::S3528Override]=1;tuned.resetNativeDefaults();require(tuned[FilterParam::S3528Override]==1,"native reset must not change S3528 override");
 tuned.resetGroupDefaults(6);require(tuned[FilterParam::S3528Override]==0,"S3528 reset restores firmware cutoff");
 {auto c=a;c[FilterParam::S3528Code]=35.5;bool rejected=false;try{c.validate();}catch(...){rejected=true;}require(rejected,"S3528 fractional code rejected");}
 {auto text=a.serialize();text.erase(text.rfind("S3528Code="));bool rejected=false;try{FilterCalibration::parse(text);}catch(...){rejected=true;}require(rejected,"partial V2 profile rejected");}
 for(unsigned k=0;k<filterParameters.size();++k){auto c=a;auto d=filterParameters[k];c.values[k]=d.hi;if(k==unsigned(FilterParam::I16))c[FilterParam::I15]=0;if(k==unsigned(FilterParam::I15))c[FilterParam::I16]=0;c.validate();require(FilterCalibration::parse(c.serialize())==c,"range roundtrip");}
 for(auto text:{a.serialize()+"Qscale=1\n",std::string("EII_FILTER_CALIBRATION=9\n"),std::string("EII_FILTER_CALIBRATION=1\nQscale=nan\n")}){bool rejected=false;try{FilterCalibration::parse(text);}catch(...){rejected=true;}require(rejected,"invalid profile accepted");}
 {auto c=a;c[FilterParam::I16]=1;bool rejected=false;try{c.validate();}catch(...){rejected=true;}require(rejected,"total VCA limit");}
 NonlinearPlaybackVoice voice(96000);voice.calibrate(a);double before=0,after=0;
 for(unsigned n=0;n<19200;++n){auto y=voice.process({std::uint8_t((n/80)%2?0x3f:0xbf),255},{2.5,true},{2,true},{0,true});if(n>9600)before+=y*y;}
 auto c=a;c[FilterParam::OutputGain]=.1;voice.calibrate(c);
 for(unsigned n=0;n<19200;++n){auto y=voice.process({std::uint8_t((n/80)%2?0x3f:0xbf),255},{2.5,true},{2,true},{0,true});require(std::isfinite(y),"nonfinite live calibration");if(n>9600)after+=y*y;}
 require(after<before*.02,"live gain trim did not reach VCA path");
 auto manual=a;manual[FilterParam::FcOverride]=1;manual[FilterParam::QOverride]=1;manual[FilterParam::AcvOverride]=1;manual[FilterParam::AcvManual]=2.5;voice.calibrate(manual);
 double manualEnergy=0;for(unsigned n=0;n<2000;++n){auto y=voice.process({0xbf,255},{0,false},{0,false},{0,false});require(std::isfinite(y),"manual override finite");manualEnergy+=y*y;}require(manualEnergy>1e-12,"explicit pin overrides must not depend on firmware CV known bits");require(voice.process({0,0},{0,false},{0,false},{0,false})==0,"unknown sample guard remains active with overrides");
 NonlinearVoiceBank::Input in;in.channel0Playback=true;for(unsigned ch=0;ch<8;++ch){in.playback.codes[ch]={0xbf,255};in.playback.amplitudeCv[ch]={2.5,true};in.frequency[ch]={2.5,true};in.resonance[ch]={3.125,true};}
 std::mutex mutex;std::condition_variable cv;unsigned frames=0;
 auto heldWorker=std::make_unique<ChipAudioWorker>([&](auto block){std::lock_guard lock(mutex);for(float v:block)require(std::isfinite(v),"worker finite");frames+=unsigned(block.size());cv.notify_all();},2);auto& worker=*heldWorker; // heap: 1MB Windows stack
 for(unsigned n=0;n<96;++n)worker.push(in);worker.calibrate(c);for(unsigned n=0;n<96;++n)worker.push(in);worker.flush();
 {std::unique_lock lock(mutex);require(cv.wait_for(lock,std::chrono::seconds(10),[&]{return frames>=192;}),"DSP deadline");}
 auto stats=worker.stats();require(stats.calibrationRevision==1&&!stats.failures&&!stats.dropped&&stats.solverSamples,"live revision stats");
 // Deliberately overload a stalled consumer; retained frames must continue
 // the pre-gap integrator state, rather than restart all eight filters.
 std::promise<void> entered,release;auto gate=release.get_future().share();std::atomic<bool> first=true;
 std::vector<float> actual;std::mutex gapMutex;std::condition_variable gapCv;
 auto quiet=in;for(auto& cv:quiet.playback.amplitudeCv)cv={.25,true};for(auto& q:quiet.resonance)q={0,true};
 {
  auto heldGap=std::make_unique<ChipAudioWorker>([&](auto pcm){bool wait=first.exchange(false);{std::lock_guard lock(gapMutex);actual.insert(actual.end(),pcm.begin(),pcm.end());gapCv.notify_all();}if(wait){entered.set_value();gate.wait();}},2);auto& gapWorker=*heldGap;
  for(unsigned n=0;n<96;++n)gapWorker.push(quiet);
  require(entered.get_future().wait_for(std::chrono::seconds(10))==std::future_status::ready,"gap consumer start");
  for(unsigned n=0;n<96*12;++n)gapWorker.push(quiet);
  const auto dropped=gapWorker.stats().dropped;release.set_value();require(dropped==384,"bounded overload drops");
  {std::unique_lock lock(gapMutex);require(gapCv.wait_for(lock,std::chrono::seconds(10),[&]{return actual.size()==864;}),"gap recovery deadline");}
 }
 auto heldReference=std::make_unique<NonlinearVoiceBank>(2);auto& reference=*heldReference;reference.calibrate(FilterCalibration{});float last=0,start=0;
 for(unsigned n=0;n<864;++n){float expected=reference.process(quiet);if(n==96)start=last;if(n>=96&&n<160){float w=float(n-95)/64;expected=start*(1-w)+expected*w;}require(std::abs(actual[n]-expected)<1e-6,"DSP gap reset integrator state");last=expected;}
 std::cout<<"PASS: profile roundtrip/ranges/rejection,VCA total limit,live signal trim,queued calibration and overload state continuity\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
