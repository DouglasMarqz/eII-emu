#include "disk_sio.h"
#include <cassert>
#include <iostream>
#include <random>
int main() {
    eii::DiskSio a,b;b.enableEventTiming();std::mt19937 rng(0x5102026);
    auto write=[&](unsigned p,unsigned v){a.write(p,v);b.write(p,v);};
    for(unsigned t=0;t<300000;++t) {
        if(t%1000==0) {
            for(unsigned n=0;n<2;++n) {
                const unsigned p=0x39+2*n;write(p,0x18);
                for(auto rv:{std::pair{4u,0x10u},{5u,0x64u},{6u,0xfau},{7u,0x96u},{3u,0xd9u},{1u,0xfdu}}){write(p,rv.first);write(p,rv.second);}
            }
            for(unsigned i=0;i<16;++i)for(unsigned n=0;n<2;++n){bool bit=(0x96fa>>i)&1;a.rxClock(bit,n);b.rxClock(bit,n);}
        }
        const auto v=rng();const unsigned p=0x38+((v>>8)&7);
        switch(v%9) {
        case 0:write(p,v>>16);break;
        case 1:assert(a.read(p)==b.read(p));break;
        case 2:a.inputs(v&128,v&256);b.inputs(v&128,v&256);break;
        default:a.rxClock(v&128,(v>>10)&1);b.rxClock(v&128,(v>>10)&1);break;
        }
        for(unsigned repeat=0;repeat<3;++repeat) {
            assert(a.requestLine()==b.requestLine());
            std::uint64_t pins=(v&1)?Z80_IEIO:0;
            if((v>>16)%7==0)pins|=Z80_M1|Z80_IORQ;
            if((v>>16)%11==0)pins|=Z80_RETI;
            assert(a.daisy(pins)==b.daisy(pins));
            assert(a.acknowledgements==b.acknowledgements);
            assert(a.readyN()==b.readyN() && a.sideN()==b.sideN() && a.writeGateN()==b.writeGateN());
        }
    }
    std::cout<<"PASS:300000 SIO public-API receive/modem/register/IRQ events, repeated cached queries and ACK/RETI vs reference\n";
}
