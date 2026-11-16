#pragma once
// Concrete (production) 20-bit controller ALU.
//
// Reference: sheet7-22 IC123..127, B+A+CI where one UCD04.1A wire drives both
// A19..10 AND A7..0 (one continuous wire); UCD04.1B drives A9/A8.
// The carry leaves the sum
// field and becomes RFD23. With a fully known operand and known control pins
// this is a single add, no compatible-assignment enumeration.
#include <cstdint>

namespace eii::production {
inline std::uint32_t addConstant(std::uint32_t operand20,unsigned addend) noexcept {
    const unsigned sum=(operand20&0xfffff)+addend;
    return (sum&0xfffff)|((sum&0x100000)?0x800000u:0u);
}
inline std::uint32_t alu(std::uint32_t operand20,bool ucd3,bool ucd4a,bool ucd4b) noexcept {
    return addConstant(operand20,(ucd4a?0xffcffu:0u)+(ucd4b?0x300u:0u)+unsigned(ucd3));
}
// RFD22/21/20 are not sum bits: they carry the delayed flags and the DECCTL
// term (IC48/115/117: RFD21 = !DECFLG XOR (!captured3 & captured1 & captured4)).
// The known path overlays them on the arithmetic result exactly like
// controllerAlu()'s put() calls.
inline std::uint32_t aluOutput(std::uint32_t operand20,bool ucd3,bool ucd4a,bool ucd4b,
                               std::uint8_t delayedFlags,std::uint8_t captured) noexcept {
    std::uint32_t out=alu(operand20,ucd3,ucd4a,ucd4b);
    const bool decrement=(captured&8)==0 && (captured&2)!=0 && (captured&16)!=0;
    const bool flag21=((delayedFlags&2)!=0)!=decrement;
    if(delayedFlags&1)out|=1u<<22;
    if(flag21)out|=1u<<21;
    if(delayedFlags&4)out|=1u<<20;
    return out;
}
}

