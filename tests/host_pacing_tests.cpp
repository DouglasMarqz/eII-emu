#include "host_pacing.h"
#include <iostream>
#include <stdexcept>
using namespace std::chrono;
static void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int main(){
 eii::host::HostPacer p;const auto epoch=eii::host::HostPacer::Clock::time_point{}+seconds(1);
 p.start(epoch,4000);require(p.deadline(8000)==epoch+milliseconds(1),"4MHz conversion");
 for(unsigned n=1;n<1000;++n){auto now=epoch+milliseconds(n)+microseconds(500);require(!p.boundDebt(now,4000+4000*n),"ordinary jitter rebase");}
 require(p.deadline(4004000)==epoch+seconds(1),"jitter changed clock rate");
 const std::uint64_t unchangedTick=8000;const auto stalled=epoch+seconds(2);
 require(p.boundDebt(stalled,unchangedTick),"stall did not discard old deadline");
 require(p.deadline(unchangedTick)==stalled,"stall still demands catch-up burst");
 require(p.deadline(unchangedTick+4000)==stalled+milliseconds(1),"CPU quantum changed");
 require(p.rebases()==1&&p.debtMs()>1998,"debt not reported");
 require(!p.boundDebt(stalled,unchangedTick),"duplicate debt rebase");
 p.start(stalled,unchangedTick);require(p.rebases()==0&&p.debtMs()==0,"resume retained old debt");
 std::cout<<"PASS: host deadlines, regular jitter, bounded debt, unchanged CPU quanta, resume\n";
}
