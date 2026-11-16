#include "machine.h"
#include <iostream>
#include <stdexcept>
using namespace eii;
void check(bool b,const char*s){if(!b)throw std::runtime_error(s);}
void write(z80ctc_t& c,unsigned ch,unsigned data){eii_ctc_io(&c,Z80CTC_IEIO|Z80CTC_CE|Z80CTC_IORQ|std::uint64_t(ch)*Z80CTC_CS0|(std::uint64_t(data)<<16));}
int main()try{
 for(bool eventRuntime:{false,true}){
  auto m=std::make_unique<Machine>(0);m->enableRuntimeDevices(eventRuntime);
  auto& c=m->scanner.ctc;
  write(c,0,0xe0);write(c,0,0x05);write(c,0,1); // free timer, ZC0 pulses every16clocks
  write(c,1,0xc7);write(c,1,4); // interrupt counter onfalling inputedges
  m->enableRuntimeDevices(eventRuntime); // rebuild cached configuration after direct test programming
  m->run(4096);
#ifdef EXPECT_OLD_SCANNER_CLOCK_BUG
  check(c.chn[1].int_state!=0,"old selfclock must reproduce spurious CT1 IRQ");
#else
  check(c.chn[1].int_state==0&&c.chn[1].down_counter==4,"idle SMPTE input must ignore internal ZC0 activity");
  for(unsigned edge=0;edge<3;++edge){m->scannerSmpteClockHigh=false;m->run(8);m->scannerSmpteClockHigh=true;m->run(8);}
  check(c.chn[1].int_state==0&&c.chn[1].down_counter==1,"only external falling edges decrement CT1");
  m->scannerSmpteClockHigh=false;m->run(8);
  check(c.chn[1].int_state!=0,"fourth external fallingedge requests CT1 IRQ");
#endif
  std::cout<<"PASS scannerCT1 externalclock wiring, eventRuntime="<<eventRuntime<<'\n';
 }
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
