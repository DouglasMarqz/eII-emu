#pragma once
#include <array>
#include <cstdint>

namespace eii {
// Lower the known IC49/IC68/bank-enable network to bus events. No ROM or
// timing constants are embedded: callers still supply the actual latched
// controller pins at their original timestamps. Unknown pins use the oracle.
struct DramStrobeProgram {
    static constexpr unsigned Ras=1, Cas=2, Row=4, Column=8, Cpu=16, Bank=32, Write=64;
    static constexpr auto compile() noexcept {
        std::array<std::uint8_t,256> out{};
        for(unsigned i=0;i<out.size();++i) {
            const bool we=i&1, inhibit=i&2, bankReset=i&4, sample=i&8;
            const bool rq=i&16,cq=i&32,oldRas=i&64,oldCas=i&128;
            const bool ras=!(rq&&!cq),cas=cq&&!inhibit;
            out[i]=(ras?Ras:0)|(cas?Cas:0)|(!oldRas&&ras?Row:0)|
                (!oldCas&&cas?Column:0)|(!sample?Cpu:0)|
                (!sample||!bankReset?Bank:0)|(cas&&we?Write:0);
        }
        return out;
    }
};
inline constexpr auto dramStrobeProgram=DramStrobeProgram::compile();
}
