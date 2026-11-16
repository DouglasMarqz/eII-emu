#pragma once
#include "z80.h"
#include "sio_sync_receiver.h"
#include "sio_sync_transmitter.h"
#include <array>
#include <cstdint>

namespace eii {
// SIO/2 modem path plus x1 Bisync/8-bit/no-parity CRC-16 receiver.
// x1 Bisync TX/CRC is modeled; TX IRQ, async/SDLC/other formats and Wait are unsupported.
class DiskSio {
public:
    struct Channel {
        std::array<std::uint8_t,8> wr{};
        std::uint8_t pointer=0,live=0x50,latched=0x50; // reset TxUnderrun + Sync/Hunt
        bool frozen=false,pending=false,servicing=false,rxServicing=false,txEmpty=true;
        SioSyncReceiver rx;
        SioSyncTransmitter tx;
    };
    void enableEventTiming(bool enable=true) noexcept {eventTiming_=enable;irqValid_=false;}
    std::array<Channel,2> channels{};
    std::array<std::uint64_t,2> acknowledgements{};
    std::uint64_t unsupportedReads=0,unsupportedWrites=0,resets=0,statusTransitions=0;
    void inputs(bool ctsN,bool dcdN) noexcept {
        auto& c=channels[0];
        const auto old=c.live;
        c.live=std::uint8_t((c.live&~0x28)|(ctsN?0:32)|(dcdN?0:8));
        if((c.wr[3]&32)&&((old^c.live)&8)) {
            if(dcdN){c.rx.enterHunt(true);c.live|=16;}
            else c.rx.firstArmed=true;
        }
        if(old!=c.live) {irqValid_=false;++statusTransitions;if(!c.frozen&&(c.wr[1]&1)){c.latched=c.live;c.frozen=true;c.pending=true;}}
        if(!c.frozen)c.latched=c.live;
    }
    bool sideN() const noexcept {return !(channels[0].wr[5]&128);}
    bool writeGateN() const noexcept {return !(channels[0].wr[5]&2);}
    void rxClock(bool data,unsigned n=0) noexcept {
        auto& c=channels[n&1];
        if((c.wr[3]&32)&&!(c.live&8))return; // Auto Enables: DCD must be active
        irqValid_=false;
        if(c.rx.clock(data,c.wr)){const auto old=c.live;c.live&=~16;statusChange(c,old);}
    }
    // Active-low receive Ready function only; IORQ/CE inhibit must be supplied
    // by the bus adapter during a real SIO transaction, not a CPU read shortcut.
    bool readyN(unsigned n=0,bool busSelected=false) const noexcept {
        const auto& c=channels[n&1];
        if(busSelected)return true;
        if((c.wr[1]&0xe0)==0xe0)return !c.rx.depth;
        if((c.wr[1]&0xe0)==0xc0)return !c.tx.empty;
        return true;
    }
    void write(std::uint8_t port,std::uint8_t value) noexcept {
        irqValid_=false;
        const unsigned n=(port>>1)&1;auto& c=channels[n];
        if(!(port&1)){c.tx.write(value);c.txEmpty=c.tx.empty;return;}
        if(c.pointer) {
            const unsigned reg=c.pointer;const auto previous=c.wr[reg];c.pointer=0;c.wr[reg]=value;
            if(reg==3) {
                // UM0081 printed283: setting Rx CRC Enable (D3) starts or
                // restarts the CRC calculation. Queue it before any Hunt
                // transition so entry applies it at the current boundary.
                if(value&8)c.rx.restartCrc();
                if((value&16)||((value&1)&&!(previous&1))){const auto old=c.live;c.rx.enterHunt(!(previous&1));c.live|=16;statusChange(c,old);}
                else if(!(value&1)&&(previous&1)){c.rx.enterHunt(true);c.live|=16;}
                if((value&1)&&!(previous&1))c.rx.firstArmed=true;
            }
            if(!c.frozen)c.latched=c.live;
            return;
        }
        const unsigned command=(value>>3)&7;
        if(command==2){c.frozen=false;c.pending=false;c.latched=c.live;}
        else if(command==3){c=Channel{};++resets;if(!n)for(auto& x:channels){x.servicing=false;x.rxServicing=false;}}
        else if(command==4)c.rx.firstArmed=true;
        else if(command==6){if(((c.wr[1]>>3)&3)==1&&(c.rx.status&32))c.rx.read();c.rx.errorReset();}
        else if(command==7&&!n)reti();
        else if(command && command!=5 && command!=6)++unsupportedWrites;
        if((value&0xc0)==0x80)c.tx.resetCrc();
        if((value&0xc0)==0xc0){c.tx.resetUnderrun();c.live&=~64;if(!c.frozen)c.latched=c.live;}
        if((value&0xc0)==0x40)c.rx.resetCrc();
        c.pointer=value&7;
    }
    bool txClock(unsigned n=0)noexcept{
        auto& c=channels[n&1];const auto old=c.live;const bool bit=c.tx.clock(c.wr);
        c.txEmpty=c.tx.empty;c.live=std::uint8_t((c.live&~64)|(c.tx.underrun?64:0));statusChange(c,old);return bit;
    }
    std::uint8_t read(std::uint8_t port) noexcept {
        irqValid_=false;
        const unsigned n=(port>>1)&1;auto& c=channels[n];
        if(!(port&1)){if(!c.rx.depth)++unsupportedReads;return c.rx.read(((c.wr[1]>>3)&3)==1);}
        const unsigned reg=c.pointer;c.pointer=0;
        if(!reg)return std::uint8_t((c.latched&~1)|(c.rx.depth?1:0)|(c.txEmpty?4:0)|(!n&&anyPending()?2:0));
        if(reg==1)return std::uint8_t((c.rx.status&~1)|(c.tx.allSent()?1:0)); // synchronous All Sent
        if(reg==2&&n)return vector(highest());
        ++unsupportedReads;return 255;
    }
    bool requestLine() const noexcept {return highest()>=0;}
    // Highest device in EII chain: SIO -> (future DMA) -> CTC -> PIO.
    // Do NOT put SIO INT into vendor CTC's persistent pins. Return its
    // daisy gating/vector first; Machine ORs its INT after downstream tick.
    std::uint64_t daisy(std::uint64_t pins) noexcept {
        if(eventTiming_) {refreshIrq();if(!irqWork_)return pins;}
        irqValid_=false;
        if(pins&Z80_RETI)for(auto& c:channels){if(c.rxServicing){c.rxServicing=false;pins&=~Z80_RETI;break;}if(c.servicing){c.servicing=false;pins&=~Z80_RETI;break;}}
        for(unsigned n=0;n<2&&(pins&Z80_IEIO);++n) {
            auto& c=channels[n];
            const bool rx=rxPending(c);
            if(c.rxServicing){pins&=~Z80_IEIO;break;}
            if(!rx&&c.servicing){pins&=~Z80_IEIO;break;}
            if(!rx&&(!c.pending||!(c.wr[1]&1)))continue;
            if((pins&(Z80_M1|Z80_IORQ))==(Z80_M1|Z80_IORQ)) {
                const auto v=vector(int(n)*2+(rx?0:1));Z80_SET_DATA(pins,v);if(rx)c.rxServicing=true;else c.servicing=true;++acknowledgements[n];
                pins&=~Z80_IEIO;
            } else if(!(pins&Z80_RETI))pins&=~Z80_IEIO;
            break;
        }
        return pins;
    }
private:
    bool eventTiming_=false;
    mutable bool irqValid_=false,irqWork_=false;
    mutable int irqSource_=-1;
    void refreshIrq() const noexcept {
        if(irqValid_)return;
        irqSource_=computeHighest();irqWork_=irqSource_>=0;
        for(const auto& c:channels)irqWork_|=c.rxServicing||c.servicing;
        irqValid_=true;
    }
    void reti() noexcept {for(auto& c:channels){if(c.rxServicing){c.rxServicing=false;break;}if(c.servicing){c.servicing=false;break;}}}
    static bool rxPending(const Channel& c) noexcept {return c.rx.interrupt((c.wr[1]>>3)&3);}
    void statusChange(Channel& c,std::uint8_t old) noexcept {
        irqValid_=false;
        if(old!=c.live){++statusTransitions;if(!c.frozen&&(c.wr[1]&1)){c.latched=c.live;c.frozen=true;c.pending=true;}}
        if(!c.frozen)c.latched=c.live;
    }
    int highest() const noexcept {
        if(eventTiming_){refreshIrq();return irqSource_;}
        return computeHighest();
    }
    int computeHighest() const noexcept {
        for(unsigned n=0;n<2;++n){const auto& c=channels[n];if(c.rxServicing)return -1;if(rxPending(c))return int(n)*2;if(c.servicing)return -1;if(c.pending&&(c.wr[1]&1))return int(n)*2+1;}return -1;
    }
    bool anyPending() const noexcept {for(const auto& c:channels)if(rxPending(c)||(c.pending&&(c.wr[1]&1)))return true;return false;}
    std::uint8_t vector(int source) const noexcept {
        const auto base=channels[1].wr[2];
        if(!(channels[1].wr[1]&4))return base;
        const auto code=source<0?6:(source&1)?(source<2?10:2):((source<2?12:4)|(channels[unsigned(source)/2].rx.status&32?2:0));
        return std::uint8_t((base&0xf1)|code);
    }
};
}

