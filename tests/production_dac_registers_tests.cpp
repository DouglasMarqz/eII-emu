// Differential test: the concrete DAC registers must reproduce
// SampleDacRegisters whenever every input is known.
#include "sample_dac.h"
#include "production_dac.h"
#include <iostream>
#include <random>

namespace { void require(bool v, const char* w) { if (!v) throw std::runtime_error(w); } }

int main() try {
    std::mt19937 rng(0x4455u);
    unsigned operations = 0;
    for (unsigned trial = 0; trial < 2000; ++trial) {
        eii::SampleDacRegisters ref;
        eii::production::DacRegisters con;
        eii::DigitalPowerOnBits pow(std::uint32_t(rng()) | 1u);
        const unsigned channel = rng() & 7;
        const bool enable = (rng() & 1) != 0;
        ref.initializePowerOn(pow, channel, {true, enable});
        std::uint8_t concreteBits[9];
        for (unsigned i = 0; i < 8; ++i) concreteBits[i] = ref.codes[i].value;
        concreteBits[8] = std::uint8_t(255 ^ (enable ? (1u << ((channel + 7) & 7)) : 0u));
        con.initializePowerOn(std::uint8_t(channel), concreteBits);
        for (unsigned step = 0; step < 50; ++step) {
            const std::uint8_t select = std::uint8_t(rng() & 7);
            const bool en = (rng() & 1) != 0;
            const std::uint8_t chdb = std::uint8_t(rng());
            ref.drive({select, 7}, {true, en}, {chdb, 255});
            con.drive(select, en, chdb);
            ++operations;
            for (unsigned i = 0; i < 8; ++i) {
                require(ref.codes[i].known == 255, "reference code left the known domain");
                require(ref.codes[i].value == con.codes[i], "DAC code mismatch");
            }
        }
    }
    std::cout << "PASS: concrete DAC registers reproduce SampleDacRegisters over " << operations << " drive operations\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
}
