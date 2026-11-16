#include "production_controller.h"
#include "reference/production_controller_057.h"
#include <iostream>
#include <vector>
#include <stdexcept>
static void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
static auto state(const auto& c){std::vector<std::uint64_t> a={c.control,c.phase,c.channel,c.shift,c.captured,c.pipeline,c.acknowledgeEnable,c.lowAperture,c.readInactive,c.requestInactive,c.resetHeld,c.dramActive,c.dramRead,c.invalid,c.invalidCode,c.risingEdges,c.promClocks,c.registerWrites,c.lastRfd,c.lastLowerWord,c.firstFailure.edge,c.firstFailure.control,c.firstFailure.phase,c.firstFailure.channel};
 auto add=[&](auto x){a.push_back(x);};
 for(unsigned i=0;i<64;++i)add(c.registers.read(i));
 add(c.staging.address);add(c.staging.data);for(auto n:c.staging.writes)add(n);
 add(c.writePort.pending);add(c.writePort.flag);add(c.writePort.mpwenN);add(c.aluOutput.output);add(c.aluOutput.clock);
 add(c.data.low20);add(c.data.flags);add(c.data.ucd4b);add(c.data.ucd9);
 auto& u=c.upper;add(u.upper);add(u.pipeline);add(u.request);add(u.ack);add(u.delayed9);add(u.cpu.done);add(u.cpu.waitQ);add(u.cpu.requestInactive);add(u.cpu.completion);
 add(u.dram.readLatch);add(u.dram.channelLatch);add(u.dram.readKnown);add(u.dram.channelKnown);add(u.dram.mdoKnown);add(u.dram.readReady);add(u.dram.writeReady);add(u.dram.reads);add(u.dram.writes);add(u.dram.readCaptures);add(u.dram.writeAddress);add(u.dram.writeData);add(u.dram.memoryFault);add(u.dram.memoryFaultAddress);add(u.dram.memoryFaultCpu);
 auto address=u.dram.aluOutputForTest();add(address.value);add(address.known);add(u.dram.rowValidForTest());add(u.dram.casAddressValidForTest());add(u.dram.rasForTest());add(u.dram.casForTest());add(u.dram.rasQForTest());add(u.dram.casQForTest());add(u.dram.rowCpuForTest());add(u.dram.mdoForTestValue());add(u.dram.mdoCurrentForTest());
 for(unsigned i=0;i<8;++i){add(u.dac.codes[i]);add(u.dac.codeKnown[i]);}add(u.dac.levels);return a;}
int main()try{
 unsigned r=0x652718;auto rnd=[&](){r^=r<<13;r^=r>>17;r^=r<<5;return r;};std::vector<std::uint8_t> memory(0x80000,0),known(0x80000,255);std::uint64_t quiet=0,edges=0,activeEdges=0,batched=0,spans=0;
 for(unsigned trial=0;trial<64;++trial){eii::PromLatch p;p.upper.fill(0);p.lower.fill(0x4a);if(trial%4==1)p.lower[trial*3]=0x6b;if(trial%4==2)p.upper[trial*3]=2;
  eii::production::Controller a;eii::production_controller_reference_057::Controller b;a.loadProms(p);b.loadProms(p);
  a.attachCpuDram(memory.data(),known.data());b.attachCpuDram(memory.data(),known.data());eii::CpuRegisterStaging sa,sb;auto seed=rnd();a.initializePowerOn(seed,sa);b.initializePowerOn(seed,sb);
  a.control=b.control=0x4a;a.invalid=b.invalid=false;a.invalidCode=b.invalidCode=0;a.firstFailure={};b.firstFailure={};a.phase=b.phase=10;a.channel=b.channel=trial%8;a.shift=b.shift=0;a.aluOutput.clock=b.aluOutput.clock=true;
  a.upper.upper=b.upper.upper=0;a.upper.pipeline=b.upper.pipeline=3;a.upper.request=b.upper.request=false;a.upper.ack=b.upper.ack=false;a.upper.delayed9=b.upper.delayed9=false;
  a.registers.fill(0xf00000);b.registers.fill(0xf00000);std::array<bool,8> request{};
  for(unsigned n=0;n<16000;++n){
   if(n%31==0){bool inactive=n%93!=0;bool read=rnd()&1;a.driveCpuBus({true,inactive},{true,read});b.driveCpuBus({true,inactive},{true,read});a.cpuClock();b.cpuClock();}
   if(n%79==0){auto address=rnd()&0x7ffff;auto value=std::uint8_t(rnd());bool active=rnd()&1,read=rnd()&1;a.driveDramCpu(active,address,{value,255},read);b.driveDramCpu(active,address,{value,255},read);}
   if(n%107==0){request[rnd()%8]=rnd()%8==0;}
   if(trial>=32 && n%113==0){eii::CpuRegisterStaging v;v.address={std::uint8_t(rnd()%64),63};v.data={rnd()&0xffffff,0xffffff};a.cpuRegisterWrite(v,true);b.cpuRegisterWrite(v,true);}
   if(trial>=32 && n%2048==2016){a.reset(true);b.reset(true);}
   if(trial>=32 && n%2048==2020){a.reset(false);b.reset(false);}
   if(n%127==0){auto address=std::uint8_t(rnd()%64);auto value=rnd()&0xffffff;a.registers.write(address,value);b.registers.write(address,value);}
   if(!a.invalid)++activeEdges;
   const auto count=a.advanceQuietClocks(rnd()%9);
   if(count){for(unsigned k=0;k<count;++k){b.rising(request,0);b.falling();}batched+=count;++spans;edges+=count;require(state(a)==state(b),"batched controller differs from original edge oracle");}
   else {a.rising(request,0);b.rising(request,0);require(state(a)==state(b),"rising controller mismatch");a.falling();b.falling();require(state(a)==state(b),"falling controller mismatch");++edges;}
  }quiet+=a.quietSourceClocks;
 }
 require(quiet>1000 && batched>1000 && batched>spans,"multi-period span path not exercised");std::cout<<"PASS: batched-period vs frozen full-edge oracle,edges="<<edges<<" batched="<<batched<<" spans="<<spans<<" active="<<activeEdges<<" quiet="<<quiet<<"; ROM variations,CPU requests/register writes,reset,ninth slots,DRAM bus,timer requests,RF updates and physical observations exact\n";
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
