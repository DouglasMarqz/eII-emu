#pragma once
#include "controller_registers.h"

namespace eii {
struct RfdDriver {Known24 data;LogicLevel enabled;};
// Settled digital bus. Conflicting or unknown potentially active drivers
// make affected bits unknown. A floating bus has no invented pull-up value.
inline Known24 resolveRfdBus(const std::array<RfdDriver,3>& drivers) noexcept {
    Known24 result{};bool first=true,certainDriver=false;
    for(const auto& d:drivers) {
        if(d.enabled.known && !d.enabled.high)continue;
        if(first){result=d.data;first=false;}else result=merge24(result,d.data);
        certainDriver|=d.enabled.known && d.enabled.high;
    }
    result.value&=0xffffff;
    result.known=certainDriver?(result.known&0xffffff):0;
    return result;
}
inline KnownByte inputFlags(Known24 rfd) noexcept {
    // IC140 DB..DE: RFD23,22,21,20 -> PRECAR,LOOPMD,DECFLG,BACKMD.
    KnownByte out{};
    for(unsigned b=0;b<4;++b){out.value|=((rfd.value>>(23-b))&1)<<b;out.known|=((rfd.known>>(23-b))&1)<<b;}
    return out;
}
struct ControllerDataPipeline {
    Known24 low20{}; // IC148/150/152 RFD19..0; top four bits unconnected here
    KnownByte flags{}; // IC141 QB/QC/QD = RFD22/21/20, no asynchronous CRST
    LogicLevel ucd4b{},ucd9{};
    void clock(Known24 rfd,LogicLevel rawProm4,LogicLevel oldUcd9) noexcept {
        low20={rfd.value&0xfffff,rfd.known&0xfffff};
        flags={std::uint8_t(((rfd.value>>22)&1) | ((rfd.value>>20)&2) | ((rfd.value>>18)&4)),
               std::uint8_t(((rfd.known>>22)&1) | ((rfd.known>>20)&2) | ((rfd.known>>18)&4))};
        ucd4b=rawProm4;ucd9=oldUcd9;
    }
};
}
