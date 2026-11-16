#pragma once
#include "controller_timing_types.h"
#include <cstdint>

namespace eii {
inline LogicLevel sampleXor(LogicLevel a,LogicLevel b) noexcept {return {a.known&&b.known,a.high!=b.high};}
// Output board 7-43 (service PDF105) "SAR and logic" with IC11 MC14559B.
// Gate-level wiring read from the rendered sheet:
//   IC10a 74HCT74: /PRE = NOT +ISMPL, D = GND, CLK = SAR Q6, /CLR = +5V.
//   N = IC8 AND(IC10a.Q, +IMODE) -> SC.  ENCODE = AND(NOT EOC, NOT N).
//   DATA = IDATA XOR AND(NOT Q7, NOT N).
//   IC10b: CLK = EOC, D = +5V, /CLR = AND(+IMODE, -PCSRIN); -INPRDY = /Q.
//   -IWAIT = OR(EOC, NOT IMODE, -PCSRIN).  IC12 drives +IDAC7..0 when IMODE=1;
//   IC13 drives PD7..0 while -PCSRIN is low (IN 08h..0Fh, 7-11).
// MC14559B (onsemi MC14549B/D): positive-edge clocked; a positive SC
// transition seen at a clock edge (or SC=1 with EOC=1, recirculation) loads
// Q7=1, Q6..0=0, EOC=0. Each later edge conditionally resets the bit under
// trial when D=0 and sets the next lower bit. EOC rises on the falling edge
// after Q0's decision. While idle with SC=0 nothing changes. An unknown
// power-up register has no stable EOC=0 state: after nine clocks with SC
// known it is idle with EOC=1 (data bits unknown), or, with SC=1, restarts.
// Ideal settled logic at CPU-tick resolution; HCT/CMOS propagation, setup
// and hold and the LS92 clock phase relative to other clocks are NOT modeled.
class SampleSar {
public:
    struct Inputs {
        LogicLevel imode{},ismpl{},pcsrinN{true,true},comparator{};
    };
    // C333K = IC91 74LS92 Q3, CPU clock /12 (7-30). Chosen phase: rising at
    // tick%12==0, falling at tick%12==6 (power-up phase unknown).
    static constexpr unsigned clockDivider=12;

    KnownByte register_() const noexcept {return q_;}
    LogicLevel eoc() const noexcept {return eoc_;}
    LogicLevel startLevel(LogicLevel imode) const noexcept {return logicAnd(ic10a_,imode);}
    LogicLevel encode(LogicLevel imode) const noexcept {return logicAnd(logicNot(eoc_),logicNot(startLevel(imode)));}
    LogicLevel readyN() const noexcept {return logicNot(ic10b_);}
    // -IWAIT low (asserted) only while reading in sampling mode before EOC.
    LogicLevel waitN(LogicLevel imode,LogicLevel pcsrinN) const noexcept {
        return logicOr(logicOr(eoc_,logicNot(imode)),pcsrinN);
    }
    LogicLevel ic10a() const noexcept {return ic10a_;}
    bool converting() const noexcept {return known_&&bit_>=0;}
    bool stateKnown() const noexcept {return known_;}
    // Nothing can change while IMODE is known low, the register is idle and
    // the flip-flops are settled: the caller may skip clocks exactly.
    bool quiescent(LogicLevel imode) const noexcept {
        return imode.known&&!imode.high&&known_&&bit_<0&&!eocPending_&&
            eoc_.known&&eoc_.high&&ic10b_.known&&!ic10b_.high&&sc_.known&&!sc_.high;
    }
    // IC10a history while the caller skipped quiescent clocks.
    void presetHistory(LogicLevel preset) noexcept {
        if(preset.known&&preset.high)ic10a_={true,true};
        else if(!preset.known&&!(ic10a_.known&&ic10a_.high))ic10a_={};
    }
    std::uint64_t conversions=0,unknownDecisions=0;

