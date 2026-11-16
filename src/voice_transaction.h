#pragma once
#include "controller_alu.h"
#include "controller_prom.h"
#include <limits>

namespace eii {
// Functional lower-datapath transaction for one voice slot (not slot 8).
// It computes future *values*, without touching live RF, DRAM or DAC state.
// A runtime must deliver each event at its timestamp; in particular an
// address event is not permission to pre-read a future sample from DRAM.
struct VoiceTransaction {
    enum class Kind { RegisterWrite, AddressCapture, AluCapture };
    struct Event {
        std::uint64_t halfClock=0;
        Kind kind=Kind::RegisterWrite;
        std::uint8_t slot=0;
        Known24 value{};
    };
    struct Beat {
        Known24 rfd{},alu{},sum{};
        ControllerDataPipeline data;
        KnownByte captured{},nextControl{};
    };
    std::array<Event,24> events{};
    unsigned count=0;
    std::array<Known24,4> finalRegisters{};
    const Beat& beat(unsigned phase) const noexcept {return beats_[idle_ && phase>=2?2:phase];}

    bool idle() const noexcept {return idle_;}

    // Entry is the first rising clock of a canonical voice slot: previous
    // lower control 0x4a, linked UCD04 samples, known request/counter phase.
    // Unknown RF bits retain masks. Uncertain control returns false, allowing
    // the owner to retain the reference path rather than choosing a history.
    bool compile(const VoicePrograms& programs,std::array<Known24,4> rf,
                 Known24 incomingAlu,LogicLevel request,std::uint64_t start) noexcept {
        count=0;idle_=false;
        if(!request.known || !programs.entryValid ||
           start>std::numeric_limits<std::uint64_t>::max()-14)return false;
        const auto flags=inputFlags(rf[2]);
        const KnownByte captured{std::uint8_t((flags.value<<1)|unsigned(request.high)),
                                 std::uint8_t((flags.known<<1)|1)};
        const auto& program=programs.get(captured);
        if(!program.valid)return false;
        if(program.idle) {
            // Functional hold transaction: RF2 -> pipeline, RF2+1 -> ALU,
            // RF3 -> pipeline, then RF2 held. No RF or address writes exist.
            // Six identical interior observations share one materialization.
            ControllerDataPipeline r2,r3;r2.clock(rf[2],{true,false},{true,false});
            r3.clock(rf[3],{true,false},{true,false});
            const auto sum=controllerAlu(r2,captured,{true,true},{true,false},true);
            beats_[0]={rf[2],incomingAlu,{},r2,captured,{0x6b,255}};
            beats_[1]={rf[3],sum,sum,r3,captured,{0x4a,255}};
            beats_[2]={rf[2],sum,{},r2,captured,{0x4a,255}};
            events[0]={start+2,Kind::AluCapture,3,sum};count=1;
            finalRegisters=rf;idle_=true;return true;
        }
        unsigned control=0x4a,previousControl=0;Known24 alu=incomingAlu;
        ControllerDataPipeline data;
        for(unsigned phase=0;phase<8;++phase) {
            if(phase && control==0x4a && previousControl==0x4a && program.code[phase]==0x4a) {
                beats_[phase]=beats_[phase-1];continue; // No new RF/ALU/address event.
            }
            const unsigned slot=control&3;
            const bool write=control&4;
            Known24 rfd{};bool driven=false;
            if(!write){rfd=rf[slot];driven=true;}
            if(!(control&64)){rfd=driven?merge24(rfd,alu):alu;driven=true;}
            rfd.value&=0xffffff;rfd.known=driven?rfd.known&0xffffff:0;
            // Sum uses the OLD pipeline and captured flags. Bus/write data
            // likewise uses the OLD ALU, before the simultaneous capture.
            const auto sum=(control&0xa0)?controllerAlu(data,captured,
                {true,bool(control&8)},{true,bool(control&16)},true):Known24{};
            const auto emit=[&](Kind kind,Known24 value) {
                events[count++]={start+2*phase,kind,std::uint8_t(slot),value};
            };
            if(write){rf[slot]=rfd;emit(Kind::RegisterWrite,rfd);}
            if(control&128)emit(Kind::AddressCapture,sum);
            if(control&32){alu=sum;emit(Kind::AluCapture,sum);}
            const KnownByte next{program.code[phase],255};
            data.clock(rfd,{true,bool(next.value&16)},{true,false});
            beats_[phase]={rfd,alu,sum,data,captured,next};
            previousControl=control;control=next.value;
        }
        finalRegisters=rf;return true;
    }
private:
    std::array<Beat,8> beats_{};
    bool idle_=false;
};
} // namespace eii
