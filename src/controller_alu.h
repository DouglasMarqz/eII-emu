#pragma once
#include "diagnostics.h"
#include "controller_data.h"

namespace eii {
inline LogicLevel xorLevel(LogicLevel a,LogicLevel b) noexcept {
    return {a.known&&b.known,a.high!=b.high};
}
inline LogicLevel byteBit(KnownByte b,unsigned bit) noexcept {
    return {bool(b.known&(1u<<bit)),bool(b.value&(1u<<bit))};
}
// Sheet7-18 IC48/115/117: RFD21.1 XOR (!DECFLG & PRECAR & BACKMD).
inline LogicLevel decrementControl(KnownByte delayedFlags,KnownByte captured) noexcept {
    const auto toggle=logicAnd(logicNot(byteBit(captured,3)),
        logicAnd(byteBit(captured,1),byteBit(captured,4)));
    return xorLevel(byteBit(delayedFlags,1),toggle);
}
// Exact independent-bit20-bit word plus one fixed hardware addend (0..0x100000).
// Once controls are fixed, carry depends only on earlier operand bits and
// is independent of the current bit; AND/OR carry propagation is exact.
inline Known24 addAluConstant(Known24 b,unsigned addend) noexcept {
    if((b.known&0xfffff)==0xfffff) {
        const unsigned sum=(b.value&0xfffff)+addend;
        return {(sum&0xfffff)|((sum&0x100000)?0x800000u:0u),0x8fffff};
    }
    if(!addend)return {b.value&0xfffff,(b.known&0xfffff)|0x800000};
    // Carry into each bit is monotone in the lower operand bits. Evaluate
    // the minimum/maximum compatible words in parallel; agreeing endpoint
    // carries are therefore certain for every compatible word. XOR removes
    // the two operands from their sum, leaving the carry-in bit vector.
    const unsigned minimum=b.value&b.known&0xfffff;
    const unsigned maximum=minimum|(~b.known&0xfffff);
    const unsigned carryMin=minimum^addend^(minimum+addend);
    const unsigned carryMax=maximum^addend^(maximum+addend);
    const unsigned certainCarry=~(carryMin^carryMax);
    const unsigned sum=(b.value&0xfffff)+addend;
    return {(sum&0xfffff)|((sum&0x100000)<<3),
            (b.known&certainCarry&0xfffff)|((certainCarry&0x100000)<<3)};
}
// Sheet7-22 IC123..127:20-bit B+A+CI. One UCD04.1A wire drives BOTH
// A19..10 AND A7..0 on the same continuous vertical wire. UCD04.1B
// drives A9/A8. Enumerate at most8 compatible control assignments.
// The carry leaves the sum field and becomes RFD23, not RFD20.
inline Known24 controllerAlu(const ControllerDataPipeline& data,KnownByte captured,
                             LogicLevel ucd3,LogicLevel ucd4a,bool sameUcd4Sample=false) noexcept {
    const auto b=data.low20;auto ucd4b=data.ucd4b;
    // Caller supplies this relation only for jointly sampled, compatible pins.
    if(sameUcd4Sample) {
        if(ucd4a.known)ucd4b=ucd4a;
        else if(ucd4b.known)ucd4a=ucd4b;
    }
    Known24 out{};
    if(ucd3.known&&ucd4a.known&&ucd4b.known) {
        out=addAluConstant(b,(ucd4a.high?0xffcff:0)+(ucd4b.high?0x300:0)+unsigned(ucd3.high));
    } else {
        bool first=true;
        for(unsigned controls=0;controls<8;++controls) {
            // Only after BOTH physical latches have sampled the same edge.
            // Their independent power-up contents must remain unconstrained.
            if(sameUcd4Sample && bool(controls&2)!=bool(controls&4))continue;
            if((ucd3.known&&ucd3.high!=bool(controls&1))||
               (ucd4a.known&&ucd4a.high!=bool(controls&2))||
               (ucd4b.known&&ucd4b.high!=bool(controls&4)))continue;
            const unsigned addend=((controls&2)?0xffcff:0)+((controls&4)?0x300:0)+(controls&1);
            const auto candidate=addAluConstant(b,addend);
            if(first){out=candidate;first=false;}else out=merge24(out,candidate);
        }
    }
    const auto put=[&](unsigned bit,LogicLevel value){if(value.high)out.value|=1u<<bit;if(value.known)out.known|=1u<<bit;};
    put(22,byteBit(data.flags,0));put(21,decrementControl(data.flags,captured));put(20,byteBit(data.flags,2));
    return out;
}
// IC99 clocks all three S374s with NAND(UCD05.1,-C10MHz.1).
// No reset; preserve the possibility of a rising gated clock at startup.
class ControllerAluLatch {
public:
    bool operator==(const ControllerAluLatch&) const = default;
    void initializePowerOn(DigitalPowerOnBits& bits,LogicLevel clock) noexcept {output={bits.next()&0xffffff,0xffffff};clock_=clock;}
    Known24 output{};
    bool mayCapture() const noexcept {return !clock_.known||!clock_.high;}
#if EII_DIAGNOSTICS
    // Slot-test precondition: start the latch in an explicit, fully known state.
    void seedForTest(Known24 value,LogicLevel clock={true,false}) noexcept {output=value;clock_=clock;}
    LogicLevel clockForTest() const noexcept {return clock_;}
#endif
    // Commit an already evaluated transaction's simultaneous rising edge.
    // The transaction owner proves the preceding clock/control sequence.
    void materialize(Known24 value,LogicLevel clock) noexcept {output=value;clock_=clock;}
    void driveClock(LogicLevel next,Known24 candidate) noexcept {
        const bool possible=(!clock_.known||!clock_.high)&&(!next.known||next.high);
        const bool certain=clock_.known&&!clock_.high&&next.known&&next.high;
        if(certain)output=candidate;
        else if(possible)output=merge24(output,candidate);
        clock_=next;
    }
private:
    LogicLevel clock_{};
};
}

