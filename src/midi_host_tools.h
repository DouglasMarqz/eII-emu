#pragma once
#include "midi_serial.h"
#include <array>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <optional>
#include <stdexcept>
namespace eii::host {
// Optional host adapters; the SCN2651/MIDI wire remains a raw hardware model.
class MidiHostTools {
public:
    bool wheelBridge=false,noteOffZero=false,recording=false;
    std::uint64_t wheelRevision=0,total=0,overwritten=0;
    // Host-enqueued CC64 observations; these are not firmware pedal state.
    std::uint16_t sustainSeenMask=0,sustainDownMask=0;
    void resetInputState() noexcept {sustainSeenMask=sustainDownMask=0;}
    void observeEnqueued(std::span<const std::uint8_t> bytes) noexcept {
        if(bytes.size()!=3 || (bytes[0]&0xf0)!=0xb0 || bytes[1]!=64)return;
        const auto bit=std::uint16_t(1u<<(bytes[0]&15));sustainSeenMask|=bit;
        if(bytes[2]>=64)sustainDownMask|=bit;else sustainDownMask&=std::uint16_t(~bit);
    }
    static constexpr auto releaseAllMessages() noexcept {
        std::array<std::array<std::uint8_t,3>,32> messages{};
        for(unsigned ch=0;ch<16;++ch) {
            messages[2*ch]={std::uint8_t(0xb0|ch),64,0};
            messages[2*ch+1]={std::uint8_t(0xb0|ch),123,0};
        }
        return messages;
    }
    struct Wheel {unsigned control;double volts;};
    struct Packet {std::array<std::uint8_t,3> bytes{};std::optional<Wheel> wheel;};
    Packet adapt(std::span<const std::uint8_t> raw) {
        if(raw.empty() || raw.size()!=midiMessageLength(raw[0]) ||
           std::any_of(raw.begin()+1,raw.end(),[](auto b){return b>=128;}))
            throw std::invalid_argument("Invalid MIDI short packet");
        Packet p;std::copy(raw.begin(),raw.end(),p.bytes.begin());
        const auto status=raw[0]&0xf0;
        if(noteOffZero && status==0x80 && raw.size()==3) {
            p.bytes[0]=std::uint8_t(0x90|(raw[0]&15));p.bytes[2]=0;
        }
        if(wheelBridge && raw.size()==3) {
            if(status==0xe0)p.wheel=Wheel{4,double(unsigned(raw[1])+(unsigned(raw[2])<<7))*5/16384};
            else if(status==0xb0 && raw[1]==1)p.wheel=Wheel{5,double(raw[2])*5/127};
            if(p.wheel)++wheelRevision;
        }
        return p;
    }
    void record(bool enabled) noexcept {
        if(enabled&&!recording){count_=head_=0;total=overwritten=0;}
        recording=enabled;
    }
    void observe(std::span<const std::uint8_t> raw,const Packet& wire,std::uint64_t hostNs,
                 std::uint64_t applyNs,std::uint64_t tick,bool accepted) noexcept {
        if(!recording)return;
        Event e;e.hostNs=hostNs;e.applyNs=applyNs;e.tick=tick;e.accepted=accepted;
        e.size=std::uint8_t(raw.size());std::copy(raw.begin(),raw.end(),e.raw.begin());e.wire=wire.bytes;
        if(count_==events_.size()){head_=(head_+1)%events_.size();--count_;++overwritten;}
        events_[(head_+count_)%events_.size()]=e;++count_;++total;
    }
    std::string exportTsv() const {
        std::ostringstream out;
        out << "# EII host MIDI input; bounded2048 events; no full machine snapshot\n"
            << "# wheel_bridge=" << wheelBridge << " note_off_zero=" << noteOffZero
            << " total=" << total << " overwritten=" << overwritten << "\n"
            << "host_ns\tapply_ns\tsim_tick\tsize\traw_hex\twire_hex\taccepted\n";
        for(unsigned n=0;n<count_;++n) {
            const auto& e=events_[(head_+n)%events_.size()];
            out << std::dec << e.hostNs << '\t' << e.applyNs << '\t' << e.tick << '\t' << unsigned(e.size) << '\t';
            for(auto bytes:{e.raw,e.wire}) {
                for(unsigned i=0;i<e.size;++i)out << std::hex << std::setfill('0') << std::setw(2) << unsigned(bytes[i]);
                out << '\t';
            }
            out << std::dec << e.accepted << '\n';
        }
        return out.str();
    }
private:
    struct Event {
        std::uint64_t hostNs=0,applyNs=0,tick=0;
        std::array<std::uint8_t,3> raw{},wire{};
        std::uint8_t size=0;bool accepted=false;
    };
    std::array<Event,2048> events_{};
    unsigned count_=0,head_=0;
};
}
