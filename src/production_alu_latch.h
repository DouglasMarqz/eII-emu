#pragma once
// Concrete (production) ALU address latch (IC152/IC123 path).
//
// Reference ControllerAluLatch keeps a LogicLevel clock and a Known24 output so
// an unknown clock level can merge. On the production path the clock level is
// known: a low->high edge latches the candidate, everything else holds.
#include <cstdint>
#include "digital_power_on.h"

namespace eii::production {
class AluLatch {
public:
    std::uint32_t output=0;
    bool clock=false;
    void initializePowerOn(DigitalPowerOnBits& bits,bool clockLevel) noexcept {
        output=bits.next()&0xffffff;clock=clockLevel;
    }
    bool mayCapture() const noexcept {return !clock;}
    void driveClock(bool next,std::uint32_t candidate) noexcept {
        if(!clock&&next)output=candidate&0xffffff;
        clock=next;
    }
};
}
