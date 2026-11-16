// Differential test: the concrete IC92 handshake and IC115 write enable must
// reproduce the reference components whenever every level is known.
#include "controller_registers.h"
#include "production_write.h"
#include <iostream>
#include <random>

namespace { void require(bool v, const char* w) { if (!v) throw std::runtime_error(w); } }

int main() try {
    std::mt19937 rng(0x3c9du);
    eii::RegisterWriteHandshake ref;
    eii::production::WriteHandshake con;
    ref.pending = {true, false};
    ref.flag = {true, false};
    ref.mpwenN = {true, true};
    for (unsigned step = 0; step < 400000; ++step) {
        const unsigned op = rng() % 3;
        if (op == 0) {
            const bool ch3 = (rng() & 1) != 0, st3 = (rng() & 1) != 0;
            ref.settle({true, ch3}, {true, st3});
            con.settle(ch3, st3);
        } else if (op == 1) {
            ref.cpuStrobe();
            con.cpuStrobe();
        } else {
            ref.sourceClock();
            con.sourceClock();
        }
        require(ref.pending.known && ref.flag.known && ref.mpwenN.known, "reference left the known domain");
        require(ref.pending.high == con.pending, "pending mismatch");
        require(ref.flag.high == con.flag, "flag mismatch");
        require(ref.mpwenN.high == con.mpwenN, "mpwenN mismatch");
        const bool clockHigh = (rng() & 1) != 0, ch3 = (rng() & 1) != 0;
        const bool ucnt2 = (rng() & 1) != 0, ucd2 = (rng() & 1) != 0;
        const auto refEn = eii::registerWriteEnable({true, clockHigh}, ref.mpwenN,
                                                   {true, ch3}, {true, ucnt2}, {true, ucd2});
        require(refEn.known, "reference enable unexpectedly unknown");
        require(refEn.high == eii::production::writeEnable(clockHigh, con.mpwenN, ch3, ucnt2, ucd2),
                "write enable mismatch");
    }
    std::cout << "PASS: concrete IC92 handshake and write enable reproduce the reference over 400000 operations\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
}
