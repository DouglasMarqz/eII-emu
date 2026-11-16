#pragma once
#include <array>
#include <cstdint>

namespace eii {
// Z80 SIO byte-oriented x1 Bisync/8-bit/no-parity receive subset.
// UM0081 Figure109:1 input +3 buffer clocks, then8+8 CRC clocks.
// Unsupported modes consume no fabricated characters and are counted.
// CRC accumulator: UM0081 printed283 states that setting Rx CRC Enable
// (WR3 D3) starts or restarts the CRC calculation, and printed250-251 gives
// the CPU one character time to set/reset D3 for the next transferred
// character. A D3 write therefore schedules a restart that is applied at the
// next character boundary (or at Hunt entry, where no boundary follows). The
// accumulator is otherwise retained; nothing here forces a successful CRC.
// "Restart at the beginning of the last character transferred" (printed283)
// is implemented as: the pending character is discarded, the next one starts
// the new calculation. EII firmware re-issues WR3=D9 before every Hunt, which
// is exactly this documented restart.
class SioSyncReceiver {
public:
    struct Entry {std::uint8_t data=0,status=0;bool first=false;};
    std::array<Entry,3> fifo{};
    unsigned depth=0;
    bool hunt=true,firstArmed=true,crcKnown=false;
    std::uint8_t status=64,sticky=0; // unresolved CRC reports error at byte-only RR1 boundary
    std::uint16_t crc=0;
    std::uint64_t clocks=0,matches=0,characters=0,overruns=0,emptyReads=0,unsupportedClocks=0;
    void enterHunt(bool disabledReceiver=false) noexcept {
        // Zilog1989 Q&A: CRC delay register becomes sync register and is
        // retained on Hunt; disabling/re-enabling clears that register.
        // Reissuing Enter Hunt while already hunting must not replace the
        // partly assembled sync word with stale/cleared receive registers.
        applyRestart();
        if(disabledReceiver){window_=0;windowBits_=0;shift_=0;crcDelay_=0;}
        else if(!hunt){window_=std::uint16_t(shift_|(unsigned(crcDelay_)<<8));windowBits_=16;}
        if(disabledReceiver)inputDelay_=0;
        hunt=true;delayCount_=0;
        bits_=0;crcGate_=false;completed_=0;
    }
    void resetCrc() noexcept {crc=0;crcKnown=true;crcRestart_=false;}
    // WR3 D3 set (printed283): start/restart the checker at the next
    // transferred character. Applied at that boundary by clock().
    void restartCrc() noexcept {crcRestart_=true;}
    void errorReset() noexcept {sticky=0;status&=~0x70;}
    // Returns true only on hunt -> synchronized transition.
    bool clock(bool input,const std::array<std::uint8_t,8>& wr) noexcept {
        ++clocks;
        if(!(wr[3]&1))return false;
        if((wr[4]&0xfd)!=0x10||(wr[3]&0xc0)!=0xc0||!(wr[5]&4)) {++unsupportedClocks;return false;}
        const bool delayed=inputDelay_&1;inputDelay_=std::uint8_t(input);
        if(hunt) {
            window_=std::uint16_t((window_>>1)|(delayed?0x8000:0));
            shift_=std::uint8_t(window_);crcDelay_=std::uint8_t(window_>>8);
            if(windowBits_<16)++windowBits_;
            if(windowBits_==16&&window_==std::uint16_t(wr[6]|(unsigned(wr[7])<<8))) {
                hunt=false;++matches;delay3_=0;delayCount_=0;bits_=0;
                crcGate_=false;completed_=0;return true;
            }
            return false;
        }
        const bool data=delay3_&1;delay3_=std::uint8_t((delay3_>>1)|(delayed?4:0));
        if(delayCount_<3){++delayCount_;return false;}
        const bool old=shift_&1,crcBit=crcDelay_&1;
        crcDelay_=std::uint8_t((crcDelay_>>1)|(old?128:0));
        if(crcGate_) {const bool carry=(crc&1)^crcBit;crc=std::uint16_t((crc>>1)^(carry?0xa001:0));}
        shift_=std::uint8_t((shift_>>1)|(data?128:0));
        if(++bits_==8) {
            bits_=0;++completed_;
            applyRestart();
            // Enable sampled at a character boundary controls the preceding
            // character, which will emerge from the extra8-bit CRC delay.
            crcGate_=completed_>=2&&bool(wr[3]&8);
            if(!(wr[3]&2)||(shift_!=wr[6]&&shift_!=wr[7])) {
                Entry e{shift_,std::uint8_t(!crcKnown||crc?64:0),firstArmed};firstArmed=false;++characters;
                if(depth==3){e.status|=32;fifo[2]=e;++overruns;}
                else {fifo[depth++]=e;if(depth==1)status=std::uint8_t(e.status|sticky);}
            }
        }
        return false;
    }
    std::uint8_t read(bool holdError=false) noexcept {
        if(!depth){++emptyReads;return 255;}
        const auto result=fifo[0];
        sticky|=result.status&0x30;
        if(holdError&&(status&32))return result.data;
        for(unsigned i=1;i<depth;++i)fifo[i-1]=fifo[i];
        if(--depth)status=std::uint8_t(fifo[0].status|sticky);
        else status|=sticky; // retain last RR1 when FIFO becomes empty
        return result.data;
    }
    bool interrupt(unsigned mode) const noexcept {
        return mode&&depth&&((status&32)||mode>=2||fifo[0].first);
    }
private:
    void applyRestart() noexcept {
        if(!crcRestart_)return;
        crc=0;crcKnown=true;crcRestart_=false;
    }
    std::uint16_t window_=0;
    std::uint8_t inputDelay_=0,delay3_=0,shift_=0,crcDelay_=0;
    unsigned windowBits_=0,delayCount_=0,bits_=0,completed_=0;
    bool crcGate_=false,crcRestart_=false;
};
}
