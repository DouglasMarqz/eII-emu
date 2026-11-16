#pragma once
#include <cstdint>
#include <stdexcept>
namespace eii {
// Reproducible physical bit realization, selected before execution only.
// Not a manufacturer reset specification or measured SRAM distribution.
class DigitalPowerOnBits {
public:
    explicit DigitalPowerOnBits(std::uint32_t seed):state_(seed) {
        if(!seed)throw std::invalid_argument("digital power-on seed must be nonzero");
    }
    std::uint32_t next() noexcept {
        state_^=state_<<13;state_^=state_>>17;state_^=state_<<5;return state_;
    }
private:
    std::uint32_t state_;
};
}
