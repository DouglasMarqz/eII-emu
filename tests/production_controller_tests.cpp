// Differential slot test: the concrete controller must continue complete slots
// exactly like ControllerTiming once both start from the same fully known state.
//
// The concrete path represents the single, fully known case: every RFD driver is
// either certain or certain-off and every latch value is known. The reference's
// consensus path (floating bus, contention, partial masks) deliberately has no
// concrete equivalent and stays reference-only, so this test constrains its
// synthetic PROM to that contract and fails loudly if the reference ever leaves
// the fully known set.
#include "production_controller.h"
#include "controller_timing.h"
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

namespace { void require(bool v, const char* w) { if (!v) throw std::runtime_error(w); } }

int main() try {
    std::mt19937 rng(0x1234u);
    // Synthetic PROM image. The low byte carries the control word for the NEXT
    // edge: bit2 is the register write aperture (UCD02) and bit6 is ALU /OE
    // (UCD06.1). For the concrete path exactly one of the SRAM/ALU must drive:
    //   enabled = (phase4 || !channel3) && bit2   (register drives when !enabled)
    //   ALU drives when !bit6
    // so choose bit6 = !enabled for the edge that will consume this byte. In the
    // ninth-slot phases0..3 the SRAM is always selected, so bit2 is forced low
    // there to keep channel7→8 and channel8→0 consistent at the slot boundary.
    eii::PromLatch image;
    for (unsigned i = 0; i < 512; ++i) {
        image.upper[i] = std::uint8_t((i * 37u + 11u) & 0xff);
        std::uint8_t low = std::uint8_t((i * 53u + 7u + (i >> 2) * 17u) & 0xff);
        const unsigned phase = (i >> 5) & 7u;
        const bool ch3 = (i & 0x100u) != 0;
        unsigned nextPhase;
        bool nextCh3;
        if (phase == 7) { nextPhase = 8; nextCh3 = false; }
        else { nextPhase = phase + 1; nextCh3 = ch3; }
        if (!ch3 && phase == 7) low = std::uint8_t(low & ~4u);
        const bool nextEnabled = (!nextCh3 || ((nextPhase & 4u) != 0)) && ((low & 4u) != 0);
        if (nextEnabled) low = std::uint8_t(low & ~0x40u);
        else             low = std::uint8_t(low | 0x40u);
        image.lower[i] = low;
    }

    std::vector<std::uint8_t> memory(0x80000, 0), known(0x80000, 255);
    eii::DramBackingView view{&memory, &known};

    eii::ControllerTiming ref(8);
    ref.loadProms(image);
    ref.attachCpuDram(view);
    ref.enableRegisterWrites();
    ref.seedRegistersForTest(0xf00000);
    ref.reset(false);   // the controller powers up held in reset

    // Warm the reference up so its sequencer state is meaningful, then place
    // every latch the concrete path assumes into an explicit known state.
    std::array<eii::LogicLevel, 8> requests{};
    for (auto& r : requests) r = {true, false};
    for (unsigned i = 0; i < 64; ++i) ref.risingClock(requests, {0, 15});

    ref.seedDataPipelineForTest(0, 0, (ref.controlForTest().value & 16u) != 0, false);
    ref.seedAluOutputForTest(0);
    ref.seedPipelineForTest(ref.pipelineForTest());
    ref.seedCapturedForTest(ref.capturedForTest());
    ref.seedWritePortForTest(false, false);   // IC92 idle: no pending CPU write
    for (unsigned i = 0; i < 16 && ref.capturedKnownForTest() != 31; ++i)
        ref.risingClock(requests, {0, 15});
    require(ref.capturedKnownForTest() == 31,
            "reference captured known mask is not 31 after seeding");

    // Build the concrete controller from the reference's observable state.
    // PROM tables share the same image.
    eii::production::Controller con;
    con.proms = &ref.tablesForTest();
    con.memory = memory.data();
    con.control = ref.controlForTest().value;
    con.phase = ref.phaseForTest();
    con.channel = std::uint8_t(ref.channelForTest().value);
    con.shift = ref.shiftForTest();
    con.captured = ref.capturedForTest();
    con.pipeline = ref.pipelineForTest();
    con.data.low20 = 0;
    con.data.flags = 0;
    con.data.ucd4b = (ref.controlForTest().value & 16u) != 0;
    con.data.ucd9 = false;
    con.aluOutput.output = 0;
    con.aluOutput.clock = false;
    // The upper latch is the high byte of the reference control word.
    con.upper.upper = std::uint8_t(ref.controlForTest().value >> 8);
    con.upper.pipeline = ref.pipelineForTest();
    con.upper.request = ref.requestForTest();
    con.upper.ack = ref.upperLatchForTest();
    for (unsigned a = 0; a < 64; ++a) {
        const auto cell = ref.registerForTest(std::uint8_t(a));
        con.registers.write(std::uint8_t(a), cell.value);
    }

    std::array<bool, 8> crequests{};
    bool seenControl[65536] = {};
    bool seenPhase[16] = {}, seenChannel[16] = {};
    unsigned distinctControls = 0, registerDriven = 0, aluDriven = 0;
    for (unsigned step = 0; step < 20000; ++step) {
        // The synthetic PROM must keep exactly one known driver per edge.
        const bool enabled = ((con.control & 4u) != 0) &&
            (((con.channel & 8) == 0) || ((con.phase & 4u) != 0));
        const bool aluDrive = (con.control & 64u) == 0;
        require(aluDrive == enabled, "synthetic PROM selected zero or two RFD drivers");
        (aluDrive ? aluDriven : registerDriven)++;

        for (unsigned i = 0; i < 8; ++i) {
            const bool level = (rng() & 7) == 0;
            crequests[i] = level;
            requests[i] = {true, level};
        }
        ref.risingClock(requests, {0, 15});
        con.rising(crequests, 0);
        // Complete the slot: the falling edge opens the low write aperture and
        // clocks the DRAM/ALU-address side of both controllers.
        ref.fallingClock();
        con.falling();

        const auto rc = ref.controlForTest();
        const auto rp = ref.pipelineForTest();
        const auto rdp = ref.dataPipelineValue();
        const auto rao = ref.aluOutputValue();
        const auto rclk = ref.aluClockForTest();
        bool registersMatch = true;
        for (unsigned a = 0; a < 64; ++a) {
            const auto cell = ref.registerForTest(std::uint8_t(a));
            if (cell.known != 0xffffff || cell.value != con.registers.read(std::uint8_t(a)))
                registersMatch = false;
        }
        const bool fullyKnown = rc.known == 0xffff && ref.capturedKnownForTest() == 31 &&
            rdp.known == 0xfffff && rao.known == 0xffffff && rclk.known;
        const bool match = rc.value == con.control && ref.phaseForTest() == con.phase &&
            ref.channelForTest().value == con.channel && ref.shiftForTest() == con.shift &&
            ref.capturedForTest() == con.captured && rp == con.pipeline &&
            ref.requestForTest() == con.upper.request &&
            ref.upperLatchForTest() == con.upper.ack &&
            rdp.value == con.data.low20 && rao.value == con.aluOutput.output &&
            rclk.high == con.aluOutput.clock &&
            ref.diagLowValue == con.lastLowerWord && registersMatch;
        if (!fullyKnown || !match) {
            std::cout << "DIVERGE step=" << step
                      << " fullyKnown=" << fullyKnown
                      << " ref control=0x" << std::hex << unsigned(rc.value)
                      << " (known 0x" << unsigned(rc.known) << ")"
                      << " phase=" << unsigned(ref.phaseForTest())
                      << " ch=" << unsigned(ref.channelForTest().value)
                      << " shift=0x" << unsigned(ref.shiftForTest())
                      << " cap=0x" << unsigned(ref.capturedForTest())
                      << " capKnown=0x" << unsigned(ref.capturedKnownForTest())
                      << " pipe=0x" << unsigned(rp)
                      << " req=" << ref.requestForTest() << " ack=" << ref.upperLatchForTest()
                      << " low20=0x" << rdp.value << "/0x" << rdp.known
                      << " alu=0x" << rao.value << "/0x" << rao.known
                      << " clk=" << rclk.known << "/" << rclk.high
                      << " lowAddr=0x" << unsigned(ref.diagLowValue)
                      << " con control=0x" << unsigned(con.control)
                      << " phase=" << unsigned(con.phase)
                      << " ch=" << unsigned(con.channel)
                      << " shift=0x" << unsigned(con.shift)
                      << " cap=0x" << unsigned(con.captured)
                      << " pipe=0x" << unsigned(con.pipeline)
                      << " req=" << con.upper.request << " ack=" << con.upper.ack
                      << " low20=0x" << con.data.low20
                      << " alu=0x" << con.aluOutput.output
                      << " clk=" << con.aluOutput.clock
                      << " lowAddr=0x" << unsigned(con.lastLowerWord)
                      << " regs=" << registersMatch << std::dec << "\n";
            std::cerr << "FAIL: diverged at step " << step << "\n";
            return 1;
        }
        if (!seenControl[rc.value]) { seenControl[rc.value] = true; ++distinctControls; }
        seenPhase[ref.phaseForTest()] = true;
        seenChannel[ref.channelForTest().value] = true;
    }
    // At least one distinct control word per phase and RFD-driver mode; the
    // phase/channel checks below prove the run visits the whole scan.
    require(distinctControls >= 32, "too few distinct control words for a meaningful run");
    require(registerDriven > 0 && aluDriven > 0, "both RFD driver paths must be exercised");
    for (unsigned i = 8; i < 16; ++i) require(seenPhase[i], "not every counter phase was exercised");
    // The scan uses channel bit3 as the ninth-slot flag and low3 as CHSL, so it
    // visits 0..8, never 9..15.
    for (unsigned i = 0; i < 9; ++i) require(seenChannel[i], "not every scan channel was exercised");
    std::cout << "PASS: concrete controller tracks ControllerTiming over 20000 complete slots ("
              << distinctControls << " control words, " << registerDriven
              << " register-driven and " << aluDriven << " ALU-driven edges)\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
}
