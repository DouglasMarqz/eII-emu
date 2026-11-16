#include "floppy_image.h"
#include "floppy_read_separator.h"
#include <algorithm>
#include <iostream>
#include <string>

static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F> static void rejects(F f,const char* why){bool caught=false;try{f();}catch(const std::exception&){caught=true;}require(caught,why);}
static std::uint8_t reverse(std::uint8_t v){std::uint8_t r=0;for(unsigned i=0;i<8;++i)r=std::uint8_t((r<<1)|((v>>i)&1));return r;}
// Independent MSB-first polynomial implementation: reverse each input byte,
// long-divide by18005, then reverse the16-bit remainder. No reflected loop.
static std::uint16_t crcOracle(std::span<const std::uint8_t> bytes){
    unsigned remainder=0;
    for(auto b:bytes){remainder^=unsigned(reverse(b))<<8;for(unsigned i=0;i<8;++i){remainder<<=1;if(remainder&0x10000)remainder^=0x18005;}}
    return std::uint16_t((unsigned(reverse(std::uint8_t(remainder)))<<8)|reverse(std::uint8_t(remainder>>8)));
}
static std::vector<std::uint8_t> separated(const eii::EmuiiFmTrack& track,std::uint64_t width){
    eii::FloppyReadSeparator separator(width);std::vector<std::uint8_t> bits;
    const auto sample=[&](std::uint64_t,bool value){bits.push_back(value);};
    for(unsigned slot=0;slot<track.transitions.size();++slot) {
        const auto t=std::uint64_t(slot)*64+8; // explicit400ns read-pulse trailing offset
        if(track.transitions[slot])separator.pulse(t,sample);else separator.advance(t,sample);
    }
    separator.advance(eii::EmuiiFmTrack::revolutionEdges+width+8,sample);
    require(separator.inputPulses>=track.transitions.size()/2,"every clock transition reaches the circuit");
    return bits;
}
int main(){try{
    const std::string check="123456789";const auto span=std::span(reinterpret_cast<const std::uint8_t*>(check.data()),check.size());
    require(eii::emuiiCrc(span)==0xbb3d&&crcOracle(span)==0xbb3d,"published CRC-16/ARC check vector");
    for(unsigned value=0;value<256;++value){const std::array<std::uint8_t,1>b={std::uint8_t(value)};require(eii::emuiiCrc(b)==crcOracle(b),"all one-byte CRCs vs independent polynomial division");}
    rejects([]{eii::EmuiiImage image(std::vector<std::uint8_t>(72704));},"E2O-sized input must not become a disk");
    rejects([]{eii::EmuiiImage image(std::vector<std::uint8_t>(eii::EmuiiImage::imageBytes-1));},"truncation rejected");
    rejects([]{eii::EmuiiImage image(std::vector<std::uint8_t>(eii::EmuiiImage::imageBytes+1));},"trailing bytes rejected");
    std::vector<std::uint8_t> raw(eii::EmuiiImage::imageBytes);
    for(unsigned id=0;id<160;++id)for(unsigned i=0;i<3584;++i)raw[id*3584+i]=std::uint8_t((i*73)^(i>>8)^id);
    eii::EmuiiImage image(raw);
    rejects([&]{image.payload(80,0);},"out-of-range cylinder");rejects([&]{image.payload(0,2);},"out-of-range side");
    rejects([&]{image.format(0,0,static_cast<eii::EmuiiGap>(99));},"unsupported gap profile rejected");
    for(unsigned id=0;id<160;++id)for(auto profile:{eii::EmuiiGap::Esynthesist2010,eii::EmuiiGap::Hxc2026}) {
        const auto payload=image.payload(id/2,id%2);
        require(std::equal(payload.begin(),payload.end(),raw.begin()+id*3584),"interleaved side offset");
        auto track=image.format(id/2,id%2,profile);
        const unsigned marker=profile==eii::EmuiiGap::Esynthesist2010?336:332;
        require(track.transitions.size()==62500&&track.dataMarkerBit==marker&&track.dataBit==marker+16,"exact revolution and independently counted header/gap offsets");
        const auto byte=[&](unsigned p){unsigned v=0;for(unsigned i=0;i<8;++i)v|=unsigned(track.transitions[2*(p+i)+1])<<i;return v;};
        for(unsigned b=0;b<20;++b)require(byte(8*b)==255,"index starts with20 FF gap bytes");
        for(unsigned b=20;b<24;++b)require(byte(8*b)==0,"four sync zero bytes");
        require(byte(192)==0xfa&&byte(200)==0x96&&byte(208)==id,"FA96 ID mark and physical cylinder/side ID");
        const std::array<std::uint8_t,1> header={std::uint8_t(id)};
        const auto hcrc=crcOracle(header),dcrc=crcOracle(payload);
        require(byte(216)==(hcrc&255)&&byte(224)==(hcrc>>8),"header CRC uses ID only, excludes sync/mark");
        require(byte(marker+16+3584*8)==(dcrc&255)&&byte(marker+24+3584*8)==(dcrc>>8),"payload-only CRC, low byte first");
        for(unsigned b=0;b<3584;++b)require(byte(marker+16+b*8)==payload[b],"every serialized payload bit vs original image");
        const auto decoded=eii::inspectEmuiiFm(track.transitions);
        require(decoded.id==id&&decoded.headerBit==192&&decoded.dataMarkerBit==marker&&std::equal(payload.begin(),payload.end(),decoded.payload.begin()),"all160 cylinder/sides, both gap profiles decoded");
        const auto bits=separated(track,87); // explicit4.35us nominal experiment
        require(bits.size()==31250,"LS221/LS74 yields exactly31250 data clocks per200ms");
        for(unsigned i=0;i<bits.size();++i)require(bits[i]==track.transitions[2*i+1],"circuit recovers each bit, not a sector-level shortcut");
    }
    auto track=image.format(0,0,eii::EmuiiGap::Hxc2026);
    // Bit-phase search: move the complete frame by13 data bits, preserving clocks.
    auto rotated=track.transitions;std::rotate(rotated.begin(),rotated.begin()+26,rotated.end());
    require(eii::inspectEmuiiFm(rotated).headerBit==179,"sync search not hardcoded to byte or index alignment");
    rotated=track.transitions;std::rotate(rotated.begin(),rotated.begin()+2000,rotated.end());
    const auto wrapped=eii::inspectEmuiiFm(rotated);
    require(wrapped.headerBit==30442&&std::equal(wrapped.payload.begin(),wrapped.payload.end(),image.payload(0,0).begin()),"header and payload wrap across revolution origin");
    track.transitions[2*(track.dataBit+159)+1]^=1;
    rejects([&]{eii::inspectEmuiiFm(track.transitions);},"payload bit corruption fails CRC");
    track=image.format(0,0,eii::EmuiiGap::Hxc2026);track.transitions[2*216+1]^=1;
    rejects([&]{eii::inspectEmuiiFm(track.transitions);},"header CRC corruption rejected");
    track=image.format(0,0,eii::EmuiiGap::Hxc2026);track.transitions[10]=0;
    rejects([&]{eii::inspectEmuiiFm(track.transitions);},"missing FM clock diagnosed");
    track=image.format(0,0,eii::EmuiiGap::Hxc2026);track.transitions[11]=2;
    rejects([&]{eii::inspectEmuiiFm(track.transitions);},"invalid transition slot diagnosed");
    rejects([]{eii::inspectEmuiiFm({});},"wrong revolution length");
    track=image.format(79,1,eii::EmuiiGap::Hxc2026);
    for(unsigned width:{65u,80u,87u,100u,127u}){
        const auto recovered=separated(track,width);require(recovered.size()==31250,"valid monostable window clock count");
        for(unsigned i=0;i<recovered.size();++i)require(recovered[i]==track.transitions[2*i+1],"window interior preserves all data");
    }
    for(unsigned width:{63u,129u}){
        const auto recovered=separated(track,width);bool equal=recovered.size()==31250;
        if(equal)for(unsigned i=0;i<recovered.size();++i)equal&=recovered[i]==track.transitions[2*i+1];
        require(!equal,"out-of-window negative control must not pass as recovered data");
    }
    rejects([]{eii::FloppyReadSeparator s(0);},"zero width rejected");
    rejects([]{eii::FloppyReadSeparator s(87);s.pulse(std::numeric_limits<std::uint64_t>::max(),[](std::uint64_t,bool){});},"expiry overflow rejected");
    eii::FloppyReadSeparator s(87);auto nop=[](std::uint64_t,bool){};s.advance(100,nop);
    rejects([&]{s.pulse(99,nop);},"backward circuit time rejected");
    s.pulse(128,nop);s.pulse(192,nop);require(s.ignoredRetriggers==1&&s.rxData()&&!s.rxClock(),"LS74 captures oldQ and LS221 does not retrigger");
    s.advance(215,nop);require(s.samples==1&&s.rxClock(),"monostable expiry raises RXC once");
    std::cout<<"PASS:160 cylinder/sides x2 explicit FM profiles, independent CRC polynomial/check vector, all payload bits,31250-bit revolution,13-bit sync offset, corruption guards; LS221/LS74 per-edge recovery and window negative controls. No Machine/SIO/DMA image transport or OS boot.\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
