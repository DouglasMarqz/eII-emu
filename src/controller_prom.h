#pragma once
#include <array>
#include <cstdint>
#include "controller_timing_types.h"

namespace eii {
struct KnownWord {std::uint16_t value=0,known=0;};
// Complete consensus table for a nine-input, eight-output PROM. Ternary
// input digits: 0=unknown, 1=known low, 2=known high. No firmware embedded.
class PromConsensus {
public:
    void load(const std::array<std::uint8_t,512>& bytes) noexcept {
        weights_.fill(0);
        unsigned power=1;
        for(unsigned bit=0;bit<9;++bit) {
            for(unsigned value=0;value<512;++value)
                if(value&(1u<<bit)) weights_[value]+=power;
            power*=3;
        }
        for(unsigned index=0;index<entries_.size();++index) {
            unsigned digits=index,value=0,known=0;
            for(unsigned bit=0;bit<9;++bit) {
                const auto digit=digits%3;digits/=3;
                if(digit)known|=1u<<bit;
                if(digit==2)value|=1u<<bit;
            }
            const unsigned unknown=(~known)&511;
            unsigned subset=unknown;KnownByte result{bytes[value|subset],255};
            do {
                const auto next=bytes[value|subset];
                result.known&=std::uint8_t(~(result.value^next));
                subset=(subset-1)&unknown;
            } while(subset!=unknown);
            entries_[index]=result;
        }
        full_=bytes;
        loaded_=true;
    }
    KnownByte lookup(unsigned value,unsigned known) const noexcept {
        if(!loaded_) return {};
        known&=511;return entries_[weights_[known]+weights_[value&known]];
    }
    // Fully known address: the consensus over an empty unknown set is the PROM
    // byte itself, so this equals lookup(value,511).value (0 while unloaded).
    std::uint8_t lookupKnown(unsigned value) const noexcept {return full_[value&511];}
    bool loaded() const noexcept {return loaded_;}
private:
    std::array<std::uint8_t,512> full_{};
    std::array<KnownByte,19683> entries_{};
    std::array<unsigned,512> weights_{};
    bool loaded_=false;
};
class VoicePrograms {
public:
    struct Program {std::array<std::uint8_t,8> code{};bool valid=false,idle=false;};
    void load(const PromConsensus& prom) noexcept {
        const auto entry=prom.lookup(0,0x1e0);entryValid=entry.known==255 && entry.value==0x6b;
        for(unsigned mask=0;mask<32;++mask) {
            unsigned power=1,weight=0;
            for(unsigned bit=0;bit<5;++bit){if(mask&(1u<<bit))weight+=power;power*=3;}
            weights_[mask]=weight;
        }
        const auto map=[](unsigned v){return ((v&1)<<4)|((v&2)<<2)|(v&4)|((v&8)>>2)|((v&16)>>4);};
        for(unsigned index=0;index<243;++index) {
            unsigned digits=index,value=0,known=0;
            for(unsigned bit=0;bit<5;++bit) {
                const auto d=digits%3;digits/=3;
                if(d)known|=1u<<bit;
                if(d==2)value|=1u<<bit;
            }
            auto& program=programs_[index];program.valid=true;
            for(unsigned phase=0;phase<8;++phase) {
                const auto op=prom.lookup((phase<<5)|map(value),0x1e0|map(known));
                program.code[phase]=op.value;program.valid&=op.known==255;
            }
            program.idle=program.valid && program.code[0]==0x6b;
            for(unsigned phase=1;phase<8;++phase)program.idle&=program.code[phase]==0x4a;
        }
    }
    const Program& get(KnownByte captured) const noexcept {
        const auto known=captured.known&31;
        return programs_[weights_[known]+weights_[captured.value&known]];
    }
    bool entryValid=false;
private:
    std::array<unsigned,32> weights_{};
    std::array<Program,243> programs_{};
};
struct ControllerProms {
    PromConsensus upper,lower;VoicePrograms voices;
    bool quietUpperTail=false;
    std::array<std::uint8_t,32> quietLowerPhases{};
    void certifyQuietUpperTail() noexcept {
        // With no channel request and an inactive/completed CPU request,
        // no upper-PROM event is permitted in phases10..15. Check the
        // loaded image, not its identity or a presumed production ROM.
        quietLowerPhases.fill(0);
        for(unsigned captured=0;captured<32;++captured) {
            const unsigned flags=((captured&1)<<4)|((captured&2)<<2)|(captured&4)|
                ((captured&8)>>2)|((captured&16)>>4);
            for(unsigned phase=10;phase<15;++phase) {
                const auto op=lower.lookup(((phase&7)<<5)|flags,511);
                if(op.known==255 && op.value==0x4a)
                    quietLowerPhases[captured]|=std::uint8_t(1u<<(phase&7));
            }
        }
        quietUpperTail=true;
        for(unsigned phase=2;phase<8;++phase)for(unsigned q:{1u,3u}) {
            const auto op=upper.lookup((phase<<4)|((q&1)<<3)|((q&2)<<1),511);
            quietUpperTail&=op.known==255 && op.value==0;
        }
    }
};
}
