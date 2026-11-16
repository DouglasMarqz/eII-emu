#pragma once
// Concrete (production) RFD bus and IC148/150/152 pipeline.
//
// The reference resolves three possible drivers with merge semantics and keeps
// partial-known masks. In the production runtime exactly one driver is active
// and every value is concrete, so the bus settles to that driver and the
// pipeline is a plain latch.
#include <cstdint>
#include <array>

namespace eii::production {
// Returns the settled bus value; `any` reports whether a driver was enabled.
inline std::uint32_t resolveBus(const std::uint32_t data[3],const bool enabled[3],bool& any) noexcept {
    any=false;
    for (unsigned i = 0; i < 3; ++i)
        if (enabled[i]) { any=true; return data[i]&0xffffff; }
    return 0;
}
// IC140 DB..DE: RFD23,22,21,20 -> PRECAR,LOOPMD,DECFLG,BACKMD (input flags).
// Shared exact reversal of RFD23..20: immutable physical pin permutation.
inline constexpr std::array<std::uint8_t,16> reversedRfdFlags={
    0,8,4,12,2,10,6,14,1,9,5,13,3,11,7,15};
inline std::uint8_t flagsFromBus(std::uint32_t rfd) noexcept {
    return reversedRfdFlags[(rfd>>20)&15];
}
// IC141 uses RFD22..20: the same mapped value without its RFD23 output.
inline std::uint8_t pipelineFlags(std::uint32_t rfd) noexcept {
    return std::uint8_t(flagsFromBus(rfd)>>1);
}
struct DataPipeline {
    std::uint32_t low20=0;   // IC148/150/152 RFD19..0
    std::uint8_t flags=0;    // IC141 QB/QC/QD
    bool ucd4b=false,ucd9=false;
    void clock(std::uint32_t rfd,bool rawProm4,bool oldUcd9) noexcept {
        low20=rfd&0xfffff;
        flags=pipelineFlags(rfd);
        ucd4b=rawProm4;ucd9=oldUcd9;
    }
};
}
