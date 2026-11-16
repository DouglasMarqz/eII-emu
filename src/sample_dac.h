#pragma once
#include "controller_timing_types.h"
#include "channel_service.h"
#include <array>

namespace eii {
// 7-41,7-44..51: the channel latch clocks on RISING electrical -CHAK.
// Channel0 IC43 HCT374 output enable (+MODE) belongs to the later SAR/
// analog model; these are stored eight-bit codes, not driven DAC voltages.
class SampleDacRegisters {
public:
    void initializePowerOn(DigitalPowerOnBits& bits,unsigned channel,LogicLevel enable) noexcept {
        for(auto& code:codes)code={std::uint8_t(bits.next()),255};
        levels_={std::uint8_t(255^(enable.high?(1u<<((channel+7)&7)):0)),255};
    }
    std::array<KnownByte,8> codes{};
    bool sameClockState(const SampleDacRegisters& b) const noexcept {return levels_==b.levels_;}
    void mergeOutputs(const SampleDacRegisters& b) noexcept {
        for(unsigned i=0;i<8;++i)codes[i]=merge(codes[i],b.codes[i]);
    }
    void drive(KnownByte select,LogicLevel enable,KnownByte chdb) noexcept {
        KnownByte next{255,255};
        if(!enable.known || enable.high) {
            if((select.known&7)==7) {
                const unsigned mask=1u<<((select.value+7)&7);
                next.value&=~mask;
                if(!enable.known)next.known&=~mask;
            } else {
                ChannelServiceBus decoder;decoder.drive(select.value,select.known,enable);
                next={0,0};
                for(unsigned i=0;i<8;++i) {
                    if(decoder.acknowledge[i].high)next.value|=1u<<i;
                    if(decoder.acknowledge[i].known)next.known|=1u<<i;
                }
            }
        }
        if(next==levels_)return;
        for(unsigned i=0;i<8;++i) {
            const auto old=byte(levels_,i),level=byte(next,i);
            const bool certain=old.known&&!old.high&&level.known&&level.high;
            const bool possible=(!old.known||!old.high)&&(!level.known||level.high);
            if(certain)codes[i]=chdb;
            else if(possible)codes[i]=merge(codes[i],chdb);
        }
        levels_=next;
    }
private:
    KnownByte levels_{};
    static LogicLevel byte(KnownByte b,unsigned bit) noexcept {return {bool(b.known&(1u<<bit)),bool(b.value&(1u<<bit))};}
    static KnownByte merge(KnownByte a,KnownByte b) noexcept {
        return {a.value,std::uint8_t(a.known&b.known&~(a.value^b.value))};
    }
};
}
