#pragma once
// Concrete (production) CPU request pipeline of the upper controller path.
//
// Reference CpuRequestHandshake keeps LogicLevel masks so it can represent the
// power-on ambiguity of DONE and the completion flip-flop. On the production
// path every level is known, so done/waitQ/requestInactive/completion are plain
// bools and the "possible rise" merge branches disappear.
#include <cstdint>
#include "digital_power_on.h"
#include "controller_timing_types.h"

namespace eii::production {
class RequestHandshake {
public:
    bool done=false;             // IC137 completion flip-flop
    bool waitQ=false;            // IC92 wait flip-flop
    bool requestInactive=true;   // external request pin
    bool completion=false;       // last completion clock level
    void initializePowerOn(DigitalPowerOnBits& bits,LogicLevel completionLevel) noexcept {
        done=false;waitQ=(bits.next()&1)!=0;requestInactive=true;completion=completionLevel.high;
    }
    void drive(bool inactive) noexcept {
        requestInactive=inactive;
        if(inactive)done=false;
    }
    bool requestToPipeline() const noexcept {return done||requestInactive;}
    bool completionKnown() const noexcept {return true;}
    void completionClock(bool level) noexcept {
        if(requestInactive)done=false;
        else if(!completion && level)done=true;
        completion=level;
    }
    void cpuClock() noexcept {waitQ=!done && !requestInactive;}
    bool waitInactive() const noexcept {return !waitQ;}
};
}
