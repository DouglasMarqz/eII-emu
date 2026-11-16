#pragma once
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace eii {
// Sheet7-15 laboratory: IC106 non-retriggerable LS221 Q -> IC105 LS74 D;
// -DRDTA rising clocks both; old Q is captured at the shared input edge.
// IC105 Q is RXDA; IC106 /Q is /RXCA (SIO samples on rising RXC).
// Explicit ideal idle initial condition; not physical startup convergence.
// Pulse width is required in20MHz edges: choose from characterized timing,
// or label a nominal experiment. No invented direct sector/byte delivery.
class FloppyReadSeparator {
public:
    explicit FloppyReadSeparator(std::uint64_t pulseEdges):width_(pulseEdges) {
        if(!width_)throw std::invalid_argument("positive explicit LS221 pulse width required");
    }
    bool rxData() const noexcept {return data_;}
    bool idle() const noexcept {return !q_;} // no pending LS221 expiry sample
    bool rxClock() const noexcept {return !q_;}
    std::uint64_t samples=0,inputPulses=0,ignoredRetriggers=0;
    template<class Sample> void advance(std::uint64_t edge,Sample&& sample) {
        if(edge<now_)throw std::invalid_argument("separator time must be monotone");
        if(q_&&until_<=edge){q_=false;++samples;sample(until_,data_);}
        now_=edge;
    }
    // Invoke at the trailing/rising edge of an active-low drive read pulse.
    // Equal expiry/input timestamps settle expiry first; exact propagation
    // coincidence is not calibrated and is excluded from timing tests.
    template<class Sample> void pulse(std::uint64_t edge,Sample&& sample) {
        if(edge>std::numeric_limits<std::uint64_t>::max()-width_)throw std::overflow_error("LS221 expiry exceeds time range");
        advance(edge,sample);++inputPulses;
        data_=q_;
        if(!q_){q_=true;until_=edge+width_;}else ++ignoredRetriggers;
    }
private:
    std::uint64_t width_,now_=0,until_=0;
    bool q_=false,data_=false;
};
}
