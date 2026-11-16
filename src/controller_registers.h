#pragma once
#include <array>
#include <cstdint>
#include "controller_timing_types.h"

namespace eii {
struct Known24 {std::uint32_t value=0,known=0;
    bool operator==(const Known24& b) const noexcept {return known==b.known && !((value^b.value)&known);}
};
inline Known24 merge24(Known24 a,Known24 b) noexcept {
    return {a.value&0xffffff,(a.known&b.known&~(a.value^b.value))&0xffffff};
}
// Sheet 7-19: IC119..122. A completed CPU OUT represents the rising
// deassertion of -PCWS0..3; this is not the later register-file write.
class CpuRegisterStaging {
public:
    KnownByte address{};
    Known24 data{};
    std::array<std::uint64_t,4> writes{};
    bool write(std::uint8_t port,std::uint8_t value) noexcept {
        if(port<0x48 || port>=0x68)return false;
        const unsigned bank=(port-0x48)>>3;
        ++writes[bank];
        if(!bank)address={std::uint8_t(value&63),63};
        else {
            const unsigned shift=(bank-1)*8,mask=255u<<shift;
            data.value=(data.value&~mask)|(unsigned(value)<<shift);
            data.known|=mask;
        }
        return true;
    }
    // IC118: CHSL3..0 -> RFA5..2; UCD01..00 -> RFA1..0.
    // -MPWEN low enables CPU latches; high enables controller buffers.
    KnownByte selectedAddress(KnownByte channel,KnownByte control,LogicLevel mpwenN) const noexcept {
        const KnownByte controller{std::uint8_t(((channel.value&15)<<2)|(control.value&3)),
                                   std::uint8_t(((channel.known&15)<<2)|(control.known&3))};
        if(mpwenN.known)return mpwenN.high?controller:address;
        return {address.value,std::uint8_t(address.known&controller.known&~(address.value^controller.value)&63)};
    }
};
// Sheet 7-20: six 2149H-3 x4 SRAMs, A9..6 grounded -> 64 x24.
// SRAM component: scheduled -RFWR and settled bus live in ControllerTiming.
// endWrite represents one completed, stable-address write aperture. An
// uncertain address/enable conservatively merges written and held states.
class ControllerRegisterFile {
public:
    void initializePowerOn(DigitalPowerOnBits& bits) noexcept {for(auto& cell:cells_)cell={bits.next()&0xffffff,0xffffff};}
    Known24 read(KnownByte address) const noexcept {
        Known24 result{};bool first=true;
        const unsigned mask=(~address.known)&63,base=address.value&address.known&63;
        unsigned subset=mask;
        do {const unsigned a=base|subset;
            if(first){result=cells_[a];first=false;}else result=merge24(result,cells_[a]);
            subset=(subset-1)&mask;
        }while(subset!=mask);
        return result;
    }
    void endWrite(KnownByte address,Known24 data,LogicLevel enabled) noexcept {
        if(enabled.known && !enabled.high)return;
        data.value&=0xffffff;data.known&=0xffffff;
        const bool certain=enabled.known && (address.known&63)==63;
        const unsigned mask=(~address.known)&63,base=address.value&address.known&63;
        unsigned subset=mask;
        do {const unsigned a=base|subset;
            cells_[a]=certain?data:merge24(cells_[a],data);
            subset=(subset-1)&mask;
        }while(subset!=mask);
    }
private:
    std::array<Known24,64> cells_{}; // unknown at power-up, no reset pin
};

// Sheet7-21 IC92: first /Q -> second D; second /Q is +MPWFG.
// No CRST connection. /CLR of the first FF is the gated -MPWEN.
class RegisterWriteHandshake {
public:
    LogicLevel pending{},flag{},mpwenN{true,true};
    void settle(LogicLevel channel3,LogicLevel state3) noexcept {
        mpwenN=logicNot(logicAnd(logicAnd(channel3,state3),flag));
        if(mpwenN.known && !mpwenN.high)pending={true,false};
        else if(!mpwenN.known && !(pending.known && !pending.high))pending.known=false;
    }
    void cpuStrobe() noexcept {
        pending=mpwenN.known?(mpwenN.high?LogicLevel{true,true}:LogicLevel{true,false}):LogicLevel{};
    }
    void sourceClock() noexcept {flag=pending;} // two inversions, OLD pending
};
// IC115 AND followed by two IC67 NANDs. Return active-high write aperture.
inline LogicLevel registerWriteEnable(LogicLevel clockHigh,LogicLevel mpwenN,
                                     LogicLevel channel3,LogicLevel ucnt2,
                                     LogicLevel ucd2) noexcept {
    const auto middle=logicNot(logicAnd(mpwenN,logicAnd(channel3,logicNot(ucnt2))));
    return logicAnd(logicAnd(logicNot(clockHigh),middle),ucd2);
}
// During /WE low the SRAM releases its I/O pins. CPU and ALU may drive
// them; no priority override. Missing ALU latch data remains unknown.
inline Known24 registerWriteData(Known24 cpu,LogicLevel mpwenN,LogicLevel aluOeN) noexcept {
    // A known enabled ALU drives unknown data; a possibly enabled ALU
    // also prevents a guaranteed CPU value (contention/unknown source).
    if(!aluOeN.known || !aluOeN.high)return {};
    if(mpwenN.known && !mpwenN.high)return cpu;
    return {}; // floating or uncertain CPU output enable
}
}
