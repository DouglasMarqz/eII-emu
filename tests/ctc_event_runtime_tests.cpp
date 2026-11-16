#include "ctc_event_runtime.h"
#include <random>
#include <iostream>
#include <cstring>
#include <stdexcept>
int main() {
    std::mt19937_64 random(0xc7c2026);
    z80ctc_t a{},b{};z80ctc_init(&a);b=a;eii::CtcEventRuntime events;events.reset(b);
    for(std::uint64_t tick=1;tick<=500000;++tick) {
        std::uint64_t pins=random()&0x3ffffff;
        pins|=Z80CTC_IEIO;
        if(tick%17==0)pins&=~Z80CTC_IEIO;
        if(tick%19==0)pins|=Z80CTC_RETI;
        if(tick%11==0)pins|=Z80CTC_CE|Z80CTC_IORQ;
        for(unsigned ch=0;ch<4;++ch)if(random()&1)pins|=std::uint64_t(Z80CTC_CLKTRG0)<<ch;
        const auto ref=z80ctc_tick(&a,pins),fast=events.tick(b,pins,tick);
        if(ref!=fast){std::cerr<<"pins tick="<<tick<<" ref="<<std::hex<<ref<<" fast="<<fast<<"\n";return 1;}
        if(tick%101==0 || tick==500000) {
            events.flush(b);
            if(std::memcmp(&a,&b,sizeof(a))) {
                std::cerr<<"state tick="<<tick<<"\n";
                for(unsigned ch=0;ch<4;++ch)std::cerr<<ch<<" count "<<unsigned(a.chn[ch].down_counter)<<"/"<<unsigned(b.chn[ch].down_counter)<<" pre "<<unsigned(a.chn[ch].prescaler)<<"/"<<unsigned(b.chn[ch].prescaler)<<" irq "<<unsigned(a.chn[ch].int_state)<<"/"<<unsigned(b.chn[ch].int_state)<<"\n";
                return 1;
            }
        }
    }
    z80ctc_reset(&a);b=a;events.reset(b,500000);
    std::uint64_t now=500000;
    const auto step=[&](std::uint64_t pins) {
        ++now;pins|=a.pins&Z80CTC_INT;
        assert(z80ctc_tick(&a,pins)==events.tick(b,pins,now));
        if(now%251==0){events.flush(b);assert(!std::memcmp(&a,&b,sizeof(a)));}
    };
    const auto write=[&](unsigned ch,unsigned data){step(Z80CTC_IEIO|Z80CTC_CE|Z80CTC_IORQ|std::uint64_t(ch)*Z80CTC_CS0|(std::uint64_t(data)<<16));};
    write(0,0xd0);write(0,0xa5);write(0,0); //256x256 long timer
    write(1,0xd5);write(1,17); //externally clocked counter
    write(2,0x9d);write(2,3); //timer waits for external rising trigger
    for(unsigned clock=0;clock<140000;++clock) {
        auto pins=std::uint64_t(Z80CTC_IEIO);
        if((clock/5)&1)pins|=Z80CTC_CLKTRG1;
        if(clock>=200)pins|=Z80CTC_CLKTRG2;
        if(clock%1024<7)pins&=~Z80CTC_IEIO;
        if(clock%1000==0)pins|=Z80CTC_M1|Z80CTC_IORQ;
        if(clock%1000==20)pins|=Z80CTC_RETI;
        step(pins);
    }
    events.flush(b);assert(!std::memcmp(&a,&b,sizeof(a)));
    std::cout<<"PASS:long256x256 timers, zero constants, trigger-wait/counter clocks and IRQ nesting;500000 randomized CTC clocks against pinned vendor oracle; exact pins, lazy counter/prescaler state, I/O/trigger/ACK/RETI/daisy ordering\n";
}
