#pragma once
#include "z80ctc.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <limits>

// Thin accessors to the pinned vendor implementation, defined beside CHIPS_IMPL.
extern "C" std::uint64_t eii_ctc_io(z80ctc_t*,std::uint64_t);
extern "C" std::uint64_t eii_ctc_zero(z80ctc_t*,unsigned,std::uint64_t);
extern "C" std::uint64_t eii_ctc_irq(z80ctc_t*,std::uint64_t);

namespace eii {
// Exact event clock for the pinned CTC. CPU bus and counter-input edges remain
// observable on every CPU boundary. Timer prescalers/downcounters materialize
// only at an underflow, I/O access or explicit inspection boundary.
class CtcEventRuntime {
public:
    void reset(const z80ctc_t& ctc,std::uint64_t now=0) noexcept {
        now_=now;last_.fill(now);initialized_=true;rebuild(ctc);irqChanged(ctc);
    }
    bool requestedBeforeService() const noexcept {return requested_;}
    void flush(z80ctc_t& ctc) noexcept {
        if(!initialized_)return;
        auto pins=ctc.pins;
        advanceTimers(ctc,now_,pins);schedule(ctc);
        ctc.pins=pins;
    }
    std::uint64_t tick(z80ctc_t& ctc,std::uint64_t pins,std::uint64_t now) noexcept {
        if(!initialized_)reset(ctc,now-1);
        assert(now==now_+1);
        if((pins&(Z80CTC_CE|Z80CTC_IORQ|Z80CTC_M1))==(Z80CTC_CE|Z80CTC_IORQ)) {
            // Vendor order is I/O first, then this clock's timer/input edges.
            advanceTimers(ctc,now-1,pins);
            pins=eii_ctc_io(&ctc,pins);
            last_.fill(now-1);rebuild(ctc);irqChanged(ctc);
        }
        pins&=~(Z80CTC_ZCTO0|Z80CTC_ZCTO1|Z80CTC_ZCTO2);
        if(now>=nextTimer_) {advanceTimers(ctc,now,pins);schedule(ctc);}
        auto changed=(pins^observed_)&monitor_;
        while(changed) {
            const auto bit=std::countr_zero(changed);
            const std::uint64_t mask=std::uint64_t(1)<<bit;
            changed&=~mask;
            const unsigned index=bit-std::countr_zero(std::uint64_t(Z80CTC_CLKTRG0));
            auto& ch=ctc.chn[index];const bool high=pins&mask;
            ch.ext_trigger=high;observed_=(observed_&~mask)|(pins&mask);
            if(ch.trigger_edge!=high)continue;
            if(ch.control&Z80CTC_CTRL_MODE_COUNTER) {
                if(--ch.down_counter==0){pins=eii_ctc_zero(&ctc,index,pins);irqChanged(ctc);}
            } else if(ch.waiting_for_trigger) {
                ch.waiting_for_trigger=false;ch.down_counter=ch.constant;
                last_[index]=now;rebuild(ctc);
            }
        }
        // No CTC interrupt-state transition is possible between these events.
        // Retain the vendor daisy implementation for ACK, RETI and new requests.
        if(anyIrq_) {
            if((pins&Z80CTC_RETI) || needed_ ||
               (pins&(Z80CTC_M1|Z80CTC_IORQ))==(Z80CTC_M1|Z80CTC_IORQ)) {
                pins=eii_ctc_irq(&ctc,pins);irqChanged(ctc);
            } else if(pins&Z80CTC_IEIO)pins&=~Z80CTC_IEIO;
        }
        now_=now;ctc.pins=pins;return pins;
    }
private:
    std::array<std::uint64_t,4> last_{};
    std::uint64_t now_=0,nextTimer_=UINT64_MAX,monitor_=0,observed_=0;
    unsigned timers_=0;
    bool initialized_=false,anyIrq_=false,needed_=false,requested_=false;
    void irqChanged(const z80ctc_t& ctc) noexcept {
        anyIrq_=needed_=requested_=false;bool blocked=false;
        for(const auto& ch:ctc.chn) {
            anyIrq_|=ch.int_state!=0;needed_|=bool(ch.int_state&Z80CTC_INT_NEEDED);
            if(ch.int_state&Z80CTC_INT_SERVICED)blocked=true;
            if(!blocked && (ch.int_state&Z80CTC_INT_REQUESTED))requested_=true;
        }
    }
    void rebuild(const z80ctc_t& ctc) noexcept {
        timers_=0;monitor_=observed_=0;
        for(unsigned i=0;i<4;++i) {
            const auto& ch=ctc.chn[i];const auto mask=std::uint64_t(Z80CTC_CLKTRG0)<<i;
            if(ch.waiting_for_trigger || (ch.control&Z80CTC_CTRL_MODE_COUNTER))monitor_|=mask;
            else if(!(ch.control&(Z80CTC_CTRL_MODE|Z80CTC_CTRL_RESET|Z80CTC_CTRL_CONST_FOLLOWS)))timers_|=1u<<i;
            if(ch.ext_trigger)observed_|=mask;
        }
        schedule(ctc);
    }
    void schedule(const z80ctc_t& ctc) noexcept {
        nextTimer_=UINT64_MAX;
        for(unsigned active=timers_;active;active&=active-1) {
            const unsigned i=std::countr_zero(active);const auto& ch=ctc.chn[i];
            const unsigned divisor=unsigned(ch.prescaler_mask)+1;
            const unsigned first=(ch.prescaler&ch.prescaler_mask)?ch.prescaler&ch.prescaler_mask:divisor;
            const unsigned count=ch.down_counter?ch.down_counter:256;
            nextTimer_=std::min(nextTimer_,last_[i]+first+std::uint64_t(count-1)*divisor);
        }
    }
    void advanceTimers(z80ctc_t& ctc,std::uint64_t until,std::uint64_t& pins) noexcept {
        for(unsigned active=timers_;active;active&=active-1) {
            const unsigned i=std::countr_zero(active);auto& ch=ctc.chn[i];
            assert(until>=last_[i]);const auto clocks=until-last_[i];
            if(!clocks)continue;
            const unsigned divisor=unsigned(ch.prescaler_mask)+1;
            const unsigned first=(ch.prescaler&ch.prescaler_mask)?ch.prescaler&ch.prescaler_mask:divisor;
            const auto decrements=clocks<first?0:1+(clocks-first)/divisor;
            ch.prescaler=std::uint8_t(ch.prescaler-clocks);last_[i]=until;
            const unsigned count=ch.down_counter?ch.down_counter:256;
            assert(decrements<=count); // every underflow is an event boundary
            if(decrements==count){pins=eii_ctc_zero(&ctc,i,pins);irqChanged(ctc);}
            else ch.down_counter=std::uint8_t(count-decrements);
        }
    }
};
}
