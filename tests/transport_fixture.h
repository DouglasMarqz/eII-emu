#pragma once
#include "prom.h"
static eii::PromLatch transportProm(){
    eii::PromLatch p;
    for(unsigned a=0;a<512;++a) {
        const unsigned phase=(a>>4)&7,feedback=a>>7;
        unsigned u=0;
        if(phase==0 && !(a&8))u=64;
        if(phase==1 && feedback==1)u=88;
        if(phase==2 && feedback==1)u=(a&4)?94:90;
        if(phase==4 && !(a&2))u=128;
        if((phase==5||phase==6) && feedback==2)u=152;
        if(phase==7 && feedback==2)u=1;
        p.upper[a]=std::uint8_t(u);
        // Read RFA=(CHSL*4)+1; address SUM is B+0. Gated ALUGO
        // captures each source rise. Slot8 permits staged CPU SRAM writes.
        p.lower[a]=std::uint8_t(0xc1|(((a&256)&&((a>>5)&7)==2)?4:0));
    }
    return p;
}
