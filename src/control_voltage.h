#pragma once
#include <array>
#include <cstdint>

namespace eii {
// Ideal register/routing/track-hold subset of schematic 7-39. No calibrated
// DAC settling, switch resistance, charge injection, leakage or TL084 model.
class ControlVoltageBus {
public:
    enum Bank : unsigned {Amplitude=0,Frequency=1,Resonance=2};
    struct Held {
        double volts=0; // meaningful only when known == true
        std::uint64_t lastTrackTick=0;
        std::uint8_t code=0;
        bool known=false;
    };
    // Digital latches have no board reset input; neither their power-up bits
    // nor the capacitor charges are assumed valid before firmware writes.
    std::array<std::array<Held,8>,3> held{};
    std::uint8_t dacCode=0,muxLatch=0;
    bool dacKnown=false,muxKnown=false;
    std::uint64_t dacWrites=0,muxWrites=0;

    void writeDac(std::uint8_t value,std::uint64_t tick) noexcept {
        dacCode=value;dacKnown=true;++dacWrites;track(tick);
    }
    void writeMux(std::uint8_t value,std::uint64_t tick) noexcept {
        muxLatch=value&0x3f;muxKnown=true;++muxWrites;track(tick);
    }
    static constexpr double nominalVolts(std::uint8_t code) noexcept {
        // Vref=-5V, internal matched feedback resistor, inverting amplifier:
        // Vout=-Vref*D/256, not D/255. PDO7..0 map to MSB..LSB normally.
        return 5.0*double(code)/256.0;
    }
    unsigned knownCount() const noexcept {
        unsigned n=0;for(const auto& bank:held)for(const auto& v:bank)n+=v.known;return n;
    }
private:
    void track(std::uint64_t tick) noexcept {
        if(!dacKnown || !muxKnown) return;
        const unsigned channel=muxLatch&7;
        for(unsigned bank=0;bank<3;++bank) {
            // IC45 Qc/Qb/Qa: PDO3/4/5 respectively. A high inhibit opens
            // that bank. The shared channel address is PDO2..0.
            if(muxLatch&(1u<<(bank+3))) continue;
            held[bank][channel]={nominalVolts(dacCode),tick,dacCode,true};
        }
    }
};
}
