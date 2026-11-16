#pragma once
// EII bank layouts, reproduced byte-exact from original firmware writes:
// * .EII file / floppy: the EMUIIFD payload from byte 78848 on, 485887 bytes.
// * HD OS 3.1 hard disk: bank k at 0x12600 + k*485888, directory entry k at
//   0x10B7B + 32*k (12-byte name = bank bytes 2B4h..2BFh, then 20 zero bytes;
//   empty = "Null Preset "). Bank data = the 27136-byte header, then seven
//   64 KB pages in which the floppy order A(38400) B(27136) is stored B A
//   (the floppy's last page is one byte short; the firmware pads it with 0).
// Verified: three firmware-saved slots (factory 13 Digital Synth, two
// third-party banks) equal hddFromEii() of their .EII files, 0 mismatches.
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
namespace eii::bank {
inline constexpr std::size_t eiiBytes=485887,hddSlotBytes=485888,headerBytes=27136,pageBytes=65536,pageSplit=38400;
inline constexpr std::size_t hddDataBase=0x12600,hddDirBase=0x10B7B,hddDirStride=32,nameOffset=0x2B4,nameBytes=12;
inline constexpr std::array<char,12> nullName{'N','u','l','l',' ','P','r','e','s','e','t',' '};
inline std::vector<std::uint8_t> hddFromEii(std::span<const std::uint8_t> e){
    if(e.size()!=eiiBytes)throw std::invalid_argument("EII bank must be 485887 bytes");
    std::vector<std::uint8_t> out(e.begin(),e.begin()+headerBytes);out.reserve(hddSlotBytes);
    for(unsigned n=0;n<7;++n){
        const std::size_t p=headerBytes+pageBytes*n;
        const std::size_t aEnd=p+pageSplit,bEnd=std::min(p+pageBytes,e.size());
        out.insert(out.end(),e.begin()+aEnd,e.begin()+bEnd);
        out.resize(out.size()+(p+pageBytes-bEnd),0); // firmware pad of the short last page
        out.insert(out.end(),e.begin()+p,e.begin()+aEnd);
    }
    return out;
}
inline std::vector<std::uint8_t> eiiFromHdd(std::span<const std::uint8_t> s){
    if(s.size()!=hddSlotBytes)throw std::invalid_argument("HDD bank slot must be 485888 bytes");
    std::vector<std::uint8_t> out(s.begin(),s.begin()+headerBytes);out.reserve(hddSlotBytes);
    for(unsigned n=0;n<7;++n){
        const std::size_t q=headerBytes+pageBytes*n;
        out.insert(out.end(),s.begin()+q+(pageBytes-pageSplit),s.begin()+q+pageBytes); // A
        out.insert(out.end(),s.begin()+q,s.begin()+q+(pageBytes-pageSplit));          // B
    }
    out.resize(eiiBytes);return out;
}
inline unsigned hddSlotCount(std::size_t imageBytes){
    if(imageBytes<=hddDataBase)return 0;const auto n=(imageBytes-hddDataBase)/hddSlotBytes;return unsigned(n>46?46:n);
}
inline std::string hddSlotName(std::span<const std::uint8_t> image,unsigned k){
    const auto at=hddDirBase+hddDirStride*k;if(at+nameBytes>image.size())return {};
    return std::string(reinterpret_cast<const char*>(image.data()+at),nameBytes);
}
inline bool hddSlotFree(std::span<const std::uint8_t> image,unsigned k){
    const auto at=hddDirBase+hddDirStride*k;if(at+nameBytes>image.size())return false;
    for(unsigned i=0;i<nameBytes;++i)if(image[at+i]!=std::uint8_t(nullName[i]))return false;return true;
}
// Writes the bank and its directory entry exactly as the firmware does.
inline void hddWriteSlot(std::span<std::uint8_t> image,unsigned k,std::span<const std::uint8_t> e){
    if(k>=hddSlotCount(image.size()))throw std::out_of_range("HDD bank slot out of range");
    const auto data=hddFromEii(e);std::copy(data.begin(),data.end(),image.begin()+hddDataBase+hddSlotBytes*k);
    const auto at=hddDirBase+hddDirStride*k;
    for(unsigned i=0;i<nameBytes;++i)image[at+i]=e[nameOffset+i];
    for(unsigned i=nameBytes;i<hddDirStride;++i)image[at+i]=0;
}
}
