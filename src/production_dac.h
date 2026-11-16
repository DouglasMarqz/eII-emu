#pragma once
// Concrete (production) IC43..50 sample/channel DAC registers.
//
// Reference SampleDacRegisters keeps KnownByte codes and a masked level byte so
// unknown selections can merge. On the production path the selection, enable
// and CHDB data are known, so codes are bytes and the level byte is a plain
// uint8_t; the "possible" capture branches disappear.
#include <array>
#include <cstdint>
#include "digital_power_on.h"

namespace eii::production {
// 7-41, 7-44..51: the channel latch clocks on RISING electrical -CHAK.
class DacRegisters {
public:
    std::array<std::uint8_t,8> codes{};
    std::array<bool,8> codeKnown{true,true,true,true,true,true,true,true};
    std::uint8_t levels=0xff;
    void initializePowerOn(std::uint8_t channel,std::uint8_t* powerOnBits) noexcept {
        for(unsigned i=0;i<8;++i){codes[i]=powerOnBits[i];codeKnown[i]=true;}
        levels=powerOnBits[8];  // already the final level byte
        (void)channel;
    }
    // Same eight code bits as SampleDacRegisters::initializePowerOn; the level
    // byte is derived from the known selection, not an extra power-on bit.
    void initializePowerOn(DigitalPowerOnBits& bits,unsigned channel,bool enable) noexcept {
        for(unsigned i=0;i<8;++i){codes[i]=std::uint8_t(bits.next());codeKnown[i]=true;}
        levels=std::uint8_t(255^(enable?(1u<<((channel+7)&7)):0u));
    }
    KnownByte code(unsigned channel) const noexcept {
        return channel<8?KnownByte{codes[channel],std::uint8_t(codeKnown[channel]?255:0)}:KnownByte{};
    }
    void drive(std::uint8_t select,bool enable,std::uint8_t chdb,bool chdbKnown=true) noexcept {
        std::uint8_t next=0xff;
        if(enable) {
            // A fully known selection takes the reference's direct branch:
            // one channel bit cleared, the rest high.
            const unsigned mask=1u<<((select+7)&7);
            next=std::uint8_t(next&~mask);
        }
        if(next==levels)return;
        for(unsigned i=0;i<8;++i) {
            const bool old=(levels>>i)&1,level=(next>>i)&1;
            if(!old&&level){codes[i]=chdb;codeKnown[i]=chdbKnown;}
        }
        levels=next;
    }
};
}
