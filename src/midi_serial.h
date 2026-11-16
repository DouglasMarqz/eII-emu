#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <span>
namespace eii {
// SCN2651 register-level MIDI receiver, not an OS note allocator. EII7-16:
// optocoupler -> RXD; external500kHz /16 ->31250baud; RXRDY_n -> CTC CT1.
// Valid host bytes are serialized as8N1 character-completion events. Pin-level
// framing/parity, synchronous modes and host MIDI OUT remain outside this path.
class MidiSerial2651 {
public:
    std::array<std::uint8_t,2> mode{};
    std::uint8_t command=0;
    bool ready=false;
    std::uint64_t received=0,reads=0,overruns=0,rejected=0;
    void writeMode(std::uint8_t value) noexcept {mode[index_]=value;index_^=1;}
    void writeCommand(std::uint8_t value) noexcept {
        command=value&~16;
        if((value&16)||!(value&4))error_=0;
        if(!(value&4))ready=false;
    }
    std::uint8_t readMode() noexcept {auto value=mode[index_];index_^=1;return value;}
    std::uint8_t readCommand() noexcept {index_=0;return command;}
    bool midiMode() const noexcept {return (mode[0]&0xdf)==0x4e && !(mode[1]&16) && !(command&0xc0);}
    bool enabled() const noexcept {return midiMode()&&(command&4);}
    void receive(std::uint8_t byte) noexcept {
        if(!enabled()){++rejected;return;}
        if(ready){error_|=16;++overruns;}
        value_=byte;ready=true;++received;
    }
    std::uint8_t status() const noexcept {
        // DCD/DSR tied low on EII; TX holding-register timing is not modeled.
        return std::uint8_t(0xc0|((command&1)?1:0)|(ready?2:0)|error_);
    }
    std::uint8_t readData() noexcept {ready=false;++reads;return value_;}
private:
    unsigned index_=0;
    std::uint8_t value_=0,error_=0;
};
class MidiWire {
public:
    static constexpr std::uint64_t characterTicks=1280; //10bits at31250,CPU4MHz
    static constexpr unsigned capacity=4096;
    std::uint64_t dropped=0,delivered=0;
    unsigned queuedBytes() const noexcept {return unsigned(bytes_.size());}
    bool enqueue(std::span<const std::uint8_t> bytes,std::uint64_t now) {
        if(bytes.size()>capacity-bytes_.size()){++dropped;return false;}
        if(bytes_.empty())next_=now+characterTicks;
        for(auto b:bytes)bytes_.push_back(b);
        return true;
    }
    void advance(std::uint64_t now,MidiSerial2651& receiver) noexcept {
        if(bytes_.empty()||now<next_)return;
        receiver.receive(bytes_.front());bytes_.pop_front();++delivered;
        next_+=characterTicks;
    }
private:
    std::deque<std::uint8_t> bytes_;
    std::uint64_t next_=0;
};
// WinMM short messages contain explicit status, including realtime bytes.
inline unsigned midiMessageLength(std::uint8_t status) noexcept {
    if(status<0x80)return 0;
    if(status<0xf0)return (status&0xe0)==0xc0?2:3;
    switch(status){case 0xf1:case 0xf3:return 2;case 0xf2:return 3;
        case 0xf6:case 0xf8:case 0xfa:case 0xfb:case 0xfc:case 0xfe:case 0xff:return 1;default:return 0;}
}
}
