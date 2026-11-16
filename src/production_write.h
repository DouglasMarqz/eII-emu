#pragma once
// Concrete (production) IC92 register-write handshake and IC115 write enable.
//
// Reference RegisterWriteHandshake keeps LogicLevel masks because -MPWEN can be
// uncertain; on the production path every level is known, so pending/flag/mpwenN
// are plain bools and the "possibly low" merge branches disappear.
namespace eii::production {
class WriteHandshake {
public:
    bool pending=false;   // first flip-flop
    bool flag=false;      // second flip-flop
    bool mpwenN=true;     // gated -MPWEN, active low
    void settle(bool channel3,bool state3) noexcept {
        mpwenN=!(channel3&&state3&&flag);
        if(!mpwenN)pending=false;
    }
    void cpuStrobe() noexcept {pending=mpwenN;}
    void sourceClock() noexcept {flag=pending;}   // two inversions, OLD pending
};
// IC115 AND followed by two IC67 NANDs; active-high write aperture.
inline bool writeEnable(bool clockHigh,bool mpwenN,bool channel3,bool ucnt2,bool ucd2) noexcept {
    const bool middle=!(mpwenN&&channel3&&!ucnt2);
    return !clockHigh&&middle&&ucd2;
}
}
