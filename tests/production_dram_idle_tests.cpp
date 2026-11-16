#include <memory>
#include "production_dram.h"
#include "reference/production_dram_057.h"
#include <iostream>
#include <stdexcept>
#include <array>
namespace eii::production {
struct ProductionDramTestAccess {
 static auto state(const DramPort& p){return std::array<std::uint64_t,22>{
 p.active_,p.cpuRead_,p.rowCpu_,p.rowValid_,p.casAddressValid_,p.mdoCurrent_,p.sourceFull_,
 p.rasQ_,p.casQ_,p.ras_,p.cas_,p.readClock_,p.channelClock_,p.cpuAddress_,p.casAddress_,
 p.cpuData_,p.mad_,p.bank_,p.row_,p.rowBank_,p.mdo_,p.aluAddress.mayCapture()};}
 static void unknownAddress(DramPort& p){p.aluAddress.output.known=0;}
 static bool idle(const DramPort& p,unsigned c){return p.idleCpuPort(c);}
};
}
namespace eii::production_reference_057 {
struct ProductionDramTestAccess {
 static auto state(const DramPort& p){return std::array<std::uint64_t,22>{
 p.active_,p.cpuRead_,p.rowCpu_,p.rowValid_,p.casAddressValid_,p.mdoCurrent_,p.sourceFull_,
 p.rasQ_,p.casQ_,p.ras_,p.cas_,p.readClock_,p.channelClock_,p.cpuAddress_,p.casAddress_,
 p.cpuData_,p.mad_,p.bank_,p.row_,p.rowBank_,p.mdo_,p.aluAddress.mayCapture()};}
 static void unknownAddress(DramPort& p){p.aluAddress.output.known=0;}
 static bool idle(const DramPort& p,unsigned c){(void)p;(void)c;return false;}
};
}
static void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main()try{
 std::uint32_t seed=0x48321;auto rnd=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
 auto heldMem=std::make_unique<std::array<std::uint8_t,0x80000>>(),heldKnown=std::make_unique<std::array<std::uint8_t,0x80000>>(); // heap: 1MB Windows stack
 auto& mem=*heldMem;auto& known=*heldKnown;for(auto& b:mem)b=std::uint8_t(rnd());known.fill(255);
 std::uint64_t eligible=0,steps=0;
 for(unsigned trial=0;trial<64;++trial){
  eii::production::DramPort fast;eii::production_reference_057::DramPort old;
  fast.memoryKnown=old.memoryKnown=known.data();
  auto init=rnd();eii::DigitalPowerOnBits a(init),b(init);fast.initializePowerOn(a,0x4a,false);old.initializePowerOn(b,0x4a,false);
  auto check=[&](){
   require(eii::production::ProductionDramTestAccess::state(fast)==eii::production_reference_057::ProductionDramTestAccess::state(old),"all internal state");
   require(fast.readLatch==old.readLatch&&fast.channelLatch==old.channelLatch&&fast.readKnown==old.readKnown&&fast.channelKnown==old.channelKnown&&fast.mdoKnown==old.mdoKnown,"latches/known");
   require(fast.readReady==old.readReady&&fast.writeReady==old.writeReady&&fast.writeAddress==old.writeAddress&&fast.writeData==old.writeData,"CPU handshake");
   require(fast.reads==old.reads&&fast.writes==old.writes&&fast.readCaptures==old.readCaptures,"event counts");
   require(fast.memoryFault==old.memoryFault&&fast.memoryFaultAddress==old.memoryFaultAddress&&fast.memoryFaultCpu==old.memoryFaultCpu,"memory faults");
   require(fast.mdoForTestValue()==old.mdoForTestValue()&&fast.mdoCurrentForTest()==old.mdoCurrentForTest(),"MDO");
   require(fast.rowValidForTest()==old.rowValidForTest()&&fast.casAddressValidForTest()==old.casAddressValidForTest()&&fast.rasForTest()==old.rasForTest()&&fast.casForTest()==old.casForTest()&&fast.rasQForTest()==old.rasQForTest()&&fast.casQForTest()==old.casQForTest()&&fast.rowCpuForTest()==old.rowCpuForTest(),"strobe/row state");
   auto x=fast.aluOutputForTest(),y=old.aluOutputForTest();require(x.value==y.value&&x.known==y.known,"ALU address");
  };
  for(unsigned n=0;n<16000;++n){
   if(n%197==17){eii::production::ProductionDramTestAccess::unknownAddress(fast);eii::production_reference_057::ProductionDramTestAccess::unknownAddress(old);}
   if(n%13==0){bool active=rnd()&1,read=rnd()&1;auto address=rnd()&0x7ffff;auto value=std::uint8_t(rnd());fast.cpuBus(active,address,value,read);old.cpuBus(active,address,value,read);}
   const unsigned controls[]={0x4a,0x4a,0x4a,0x4a,0x4a,0x4a,0x4a,0x4a,0x4a,0x9400,0xb900,0x834a,0x2100,0x100,0x80,0x200};
   unsigned c=controls[(n%96<64)?0:rnd()%16];bool delayed=(n%96<64)?false:(rnd()&1);auto sum=rnd()&0xffffff;
   eligible+=eii::production::ProductionDramTestAccess::idle(fast,c);
   fast.rising(c,sum,mem.data());old.rising(c,sum,mem.data());check();
   fast.falling(c,delayed,mem.data());old.falling(c,delayed,mem.data());check();++steps;
  }
 }
 require(eligible>100000,"idle shortcut not exercised");
 std::cout<<"PASS: frozen0.57 DRAM oracle,cycles="<<steps<<" idle-qualified="<<eligible<<"; CPU bus changes,strobe transitions,memory data/faults,latches and counts exact\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