    // Asynchronous preset/clear and level-sensitive nets.
    void settle(const Inputs& in) noexcept {
        if(in.ismpl.known&&in.ismpl.high)ic10a_={true,true};
        else if(!in.ismpl.known&&!(ic10a_.known&&ic10a_.high))ic10a_={};
        const auto clear=logicAnd(in.imode,in.pcsrinN); // IC10b /CLR
        if(clear.known&&!clear.high)ic10b_={true,false};
        else if(!clear.known&&!(ic10b_.known&&!ic10b_.high))ic10b_={};
    }
    // Off-grid -PCSRIN strobe: IC10b /CLR = IMODE AND -PCSRIN (level, idempotent).
    void readStrobe(LogicLevel imode) noexcept {
        const auto clear=logicAnd(imode,{true,false});
        if(clear.known&&!clear.high)ic10b_={true,false};
        else if(!clear.known&&!(ic10b_.known&&!ic10b_.high))ic10b_={};
    }
    void risingClock(const Inputs& in) noexcept {
        settle(in);
        const auto n=startLevel(in.imode);
        const auto oldQ6=bitLevel(6);
        const KnownByte oldQ=q_;
        if(!n.known) {
            unknownState(); // a possible SC edge may (re)start the register
        } else if(!known_) {
            // Datasheet bound: an unknown register reaches EOC=1 within nine
            // clocks and then recirculates if SC=1. The model resolves the
            // unknown power-up phase at that bound (a chosen schedule).
            if(n.high&&sc_.known&&!sc_.high)start();
            else if(++unknownClocks_>=9) {
                if(n.high)start();else {known_=true;bit_=-1;eocPending_=true;}
            }
        } else if(n.high && ((sc_.known&&!sc_.high) || (eoc_.known&&eoc_.high))) {
            start();
        } else if(n.high && !sc_.known) {
            unknownState();
        } else if(bit_>=0) {
            // DATA = IDATA XOR (NOT Q7 AND NOT N), N known here.
            const auto s=logicAnd(logicNot(bitLevel(7)),logicNot(n));
            const auto d=sampleXor(in.comparator,s);
            const std::uint8_t m=std::uint8_t(1u<<bit_);touched_=true;
            if(!d.known){q_.known&=std::uint8_t(~m);++unknownDecisions;}
            else if(!d.high)q_.value&=std::uint8_t(~m);
            if(bit_>0){const std::uint8_t lower=std::uint8_t(m>>1);q_.value|=lower;q_.known|=lower;}
            if(--bit_<0)eocPending_=true;
        }
        sc_=n;
        const auto newQ6=bitLevel(6);
        // IC10a CLK = Q6: a rising edge loads D=GND unless preset holds it.
        // An idle register keeps its (possibly unknown) Q6: no clock edge.
        const bool updated=!(oldQ==q_)||oldQ.known!=q_.known||touched_;touched_=false;
        if(updated&&!(in.ismpl.known&&in.ismpl.high)) {
            const bool possible=!(oldQ6.known&&oldQ6.high)&&!(newQ6.known&&!newQ6.high);
            const bool certain=oldQ6.known&&!oldQ6.high&&newQ6.known&&newQ6.high;
            if(certain)ic10a_={true,false};
            else if(possible&&!(ic10a_.known&&!ic10a_.high))ic10a_={};
        }
        settle(in);
    }
    void fallingClock(const Inputs& in) noexcept {
        settle(in);
        if(eocPending_) {
            eocPending_=false;
            const auto old=eoc_;eoc_={true,true};
            if(known_&&bit_<0&&old.known&&!old.high)++conversions;
            // IC10b CLK = EOC rising, D = +5V, unless /CLR is low.
            const auto clear=logicAnd(in.imode,in.pcsrinN);
            if(!(clear.known&&!clear.high)) {
                if(old.known&&!old.high)ic10b_={true,true};
                else if(!old.known&&!(ic10b_.known&&ic10b_.high))ic10b_={};
            }
        }
    }
    // One CPU tick at absolute tick count t (after the tick's other devices).
    void cpuTick(std::uint64_t t,const Inputs& in) noexcept {
        const unsigned phase=unsigned(t%clockDivider);
        if(phase==0)risingClock(in);
        else if(phase==6)fallingClock(in);
        else settle(in);
    }
private:
    KnownByte q_{0,0};
    LogicLevel eoc_{},sc_{},ic10a_{},ic10b_{};
    int bit_=-1;
    bool known_=false,eocPending_=false;
    unsigned unknownClocks_=0;
    LogicLevel bitLevel(unsigned b) const noexcept {
        return {bool(q_.known&(1u<<b)),bool(q_.value&(1u<<b))};
    }
    bool touched_=false; // register clocked this edge (start/decision/unknown)
    void start() noexcept {
        touched_=true;q_={0x80,0xff};eoc_={true,false};bit_=7;known_=true;eocPending_=false;unknownClocks_=0;
    }
    void unknownState() noexcept {
        touched_=true;known_=false;q_.known=0;eoc_={};bit_=-1;eocPending_=false;unknownClocks_=0;
    }
};
}
