#include "controller_upper_paths.h"
#include "reference/upper_paths_035.h"
#include <iostream>
static void require(bool b, const char *why) {
    if (!b)
        throw std::runtime_error(why);
}
static unsigned state = 0x6a09e667;
static unsigned randomWord() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
static eii::LogicLevel pin() {
    const auto n = randomWord();
    return {bool(n % 9), bool(n & 1)};
}
int main(int argc,char** argv) try {
    const bool quick=argc==2&&std::string(argv[1])=="--quick";
    const unsigned fixtures=quick?3:12, clocks=quick?256:1024;
    std::array<std::uint8_t, 512> bytes{};
    for (auto &b : bytes)
        b = std::uint8_t(randomWord());
    eii::PromConsensus prom;
    prom.load(bytes);
    std::vector<std::uint8_t> ram(0x80000), known(ram.size(), 255);
    for (auto &b : ram)
        b = std::uint8_t(randomWord());
    eii::DramBackingView memory{&ram, &known};
    unsigned single = 0, multiple = 0;
    for (unsigned fixture = 0; fixture < fixtures; ++fixture) {
        eii::ControllerUpperPaths fast;
        eii::ReferenceUpperPaths reference;
        const eii::KnownWord control{0, 0};
        const eii::KnownByte pipeline{0, 3};
        const eii::LogicLevel request{true, false};
        fast.initialize(control, pipeline, request, {}, {});
        reference.initialize(control, pipeline, request, {}, {});
        for (unsigned t = 0; t < clocks; ++t) {
            const auto inactive = pin(), readInactive = pin(), input = pin();
            const eii::LogicLevel capture{t % 37 != 0, t % 5 == 0};
            const bool reset = t % 113 == 0;
            const unsigned phase = t % 16, channel = (t / 16) % 9;
            const eii::KnownWord lower{std::uint16_t(randomWord() & 255), 255};
            const eii::Known24 sum{randomWord() & 0xffffff, 0xffffff};
            const auto address = randomWord() & 0x7ffff;
            const eii::KnownByte data{std::uint8_t(randomWord()), 255};
            const bool active = t % 7 < 4, read = t % 3 != 0;
            fast.driveCpu(inactive);
            reference.driveCpu(inactive);
            fast.driveDram(active, address, data, read);
            reference.driveDram(active, address, data, read);
            if (reset) {
                fast.reset();
                reference.reset();
            }
            fast.falling(lower, memory);
            reference.falling(lower, memory);
            fast.rising(prom, phase, channel, capture, input, reset, readInactive, lower, sum,
                        memory);
            reference.rising(prom, phase, channel, capture, input, reset, readInactive, lower, sum,
                             memory);
            fast.cpuClock();
            reference.cpuClock();
            require(fast.branches.size() == reference.branches.size(), "branch coverage changed");
            single += fast.branches.size() == 1;
            multiple += fast.branches.size() > 1;
            for (unsigned i = 0; i < fast.branches.size(); ++i) {
                const auto &a = fast.branches[i];
                const auto &b = reference.branches[i];
                require(a.upper == b.upper && a.pipeline == b.pipeline && a.request == b.request &&
                            a.ack == b.ack && a.delayed9 == b.delayed9 && a.cpu == b.cpu &&
                            a.dram.sameState(b.dram) && a.dac.sameClockState(b.dac),
                        "future correlated state differs from0.35");
                require(a.dac.codes == b.dac.codes, "DAC outputs changed");
                require(a.dram.sampleReads == b.dram.sampleReads && a.dram.reads == b.dram.reads &&
                            a.dram.writes == b.dram.writes &&
                            a.dram.readCaptures == b.dram.readCaptures,
                        "diagnostic counts changed");
            }
        }
    }
    require(single && multiple, "both concrete and branched histories exercised");
    std::cout << "PASS:"<<fixtures*clocks<<" randomized upper-controller clocks vs frozen0.35 oracle, single="
              << single << " multiple=" << multiple << ";DRAM/latches/DAC/counters agree.\n";
} catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
}
