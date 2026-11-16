// Differential test: the concrete DRAM port must reproduce DramCpuPort for the
// same known control sequence, CPU bus state and backing memory. Every public
// observation plus the internal validity records are compared at every step.
#include "dram_cpu.h"
#include "production_dram.h"
#include <iostream>
#include <random>
#include <vector>

namespace {
void require(bool v, const char* why) { if (!v) throw std::runtime_error(why); }
}

int main() try {
    std::mt19937 rng(0xd7a3u);
    std::vector<std::uint8_t> bytes(0x80000), known(0x80000, 255);
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = std::uint8_t(rng());
    eii::DramBackingView view{&bytes, &known};
    eii::DramCpuPort ref;
    eii::production::DramPort con;
    std::uint32_t address = 0x2800;
    for (unsigned step = 0; step < 200000; ++step) {
        const bool active = (rng() & 3) != 0;
        const bool read = (rng() & 1) != 0;
        const std::uint8_t data = std::uint8_t(rng());
        address = (address + std::uint32_t(rng() % 7)) & 0x7ffff;
        ref.cpuBus(active, address, {data, 255}, read);
        con.cpuBus(active, address, data, read);
        const unsigned control = rng() & 0xffff;
        const bool delayed9 = (rng() & 1) != 0;
        const std::uint32_t sum = rng() & 0xfffff;
        if (rng() & 1) {
            ref.falling({std::uint16_t(control), 65535}, {true, delayed9}, view);
            con.falling(control, delayed9, bytes.data());
        } else {
            ref.rising({std::uint16_t(control), 65535}, {sum, 0xfffff}, view);
            con.rising(control, sum, bytes.data());
        }
        const auto rAlu = ref.aluOutputForTest();
        const auto cAlu = con.aluOutputForTest();
        require(rAlu.value == cAlu.value && rAlu.known == cAlu.known, "ALU latch divergence");
        require(ref.rowValidForTest() == con.rowValidForTest(), "row validity divergence");
        require(ref.casAddressValidForTest() == con.casAddressValidForTest(), "CAS address validity divergence");
        require(ref.mdoForTest() == con.mdoForTestValue(), "MDO divergence");
        require(ref.mdoCurrentForTest() == con.mdoCurrentForTest(), "MDO current divergence");
        require(ref.rasForTest() == con.rasForTest() && ref.casForTest() == con.casForTest(), "RAS/CAS level divergence");
        require(ref.rasQForTest() == con.rasQForTest() && ref.casQForTest() == con.casQForTest(), "RASQ/CASQ divergence");
        require(ref.readLatch.value == con.readLatch && ref.channelLatch.value == con.channelLatch, "latch divergence");
        require(ref.writeAddress == con.writeAddress && ref.writeData.value == con.writeData, "write aperture divergence");
        require(ref.readReady == con.readReady && ref.writeReady == con.writeReady, "ready divergence");
        require(ref.reads == con.reads && ref.writes == con.writes &&
                ref.readCaptures == con.readCaptures, "counter divergence");
    }
    std::cout << "PASS: concrete DRAM port reproduces DramCpuPort over 200000 bus/control/memory steps\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
}
