#pragma once
#include <cstdint>
#include "channel_requests.h"
namespace eii {
struct KnownByte {std::uint8_t value=0,known=0;
    bool operator==(const KnownByte& b) const noexcept {return known==b.known && !((value^b.value)&known);}
};
inline LogicLevel logicNot(LogicLevel a) noexcept {return {a.known,!a.high};}
inline LogicLevel logicAnd(LogicLevel a,LogicLevel b) noexcept {
    if((a.known && !a.high)||(b.known && !b.high))return {true,false};
    return {a.known&&b.known,a.high&&b.high};
}
inline LogicLevel logicOr(LogicLevel a,LogicLevel b) noexcept {
    return logicNot(logicAnd(logicNot(a),logicNot(b)));
}
// Main CPU sheet 7-10: completion FF and CPU-clocked wait FF. The latter
// feeds the Machine CPU DRAM/WAIT adapter when strict service is enabled.
class CpuRequestHandshake {
public:
    bool operator==(const CpuRequestHandshake&) const = default;
    void initializePowerOn(DigitalPowerOnBits& bits,LogicLevel completion) noexcept {
        done={true,false};waitQ={true,bool(bits.next()&1)};requestInactive={true,true};completion_=completion;
    }
    LogicLevel done{true,false},waitQ{},requestInactive{true,true};
    void drive(LogicLevel inactive) noexcept {
        requestInactive=inactive;
        if(inactive.known && inactive.high)done={true,false};
        else if(!inactive.known && (!done.known || done.high))done.known=false;
    }
    LogicLevel requestToPipeline() const noexcept {return logicOr(done,requestInactive);}
    bool completionKnown() const noexcept {return completion_.known;}
    void completionClock(LogicLevel level) noexcept {
        const bool possibleRise=(!completion_.known || !completion_.high) && (!level.known || level.high);
        const bool certainRise=completion_.known && !completion_.high && level.known && level.high;
        if(requestInactive.known && requestInactive.high)done={true,false};
        else if(certainRise && requestInactive.known)done={true,true};
        else if(possibleRise && !(done.known && done.high && requestInactive.known))done.known=false;
        completion_=level;
    }
    void cpuClock() noexcept {waitQ=logicAnd(logicNot(done),logicNot(requestInactive));}
    LogicLevel waitInactive(LogicLevel external={true,true}) const noexcept {
        return logicAnd(logicNot(waitQ),external);
    }
private:
    LogicLevel completion_{};
};
}
