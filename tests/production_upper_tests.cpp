// Differential test: the concrete upper path must reproduce the single-branch
// fast path of ControllerUpperPaths, including the PROM address construction,
// ack/delayed9 latching, pipeline update, completion clock and request latch.
#include "controller_upper_paths.h"
#include "production_upper.h"
#include <iostream>
#include <random>
#include <vector>

namespace { void require(bool v, const char* w) { if (!v) throw std::runtime_error(w); } }

int main() try {
    std::mt19937 rng(0x77c1u);
    std::array<std::uint8_t, 512> promBytes{};
    for (unsigned i = 0; i < promBytes.size(); ++i) promBytes[i] = std::uint8_t((i * 37u + 11u) & 0xff);
    eii::PromConsensus prom;
    prom.load(promBytes);
    require(prom.loaded(), "synthetic PROM failed to load");

    std::vector<std::uint8_t> memory(0x80000);
    for (std::size_t i = 0; i < memory.size(); ++i) memory[i] = std::uint8_t(rng());

    const std::uint16_t control = 0x0042;   // upper latch value 0x00, one branch
    eii::CpuRequestHandshake cpu;
    cpu.done = {true, false};
    cpu.waitQ = {true, false};
    cpu.requestInactive = {true, true};
    cpu.completionClock({true, false});     // put the completion latch in the known domain
    eii::DramCpuPort dram;

    eii::ControllerUpperPaths ref;
    ref.initialize({control, 0xffff}, {1, 3}, {true, false}, cpu, dram);
    require(ref.branches.size() == 1, "reference did not stay single-branch");

    eii::production::UpperPath con;
    con.upper = std::uint8_t((control >> 8) & 63);
    con.pipeline = 1;
    con.request = false;

    std::vector<std::uint8_t> memKnown(0x80000, 255);
    eii::DramBackingView fullView{&memory, &memKnown};

    for (unsigned step = 0; step < 200000; ++step) {
        const unsigned phase = rng() & 15;
        const unsigned channel = rng() & 15;
        const bool capture = (rng() & 1) != 0;
        const bool input = (rng() & 3) != 0;
        const bool readInactive = (rng() & 1) != 0;
        const std::uint16_t lower = std::uint16_t(rng());
        const std::uint32_t sum = rng() & 0xfffff;
        ref.rising(prom, phase, channel, {true, capture}, {true, input}, false,
                   {true, readInactive}, {lower, 0xffff}, {sum, 0xfffff}, fullView);
        con.rising(prom, phase, channel, capture, input, false, readInactive, lower, sum, memory.data());
        require(ref.branches.size() == 1, "reference branched during rising");
        const auto& b = ref.branches[0];
        require(b.upper.known == 255, "reference upper latch left the known domain");
        require(b.upper.value == con.upper, "upper latch mismatch");
        // The pipeline latch keeps only bits 0..1; the reference's known mask
        // covers exactly those two bits.
        require((b.pipeline.known & 3) == 3 && (b.pipeline.value & 3) == con.pipeline,
                "pipeline mismatch");
        require(b.request.known && b.request.high == con.request, "request mismatch");
        require(b.ack.known && b.ack.high == con.ack, "ack mismatch");
        require(b.delayed9.known && b.delayed9.high == con.delayed9, "delayed9 mismatch");
    }
    std::cout << "PASS: concrete upper path reproduces ControllerUpperPaths fast path over 200000 risings\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
}
