#pragma once
#include <bitset>
#include "digital_power_on.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace eii {
struct LogicLevel { bool known=false,high=false;
    bool operator==(const LogicLevel& b) const noexcept {return known==b.known && (!known || high==b.high);}
};
// Output channels 0..7, schematics 7-44..7-51: HC393 and HCT74.
// Settled logic only; ripple propagation/glitches and setup/hold are absent.
class ChannelRequestTiming {
public:
    LogicLevel request{};
    std::uint64_t requestClocks=0,ambiguousRequestClocks=0,lastRequestClock=0;
    ChannelRequestTiming(bool sampling=false): sampling_(sampling) { unknownDivider(); }
    void initializePowerOn(DigitalPowerOnBits& bits,LogicLevel timer) noexcept {
        unsigned second=(bits.next()>>4)&(sampling_?15:7),first=bits.next()&15;
        if(second&stopMask())first=0;
        single_=int((second<<4)|first);states_.reset();
        request={true,bool(bits.next()&1)};timer_=timer;ack_={true,true};
        this->timer(timer);
    }
    // Electrical -CHAK level, NOT a CPU port or invented controller ACK.
    void acknowledge(bool high) noexcept {
        acknowledge(LogicLevel{true,high});
    }
    void acknowledge(LogicLevel level) noexcept {
        ack_=level;
        if(level.known && !level.high) request={true,false};
        else if(!level.known && (!request.known || request.high)) request.known=false;
    }
    void timer(LogicLevel level) noexcept {
        timer_=level;
        if(!level.known) {unknownDivider();return;}
        if(!level.high) {
            if(single_>=0) {single_&=15;return;}
            std::bitset<256> next;
            for(unsigned s=0;s<256;++s) if(states_[s]) next.set(s&15);
            states_=next;collapse();
        }
    }
    void fallingSourceClock(std::uint64_t timestamp) noexcept {
        if(single_<0 || !timer_.known) {fallingUnresolved(timestamp);return;}
        const auto next=transitionTable[sampling_][timer_.high][unsigned(single_)];
        single_=next&255;
        const bool edge=next&256;
        if(edge){++requestClocks;lastRequestClock=timestamp;}
        if(ack_.known&&!ack_.high){request={true,false};return;}
        if(edge){if(ack_.known)request={true,true};else request.known=false;}
    }
    // Next observable CHRQ event; divider phase itself is synchronized in bulk
    // before a public observation or a timer OUT transition. Unknown histories
    // retain the original per-edge transitions until they converge.
    std::uint64_t clocksUntilRequestEvent() const noexcept {
        if(!timer_.known)return unknownTimerNeedsClock()?1:UINT64_MAX;
        if(single_<0)return stoppedHistory()?UINT64_MAX:1;
        const unsigned s=unsigned(single_);
        if(!timer_.high || ((s>>4)&stopMask()) || s>=64)return UINT64_MAX;
        return 64-s;
    }
    bool unknownTimerNeedsClock() const noexcept {
        return !timer_.known && request.known && !(ack_.known && !ack_.high) &&
            !(ack_.known && ack_.high && request.high);
    }
    std::uint64_t clocksUntilPhaseEvent() const noexcept {
        if(!timer_.known||single_<0)return 1;
        if(!timer_.high||unsigned(single_)>=64)return UINT64_MAX;
        return 16-(unsigned(single_)&15);
    }
    // Channel 0 also decodes ISMPL at second-counter state5 (80..95),
    // AFTER the playback phi1/phi2 window. A sampling observer must keep
    // materializing this history through the end of that pulse; the audio
    // observer above may stop at64 because its phase outputs stay low.
    std::uint64_t clocksUntilSamplingEvent() const noexcept {
        if(!sampling_)return clocksUntilPhaseEvent();
        if(!timer_.known||single_<0)return 1;
        if(!timer_.high||unsigned(single_)>=128)return UINT64_MAX;
        return 16-(unsigned(single_)&15);
    }
    void advanceSourceClocks(std::uint64_t clocks,std::uint64_t firstTimestamp) noexcept {
        if(clocks && timer_.known && timer_.high && single_<0 && stoppedHistory())return;
        while(clocks && (single_<0 || !timer_.known)) {
            fallingSourceClock(firstTimestamp++);--clocks;
            if(!timer_.known)return; // unresolved timer holds divider history
        }
        if(!clocks)return;
        const unsigned old=unsigned(single_);
        if(!timer_.high) {single_=int(((old&15)+clocks)&15);return;}
        if((old>>4)&stopMask()) {single_=int(old&240);return;}
        const auto stop=std::uint64_t(stopMask()*16);
        const auto next=std::min<std::uint64_t>(stop,old+std::min(clocks,stop));
        single_=int(next);
        if(old<64 && next>=64) {
            ++requestClocks;lastRequestClock=firstTimestamp+(64-old)-1;
            if(ack_.known&&!ack_.high)request={true,false};
            else if(ack_.known)request={true,true};else request.known=false;
        }
    }
    bool acknowledgeMatches(LogicLevel level) const noexcept {return ack_==level;}
    bool dividerKnown() const noexcept {return single_>=0;}
    unsigned firstCount() const noexcept {return single_>=0?unsigned(single_)&15:0;}
    unsigned secondLowBits() const noexcept {return single_>=0?unsigned(single_)>>4:0;}
    LogicLevel phi1() const noexcept {return phase(false);}
    LogicLevel phi2() const noexcept {return phase(true);}
    LogicLevel samplingPulse() const noexcept {
        if(!sampling_) return {true,false};
        return decode(2);
    }
private:
    bool stoppedHistory() const noexcept {
        // Every surviving history is asynchronously held with first counter0.
        // This is a fixed point even when the second counter remains unknown.
        static const auto stopped=[] {
            std::array<std::bitset<256>,2> masks;
            for(unsigned sampling=0;sampling<2;++sampling)
                for(unsigned second=0;second<(sampling?16u:8u);++second)
                    if(second&(sampling?8u:4u))masks[sampling].set(second<<4);
            return masks;
        }();
        return (states_ & ~stopped[sampling_]).none();
    }
    void fallingUnresolved(std::uint64_t timestamp) noexcept {
        // An unknown timer cannot establish a divider phase. Keep a
        // conservative set instead of choosing a power-up count of zero.
        if(!timer_.known) {
            if(!(ack_.known && !ack_.high) &&
               !(ack_.known && ack_.high && request.known && request.high))
                request.known=false;
            return;
        }
        bool some=false,all=true;
        if(single_>=0) {
            bool edge=false;single_=transition(unsigned(single_),edge);
            some=all=edge;
        } else {
            std::bitset<256> next;
            for(unsigned s=0;s<256;++s) if(states_[s]) {
                bool edge=false;next.set(transition(s,edge));some|=edge;all&=edge;
            }
            states_=next;collapse();
        }
        if(all) {++requestClocks;lastRequestClock=timestamp;}
        else if(some) ++ambiguousRequestClocks;
        if(ack_.known && !ack_.high) {request={true,false};return;}
        if(!some) return;
        if(ack_.known && ack_.high) {
            if(all) request={true,true};
            else if(!request.known || !request.high) request.known=false;
        } else request.known=false; // controller clear input still unknown
    }
    std::bitset<256> states_;
    int single_=-1;
    LogicLevel timer_{},ack_{};
    bool sampling_=false;
    unsigned stopMask() const noexcept {return sampling_?8u:4u;}
    void unknownDivider() noexcept {
        single_=-1;states_.reset();
        // Channels 1..7 stop on Q2; sampling channel 0 stops on Q3.
        // Retain only connected bits of the second half.
        for(unsigned second=0;second<(sampling_?16u:8u);++second)
            for(unsigned first=0;first<((second&stopMask())?1u:16u);++first)
                states_.set((second<<4)|first);
    }
    // Small immutable truth table for the original HC393 wiring transition.
    // It contains no ROM/media data and retains the asynchronous stop/reset.
    static constexpr auto transitionTable=[] {
        std::array<std::array<std::array<std::uint16_t,256>,2>,2> table{};
        for(unsigned sampling=0;sampling<2;++sampling)for(unsigned timer=0;timer<2;++timer)
            for(unsigned state=0;state<256;++state) {
                unsigned first=state&15,second=state>>4;
                const bool oldQ2=second&4;
                if(!timer)second=0;
                const unsigned stop=sampling?8:4;
                if(second&stop)first=0;
                else {first=(first+1)&15;if(!first&&timer)second=(second+1)&(sampling?15:7);}
                const bool edge=!oldQ2&&(second&4);
                if(second&stop)first=0;
                table[sampling][timer][state]=std::uint16_t((second<<4)|first|(unsigned(edge)<<8));
            }
        return table;
    }();
    unsigned transition(unsigned state,bool& rising) const noexcept {
        const auto next=transitionTable[sampling_][timer_.high][state];
        rising=bool(next&256);return next&255;
    }
    void collapse() noexcept {
        if(states_.count()!=1) return;
        for(unsigned s=0;s<256;++s) if(states_[s]) {single_=int(s);return;}
    }
    LogicLevel phase(bool secondPhase) const noexcept {return decode(secondPhase?1:0);}
    LogicLevel decode(unsigned which) const noexcept {
        auto high=[this,which](unsigned s) {
            const unsigned q=s>>4;
            if(which==2) return bool(q&1) && !(q&2) && bool(q&4);
            return bool(q&1) && (bool(q&2)==bool(which)) && (!sampling_ || !(q&4));
        };
        if(single_>=0) return {true,high(unsigned(single_))};
        bool zero=false,one=false;
        for(unsigned s=0;s<256;++s) if(states_[s]) {if(high(s))one=true;else zero=true;}
        return {!(zero&&one),one};
    }
};
}
