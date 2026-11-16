#include "chip_audio_worker.h"
#include <future>
#include <iostream>
#include <memory>
#include <vector>
#include <bit>
using namespace eii::analog;
int main(){
 for(bool legacy:{false,true})for(unsigned factor:{2u,4u}){
  NonlinearVoiceBank reference(factor,legacy);
  std::promise<std::vector<float>> delivered; auto result=delivered.get_future();
  auto worker=std::make_unique<ChipAudioWorker>([&](auto pcm){delivered.set_value(std::vector<float>(pcm.begin(),pcm.end()));},factor,legacy);
  unsigned sequence=0;
  for(unsigned used:{96u,1u,95u,2u,64u,16u,3u,96u,1u}){
   auto calibration=FilterCalibration{};calibration[FilterParam::OutputGain]=sequence%2?.7:1.;
   worker->calibrate(calibration);reference.calibrate(calibration);
   std::vector<float> expected;
   for(unsigned i=0;i<used;++i){
    NonlinearVoiceBank::Input input{}; input.channel0Playback=true;
    for(unsigned ch=0;ch<8;++ch){input.playback.codes[ch]={std::uint8_t((sequence*17+i*3+ch)&255),255};input.playback.amplitudeCv[ch]={2.5,true};input.frequency[ch]={2.0,true};input.resonance[ch]={1.0,true};}
    expected.push_back(reference.process(input));worker->push(input);
   }
   worker->flush();
   if(result.wait_for(std::chrono::seconds(10))!=std::future_status::ready)return 1;
   const auto actual=result.get();if(actual.size()!=expected.size())return 2;
   for(unsigned i=0;i<used;++i)if(std::bit_cast<unsigned>(actual[i])!=std::bit_cast<unsigned>(expected[i]))return 3;
   delivered=std::promise<std::vector<float>>{};result=delivered.get_future();++sequence;
  }
  if(worker->stats().dropped||worker->stats().failures)return 4;
 }
 std::cout<<"PASS: mixed partial/full blocks, wrap, calibration, bit-exact PCM, both models/rates; Input bytes="<<sizeof(NonlinearVoiceBank::Input)<<" block frame bytes="<<96*sizeof(NonlinearVoiceBank::Input)<<"\n";
}
