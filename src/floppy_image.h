#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace eii {
// A payload image has no original gaps, CRCs, flux or index phase. These
// profiles synthesize nominal formatting; neither recovers a physical disk.
enum class EmuiiGap { Esynthesist2010, Hxc2026 };

// CRC-16/ARC: reflected 8005 polynomial, initial/final XOR zero, LSB first.
inline std::uint16_t emuiiCrc(std::span<const std::uint8_t> data) noexcept {
    std::uint16_t crc=0;
    for(auto b:data) {
        crc^=b;
        for(unsigned i=0;i<8;++i)crc=std::uint16_t((crc>>1)^((crc&1)?0xa001:0));
    }
    return crc;
}

struct EmuiiFmTrack {
    // One means a flux transition in this FM half-bit slot, NOT a level or
    // physical read pulse. 312500 slots/s * .2s =62500, at64 20MHz edges/slot.
    static constexpr unsigned slotEdges=64,slotCount=62500;
    static constexpr unsigned revolutionEdges=slotCount*slotEdges;
    std::vector<std::uint8_t> transitions;
    unsigned dataMarkerBit=0,dataBit=0;
};

class EmuiiImage {
public:
    static constexpr unsigned cylinders=80,sides=2,sectorBytes=3584;
    static constexpr unsigned imageBytes=cylinders*sides*sectorBytes;
    explicit EmuiiImage(std::vector<std::uint8_t> bytes):bytes_(std::move(bytes)) {
        if(bytes_.size()!=imageBytes)throw std::invalid_argument("EMUIIFD requires exactly573440 payload bytes; E2O/EII/flux images are different formats");
    }
    std::span<const std::uint8_t> payload(unsigned cylinder,unsigned side) const {
        if(cylinder>=cylinders||side>=sides)throw std::out_of_range("EII cylinder0..79 and side0..1 required");
        return std::span<const std::uint8_t>(bytes_).subspan((cylinder*2+side)*sectorBytes,sectorBytes);
    }
    std::uint64_t revision=0;
    std::span<const std::uint8_t> bytes()const noexcept{return bytes_;}
    void replacePayload(unsigned cylinder,unsigned side,std::span<const std::uint8_t> data){
        if(cylinder>=cylinders||side>=sides||data.size()!=sectorBytes)throw std::invalid_argument("invalid floppy write extent");
        std::copy(data.begin(),data.end(),bytes_.begin()+(cylinder*2+side)*sectorBytes);++revision;
    }
    EmuiiFmTrack format(unsigned cylinder,unsigned side,EmuiiGap profile) const {
        const auto source=payload(cylinder,side);
        if(profile!=EmuiiGap::Esynthesist2010&&profile!=EmuiiGap::Hxc2026)throw std::invalid_argument("explicit supported gap profile required");
        EmuiiFmTrack result;result.transitions.reserve(EmuiiFmTrack::slotCount);
        const auto bit=[&](bool value){result.transitions.push_back(1);result.transitions.push_back(value);};
        const auto byte=[&](std::uint8_t value){for(unsigned i=0;i<8;++i)bit((value>>i)&1);};
        const auto repeat=[&](unsigned n,std::uint8_t value){while(n--)byte(value);};
        const auto crc=[&](std::span<const std::uint8_t> values){const auto c=emuiiCrc(values);byte(std::uint8_t(c));byte(std::uint8_t(c>>8));};
        repeat(20,255);repeat(4,0);byte(0xfa);byte(0x96);
        const std::array<std::uint8_t,1> id={std::uint8_t(cylinder*2+side)};
        byte(id[0]);crc(id);byte(0);
        // Report:8 FF bytes; HxC reference generator:7 FF bytes +4 one bits.
        const unsigned gapBits=profile==EmuiiGap::Esynthesist2010?64:60;
        for(unsigned i=0;i<gapBits;++i)bit(true);
        repeat(4,0);result.dataMarkerBit=unsigned(result.transitions.size()/2);
        byte(0xfa);byte(0x96);result.dataBit=unsigned(result.transitions.size()/2);
        for(auto b:source)byte(b);
        crc(source);repeat(2,0);repeat(20,255);
        if(result.transitions.size()>EmuiiFmTrack::slotCount)throw std::logic_error("EII frame exceeds nominal revolution");
        // The remaining .25 byte is retained; never round revolution length.
        while(result.transitions.size()<EmuiiFmTrack::slotCount)bit(true);
        return result;
    }
private:
    std::vector<std::uint8_t> bytes_;
};

// Offline inspection of a complete nominal revolution. Searches every bit
// phase for zero sync + FA96, checks FM clocks and both CRCs, then returns
// decoded bytes. This does not provide data directly to CPU memory or SIO.
struct EmuiiDecodedTrack {
    std::uint8_t id=0;
    unsigned headerBit=0,dataMarkerBit=0;
    std::array<std::uint8_t,EmuiiImage::sectorBytes> payload{};
};
inline EmuiiDecodedTrack inspectEmuiiFm(std::span<const std::uint8_t> slots) {
    if(slots.size()!=EmuiiFmTrack::slotCount)throw std::invalid_argument("one62500-slot revolution required");
    for(unsigned i=0;i<slots.size();++i)
        if(slots[i]>1||(!(i&1)&&slots[i]!=1))throw std::invalid_argument("malformed nominal FM clock/data slot");
    constexpr unsigned bits=EmuiiFmTrack::slotCount/2;
    const auto byte=[&](unsigned start){std::uint8_t v=0;for(unsigned i=0;i<8;++i)v|=std::uint8_t(slots[2*((start+i)%bits)+1]<<i);return v;};
    const auto syncMark=[&](unsigned p){
        if(byte(p)!=0xfa||byte(p+8)!=0x96)return false;
        for(unsigned i=1;i<=4;++i)if(byte(p+bits-8*i))return false;
        return true;
    };
    for(unsigned p=0;p<bits;++p)if(syncMark(p)) {
        std::array<std::uint8_t,3> header={byte(p+16),byte(p+24),byte(p+32)};
        if(header[0]>=160||emuiiCrc(header)!=0)continue;
        // The next sync+mark must follow the small inter-field gap, not a
        // pattern coincidentally occurring far inside the payload.
        for(unsigned distance=72;distance<=160;++distance) {
            const auto mark=(p+distance)%bits;
            if(!syncMark(mark))continue;
            EmuiiDecodedTrack result;result.id=header[0];result.headerBit=p;result.dataMarkerBit=mark;
            std::array<std::uint8_t,EmuiiImage::sectorBytes+2> data{};
            for(unsigned i=0;i<data.size();++i)data[i]=byte(mark+16+8*i);
            if(emuiiCrc(data)!=0)continue;
            std::copy_n(data.begin(),result.payload.size(),result.payload.begin());
            return result;
        }
    }
    throw std::invalid_argument("no valid EII header/data frame with both CRCs found");
}
}

