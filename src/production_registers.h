#pragma once
// Concrete (production) datapath components for the fast runtime.
//
// The reference engine keeps partial-known masks on every value so it can prove
// behaviour under modelled uncertainty; that costs subset iteration loops and
// merge operations in the hot path. The production runtime starts from a fully
// concrete state, so the same components collapse to plain array accesses.
//
// Every component here must be validated against its reference counterpart on
// known inputs before the production runtime may use it.
#include <array>
#include <cstdint>

namespace eii::production {
// Sheet 7-20: six 2149H-3 x4 SRAMs, A9..6 grounded -> 64 x24. The reference
// iterates over every subset of unknown address bits; with a concrete address
// this is one cell read or write.
class RegisterFile {
public:
    void initializePowerOn(const std::array<std::uint32_t,64>& cells) noexcept {cells_=cells;}
    void fill(std::uint32_t value) noexcept {cells_.fill(value&0xffffff);}
    std::uint32_t read(std::uint8_t address) const noexcept {return cells_[address&63];}
    void write(std::uint8_t address,std::uint32_t value) noexcept {cells_[address&63]=value&0xffffff;}
private:
    std::array<std::uint32_t,64> cells_{};
};
// Sheet 7-19: IC119..122 staging. A completed CPU OUT latches the address or
// one data byte; the controller reads either the staging address or the
// controller-selected address depending on -MPWEN.
class Staging {
public:
    std::uint8_t address=0;
    std::uint32_t data=0;
    std::array<std::uint64_t,4> writes{};
    bool write(std::uint8_t port,std::uint8_t value) noexcept {
        if(port<0x48 || port>=0x68)return false;
        const unsigned bank=(port-0x48)>>3;
        ++writes[bank];
        if(!bank)address=std::uint8_t(value&63);
        else data=(data&~(0xffu<<((bank-1)*8)))|(unsigned(value)<<((bank-1)*8));
        return true;
    }
    // mpwenN high selects the controller address; low selects the CPU staging
    // address. Both are concrete in this path, so no merge is needed.
    std::uint8_t selectedAddress(std::uint8_t channel,std::uint16_t control,bool mpwenN) const noexcept {
        return mpwenN?std::uint8_t(((channel&15)<<2)|(control&3)):address;
    }
};
}
