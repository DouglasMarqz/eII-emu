#include "chip_audio_worker.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <condition_variable>
using namespace eii::analog;
int main(){
 for(unsigned used:{1u,16u,64u,96u}){
  std::mutex m;std::condition_variable cv;unsigned received=0;
  auto worker=std::make_unique<ChipAudioWorker>([&](auto pcm){std::lock_guard lock(m);received+=pcm.size();cv.notify_one();},2,true);
  NonlinearVoiceBank::Input input{}; // unknown pins: isolate queue/worker overhead, not full DSP
  const auto start=std::chrono::steady_clock::now();
  for(unsigned n=0;n<2000;++n){for(unsigned i=0;i<used;++i)worker->push(input);worker->flush();std::unique_lock lock(m);if(!cv.wait_for(lock,std::chrono::seconds(5),[&]{return received==(n+1)*used;}))return 1;}
  const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
  std::cout<<"used="<<used<<" blocks=2000 frames="<<received<<" seconds="<<seconds<<" drops="<<worker->stats().dropped<<'\n';
 }
}
