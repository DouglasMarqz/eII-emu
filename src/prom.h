#pragma once
#include <array>
#include <cstdint>

namespace eii {
// S-6149-001, sheet 13 (manual 7-18). Electrical input levels, not
// asserted/deasserted booleans: mprq_n/mprd_n are the negative-polarity nets.
struct PromInputs {
    std::uint8_t counter=0;
    bool channel3=false,ureq=false,mprq_n=true,mprd_n=true;
    bool preCarry=false,loop=false,decrement=false,backwards=false;
};
class PromLatch {
public:
    std::array<std::uint8_t,512> upper{},lower{};
    std::uint16_t latched=0;
    std::uint16_t upperAddress(const PromInputs& in) const {
        // IC135 A8/A7 are feedback from the PREVIOUS latched UCD15/UCD14,
        // not tied-high pins and not the counter's top two bits.
        return ((latched>>7)&0x180)|((in.counter&7)<<4)|
            (in.mprq_n?8:0)|(in.mprd_n?4:0)|(in.channel3?2:0)|(in.ureq?1:0);
    }
    static std::uint16_t lowerAddress(const PromInputs& in) {
        return (in.channel3?256:0)|((in.counter&7)<<5)|(in.ureq?16:0)|
            (in.preCarry?8:0)|(in.loop?4:0)|(in.decrement?2:0)|(in.backwards?1:0);
    }
    std::uint16_t clock(const PromInputs& in) {
        auto high=upper[upperAddress(in)],low=lower[lowerAddress(in)];
        latched=(std::uint16_t(high)<<8)|low;return latched;
    }
};
}
