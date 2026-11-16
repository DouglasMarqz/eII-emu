// Differential test: the concrete CPU request pipeline must reproduce
// CpuRequestHandshake whenever every input level is known.
#include "controller_timing_types.h"
#include "production_request.h"
#include <iostream>
#include <random>

namespace {
void require(bool v, const char* why) { if (!v) throw std::runtime_error(why); }
}

int main() try {
    std::mt19937 rng(0x9b21u);
    eii::CpuRequestHandshake ref;
    eii::production::RequestHandshake con;
    // Start both from the same known state.
    ref.done = {true, false};
    ref.waitQ = {true, false};
    ref.requestInactive = {true, true};
    // The completion latch powers up unknown in the reference; clock it once so
    // both sides start in the known domain.
    ref.completionClock({true, false});
    con.completionClock(false);
    for (unsigned step = 0; step < 400000; ++step) {
        const unsigned op = rng() % 3;
        if (op == 0) {
            const bool inactive = (rng() & 1) != 0;
            ref.drive({true, inactive});
            con.drive(inactive);
        } else if (op == 1) {
            const bool level = (rng() & 1) != 0;
            ref.completionClock({true, level});
            con.completionClock(level);
        } else {
            ref.cpuClock();
            con.cpuClock();
        }
        require(ref.done.known && ref.waitQ.known && ref.requestInactive.known,
                "reference left the known domain");
        require(ref.done.high == con.done, "done mismatch");
        require(ref.waitQ.high == con.waitQ, "waitQ mismatch");
        require(ref.requestInactive.high == con.requestInactive, "requestInactive mismatch");
        require(ref.requestToPipeline().high == con.requestToPipeline(), "requestToPipeline mismatch");
        require(ref.waitInactive().high == con.waitInactive(), "waitInactive mismatch");
        require(ref.completionKnown() == con.completionKnown(), "completionKnown mismatch");
    }
    std::cout << "PASS: concrete CPU request pipeline reproduces CpuRequestHandshake over 400000 operations\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
}
