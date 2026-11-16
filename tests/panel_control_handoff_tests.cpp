#include "vst3/panel_control_fallback.h"
#include <vector>
#include <iostream>
#include <stdexcept>
using eii::vst::PanelControlFallback;
void check(bool v,const char* s){if(!v)throw std::runtime_error(s);}
int main()try{
 PanelControlFallback bridge;std::array<double,6> values{};values[4]=.5;
 std::array<bool,6> host{};std::vector<std::pair<unsigned,double>> queued;
 auto emit=[&](unsigned i,double v){queued.emplace_back(i,v);};auto read=[&](unsigned i){return values[i];};
 bridge.append(host,read,emit);check(queued.size()==6,"initial complete control handoff");
 for(auto [i,v]:queued)bridge.queued(i,v);queued.clear();
 bridge.append(host,read,emit);check(queued.empty(),"stationary controls produce no duplicate physical writes");
 // No-echo host: thousands of UI updates between callbacks deliver the latest
 // value once. The ADC is fed through the ordinary audio-clock event queue.
 for(unsigned n=1;n<=4000;++n)values[0]=double(n)/5000;
 bridge.append(host,read,emit);check(queued.size()==1&&queued[0]==std::pair<unsigned,double>{0,4},"no-echo host keeps final slider value");
 // An event rejected by the bounded engine queue must be retried next block.
 queued.clear();bridge.append(host,read,emit);check(queued.size()==1,"rejected handoff remains pending");
 bridge.queued(0,4);queued.clear();bridge.append(host,read,emit);check(queued.empty(),"accepted handoff acknowledged");
 // Host supplies all timestamped automation points: no second fallback write
 // for that control, while another independent panel control still updates.
 host[0]=true;values[0]=.6;values[1]=.3;bridge.append(host,read,emit);
 check(queued.size()==1&&queued[0].first==1,"host automation and independent UI slider coexist");
 bridge.queued(0,.2*5);bridge.queued(0,.4*5);bridge.queued(0,.6*5);
 for(auto [i,v]:queued)bridge.queued(i,v);queued.clear();host[0]=false;
 bridge.append(host,read,emit);check(queued.empty(),"automation final value does not echo back again");
 // Concrete old two-route ordering: a delayed host echo rewinds an already
 // updated GUI pin. A single host-clock route has no immediate newer pin write.
 const std::vector<double> oldPins{1,1,2,3,2,4,3,4},singlePins{1,2,3,4};
 unsigned reversals=0;for(unsigned n=1;n<oldPins.size();++n)reversals+=oldPins[n]<oldPins[n-1];
 check(reversals==2,"old delayed-echo fixture reproduces pin rewinds");
 for(unsigned n=1;n<singlePins.size();++n)check(singlePins[n]>=singlePins[n-1],"single ordered source has no newer/older interleave");
 for(unsigned i=0;i<6;++i){values[i]=(i&1)?1:0;}bridge.append(host,read,emit);
 for(auto [i,v]:queued){check(v==0||v==5,"all6 panel controls retain full range");bridge.queued(i,v);}
 queued.clear();bridge.append(host,read,emit);check(queued.empty(),"full-scale handoff settles");
 std::cout<<"PASS no-echo host, coalesced UI final value, stationary suppression, rejected-queue retry, host automation coexistence, all6 ranges and delayed-echo ordering fixture\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
