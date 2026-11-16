#pragma once
// Strict read-only HFEv1 E-mu FM payload importer. Container layout follows
// HxC's published specification: alternating256-byte side chunks per512 block,
// LSB-first sampled cells. Only two-sample FM pulses with a zero alternate
// sample lane are supported. No HFEv2/3 opcodes, weak bits or timing replay.
#include "floppy_image.h"
#include <string>

namespace eii {
class HfeImage {
public:
    struct Sector {
        std::array<std::uint8_t,EmuiiImage::sectorBytes> data{};
        unsigned cylinder=0,side=0,samplePhase=0,headerCell=0,dataCell=0;
    };
    explicit HfeImage(std::vector<std::uint8_t> bytes):raw_(std::move(bytes)) {
        if(raw_.size()<512)throw std::invalid_argument("HFE header truncated");
        if(std::string(reinterpret_cast<const char*>(raw_.data()),8)!="HXCPICFE"||raw_[8]!=0)
            throw std::invalid_argument("only HFEv1 revision0 is supported");
        tracks_=raw_[9];bitrate_=word(12);
        if(!tracks_||tracks_>80||raw_[10]!=2||raw_[11]!=3||raw_[16]!=0x0b||!bitrate_||bitrate_>1000)
            throw std::invalid_argument("requires1..80 tracks,2 sides, E-mu FM encoding/interface and valid bitrate");
        for(unsigned p:{22u,24u})if(raw_[p]!=255&&(raw_[p]!=0||raw_[p+1]!=3))
            throw std::invalid_argument("unsupported track0 alternate encoding");
        const std::size_t lut=std::size_t(word(18))*512;
        if(lut<512||lut>raw_.size()||tracks_*4>raw_.size()-lut)
            throw std::invalid_argument("HFE track list out of range");
        for(unsigned c=0;c<tracks_;++c) {
            const std::size_t p=lut+c*4,offset=std::size_t(word(p))*512,length=word(p+2);
            if(!length||(length&1))throw std::invalid_argument("HFE paired side length must be nonzero/even");
            const std::size_t sideBytes=length/2,storage=((sideBytes+255)/256)*512;
            if(offset<lut+tracks_*4||offset>raw_.size()||storage>raw_.size()-offset)
                throw std::invalid_argument("HFE padded track storage truncated or overlaps metadata");
            for(const auto& e:entries_)if(offset<e.offset+e.storage&&e.offset<offset+storage)
                throw std::invalid_argument("overlapping HFE track storage");
            entries_.push_back({offset,sideBytes,storage});
        }
    }
    unsigned tracks() const noexcept{return tracks_;}
    unsigned bitrate() const noexcept{return bitrate_;}
    Sector sector(unsigned cylinder,unsigned side) const {
        if(cylinder>=tracks_||side>=2)throw std::out_of_range("HFE cylinder/side out of range");
        const auto& e=entries_[cylinder];
        std::vector<std::uint8_t> samples;
        samples.reserve(e.sideBytes*8);
        unsigned ones[2]={0,0};
        for(std::size_t i=0;i<e.sideBytes;++i) {
            const auto byte=raw_[e.offset+(i/256)*512+side*256+i%256];
            for(unsigned k=0;k<8;++k) {const auto bit=std::uint8_t((byte>>k)&1);ones[samples.size()&1]+=bit;samples.push_back(bit);}
        }
        if((ones[0]==0)==(ones[1]==0))throw std::invalid_argument("unsupported HFE FM pulse sampling (alternate lane must be zero)");
        const unsigned phase=ones[1]==0?0:1;
        std::vector<std::uint8_t> cells;
        cells.reserve(samples.size());
        for(std::size_t p=phase;p<samples.size();p+=2)cells.push_back(samples[p]);
        const auto size=cells.size();
        if(size<32)throw std::invalid_argument("HFE FM revolution too short");
        const auto oneRevolution=cells;
        cells.insert(cells.end(),oneRevolution.begin(),oneRevolution.end());
        const auto mark=[&](std::size_t at) {
            if(at>cells.size()||32>cells.size()-at)return false;
            for(unsigned n=0;n<2;++n)for(unsigned k=0;k<8;++k) {
                const auto p=at+n*16+k*2;
                if(cells[p]!=1||cells[p+1]!=(((n?0x96:0xfa)>>k)&1))return false;
            }
            return true;
        };
        const auto read=[&](std::size_t at,std::span<std::uint8_t> out) {
            if(at>cells.size()||out.size()>(cells.size()-at)/16)return false;
            for(auto& byte:out) {
                byte=0;
                for(unsigned k=0;k<8;++k) {
                    if(cells[at+k*2]!=1)return false;
                    byte|=cells[at+k*2+1]<<k;
                }
                at+=16;
            }
            return true;
        };
        Sector result;unsigned found=0;
        for(std::size_t h=0;h<size;++h)if(mark(h)) {
            std::array<std::uint8_t,3> header{};
            if(!read(h+32,header)||header[0]!=cylinder*2+side)continue;
            if(emuiiCrc(std::span<const std::uint8_t>(header).first(1))!=unsigned(header[1]|(header[2]<<8)))continue;
            // This importer supports the standard bounded EII inter-field gap.
            std::size_t data=h+80;
            while(data<h+1104&&!mark(data))++data;
            constexpr std::size_t frame=32+(EmuiiImage::sectorBytes+2)*16;
            if(data==h+1104||data+frame>h+size)continue;
            Sector candidate;std::array<std::uint8_t,2> check{};
            if(!read(data+32,candidate.data)||!read(data+32+candidate.data.size()*16,check))continue;
            if(emuiiCrc(candidate.data)!=unsigned(check[0]|(check[1]<<8)))continue;
            candidate.cylinder=cylinder;candidate.side=side;candidate.samplePhase=phase;
            candidate.headerCell=unsigned(h);candidate.dataCell=unsigned(data);
            result=std::move(candidate);++found;
        }
        if(found!=1)throw std::invalid_argument("requires one complete CRC-valid EII sector at cylinder"+std::to_string(cylinder)+" side"+std::to_string(side)+" (found"+std::to_string(found)+")");
        return result;
    }
    std::span<const std::uint8_t> bytes()const noexcept{return raw_;}
    void replaceSector(unsigned cylinder,unsigned side,std::span<const std::uint8_t> payload){
        if(payload.size()!=EmuiiImage::sectorBytes)throw std::invalid_argument("HFE sector size");
        const auto decoded=sector(cylinder,side);const auto& e=entries_.at(cylinder);
        std::array<std::uint8_t,EmuiiImage::sectorBytes+2> data{};std::copy(payload.begin(),payload.end(),data.begin());
        const auto crc=emuiiCrc(payload);data[payload.size()]=std::uint8_t(crc);data[payload.size()+1]=std::uint8_t(crc>>8);
        const auto cellCount=e.sideBytes*4;
        const auto set=[&](std::size_t cell,bool value){const auto sample=(cell%cellCount)*2+decoded.samplePhase;
            const auto byte=sample/8;auto& raw=raw_[e.offset+(byte/256)*512+side*256+byte%256];
            const auto mask=std::uint8_t(1u<<(sample%8));raw=std::uint8_t((raw&~mask)|(value?mask:0));};
        for(unsigned n=0;n<data.size();++n)for(unsigned bit=0;bit<8;++bit){const auto cell=decoded.dataCell+32+n*16+bit*2;
            set(cell,true);set(cell+1,(data[n]>>bit)&1);}
    }
    std::vector<std::uint8_t> extractEmuii() const {
        if(tracks_!=80)throw std::invalid_argument("complete EMUIIFD extraction requires80cylinders/160sectors");
        std::vector<std::uint8_t> out;out.reserve(EmuiiImage::imageBytes);
        for(unsigned c=0;c<80;++c)for(unsigned s=0;s<2;++s) {
            const auto decoded=sector(c,s);out.insert(out.end(),decoded.data.begin(),decoded.data.end());
        }
        return out; // No output is exposed until every physical sector validates.
    }
private:
    struct Entry {std::size_t offset,sideBytes,storage;};
    std::vector<std::uint8_t> raw_;
    std::vector<Entry> entries_;
    unsigned tracks_=0,bitrate_=0;
    std::uint16_t word(std::size_t at) const {return std::uint16_t(raw_[at]|(raw_[at+1]<<8));}
};
}

