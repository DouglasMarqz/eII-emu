// Sample input step 3: real Z80 IN 08h..0Fh through the 7-43 SAR logic,
// -IWAIT pacing (7-10 -PWAIT) and IMODE on PIO B7. Synthetic program, no ROMs.
#include "machine.h"
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <memory>
static int failures=0;
static void require(bool c,const char* what){if(!c){std::cerr<<"FAIL: "<<what<<"\n";++failures;}}
static std::unique_ptr<eii::Machine> make(bool comparatorHigh){
    auto m=std::make_unique<eii::Machine>();
    const std::uint8_t code[]={
        0x31,0x00,0x24,                                                 // SP=2400h (SRAM)
        0x3e,0x34,0xd3,0x1f, 0x3e,0x68,0xd3,0x1c, 0x3e,0x01,0xd3,0x1c, // 8254 IC3 C0 mode2 = 360
        0x3e,0xcf,0xd3,0x33, 0x3e,0x7f,0xd3,0x33,                       // PIO B mode3, B7 output
        0x3e,0x80,0xd3,0x32,                                            // B7=1: IMODE (sampling)
        0x06,0x00, 0x10,0xfe,                                           // settle: DJNZ 256
        0x21,0x00,0x21, 0x06,0x10, 0x0e,0x08,                          // HL=2100h B=16 C=08h
        0xed,0xa2, 0xe3,0xe3, 0x20,0xfa,                                // INI; EX(SP),HL x2; JR NZ (66T, like OS3.1 6E1F)
        0x3e,0x00,0xd3,0x32,                                            // IMODE=0
        0x76};
    std::copy(std::begin(code),std::end(code),m->main.rom.begin());
    m->sampleComparatorExternal=true;m->sampleComparator={true,comparatorHigh};
    return m;
}
int main(){
    for(bool high:{true,false}) {
        auto m=make(high);
        m->run(60000);
        const std::uint8_t want=high?0xff:0x7f; // IDATA constant: sign then magnitude per 7-43 XOR
        bool all=true;for(unsigned i=0;i<16;++i)all&=m->memory[0x2100+i]==want;
        require(all,"16 INI bytes equal the SAR result for a constant comparator");
        require(m->sampleReads==16 && m->sampleUnknownReads==0,"16 known reads");
        require(m->sampleWaitClocks>0,"reads stretched by -IWAIT");
        require(m->sampleSar.conversions>=16,"one conversion per ISMPL");
        require(m->unmappedIo==0,"IN 08h is decoded (-PCSRIN), not unmapped");
        const auto mode=m->channel0Mode();require(mode.known&&!mode.high,"IMODE returned to 0");
        std::printf("comparator=%d byte=%02x reads=%llu waits=%llu conversions=%llu\n",high,m->memory[0x2100],
            (unsigned long long)m->sampleReads,(unsigned long long)m->sampleWaitClocks,(unsigned long long)m->sampleSar.conversions);
    }
    {   // Pacing: a loop longer than the EOC-high window (36us period - 27us
        // conversion) but shorter than a period reads each conversion once.
        auto m=make(true);std::uint64_t first=0,last=0;
        m->trace=[&](bool s,bool w,std::uint16_t a,std::uint8_t,std::uint16_t){if(!s&&!w&&(a&0xf8)==0x08){if(!first)first=m->ticks;last=m->ticks;}};
        m->run(60000);
        if constexpr(eii::diagnosticsEnabled) require(last-first>=14*144,"reads paced by conversions (360/10MHz = 144 CPU ticks)");
        std::printf("read span %llu ticks\n",(unsigned long long)(last-first));
    }
    {   // IMODE=0: reads are never held; the idle register is unknown -> FFh, counted.
        auto m=std::make_unique<eii::Machine>();
        const std::uint8_t code[]={0x3e,0xcf,0xd3,0x33,0x3e,0x7f,0xd3,0x33,0x3e,0x00,0xd3,0x32,0x06,0x00,0x10,0xfe,0xdb,0x08,0x32,0x00,0x21,0x76};
        std::copy(std::begin(code),std::end(code),m->main.rom.begin());m->run(20000);
        require(m->memory[0x2100]==0xff && m->sampleUnknownReads==1 && m->sampleWaitClocks==0,"IMODE=0 read not held; unknown bits exposed as 1 and counted");
    }
    std::cout<<(failures?"FAIL":"PASS")<<": sample input machine wiring (IN 08h, -IWAIT, IMODE)\n";
    return failures?1:0;
}
