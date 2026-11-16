// Differential test: the concrete production register file and staging must
// reproduce the reference components exactly whenever the reference inputs are
// fully known. This is the gate for using them in the fast runtime.
#include "production_registers.h"
#include "controller_registers.h"
#include "controller_alu.h"
#include "production_alu.h"
#include "controller_data.h"
#include "production_bus.h"
#include "dram_cpu.h"
#include "production_dram.h"
#include <iostream>
#include <random>

namespace {
void require(bool v, const char* why) { if (!v) throw std::runtime_error(why); }
}

int main() try {
    std::mt19937 rng(0x5157u);
    // Registers: random contents, every address, read and write agreement.
    eii::production::RegisterFile concrete;
    eii::ControllerRegisterFile reference;
    std::array<std::uint32_t, 64> powerOn{};
    for (auto& cell : powerOn) { cell = rng() & 0xffffff; concrete.write(std::uint8_t(&cell - powerOn.data()), cell); }
    // Reference power-on cells are written through endWrite with known inputs.
    for (unsigned a = 0; a < 64; ++a)
        reference.endWrite({std::uint8_t(a), 63}, {powerOn[a], 0xffffff}, {true, true});
    for (unsigned a = 0; a < 64; ++a) {
        const auto r = reference.read({std::uint8_t(a), 63});
        require(r.known == 0xffffff, "reference cell unexpectedly unknown");
        require(r.value == concrete.read(std::uint8_t(a)), "power-on read mismatch");
    }
    for (unsigned i = 0; i < 200000; ++i) {
        const std::uint8_t a = std::uint8_t(rng() & 63);
        const std::uint32_t v = rng() & 0xffffff;
        concrete.write(a, v);
        reference.endWrite({a, 63}, {v, 0xffffff}, {true, true});
    }
    for (unsigned a = 0; a < 64; ++a)
        require(reference.read({std::uint8_t(a), 63}).value == concrete.read(std::uint8_t(a)), "write/read mismatch");

    // Staging: every port bank and the selected-address rule with known inputs.
    eii::production::Staging cs;
    eii::CpuRegisterStaging rs;
    for (unsigned i = 0; i < 200000; ++i) {
        const std::uint8_t port = std::uint8_t(0x40 + (rng() & 0x3f));
        const std::uint8_t value = std::uint8_t(rng());
        const bool c = cs.write(port, value);
        const bool r = rs.write(port, value);
        require(c == r, "staging accept mismatch");
        require(cs.address == (rs.address.known == 63 ? rs.address.value : cs.address), "staging address mismatch");
        require(cs.data == (rs.data.known == 0xffffff ? rs.data.value : cs.data), "staging data mismatch");
        const std::uint8_t channel = std::uint8_t(rng() & 15);
        const std::uint16_t control = std::uint16_t(rng());
        const bool mpwen = (rng() & 1) != 0;
        const auto ref = rs.selectedAddress({channel, 15}, {std::uint8_t(control), 255}, {true, mpwen});
        // The staging address is unknown until a bank-0 write has happened;
        // compare only where the reference itself is fully known.
        if (mpwen || rs.address.known == 63)
            require(ref.value == cs.selectedAddress(channel, control, mpwen), "selected address mismatch");
    }
    // ALU: a fully known operand and known control pins must reproduce the
    // reference B+A+CI exactly, including bit23 carry leaving the sum field.
    for (unsigned i = 0; i < 200000; ++i) {
        const std::uint32_t b = rng() & 0xfffff;
        const bool c3 = (rng() & 1) != 0, c4a = (rng() & 1) != 0, c4b = (rng() & 1) != 0;
        eii::ControllerDataPipeline data;
        data.low20 = {b, 0xfffff};
        data.ucd4b = {true, c4b};
        const auto ref = eii::controllerAlu(data, {0, 0}, {true, c3}, {true, c4a}, false);
        const auto got = eii::production::alu(b, c3, c4a, c4b);
        require(ref.known == 0x8fffff, "reference ALU result unexpectedly unknown");
        require(ref.value == got, "ALU result mismatch");
    }
    // RFD bus and pipeline: exactly one enabled driver, fully known data.
    for (unsigned i = 0; i < 200000; ++i) {
        std::uint32_t values[3] = {std::uint32_t(rng() & 0xffffff), std::uint32_t(rng() & 0xffffff), std::uint32_t(rng() & 0xffffff)};
        const unsigned active = rng() % 3;
        bool enabled[3] = {false, false, false};
        enabled[active] = true;
        bool any = false;
        const auto got = eii::production::resolveBus(values, enabled, any);
        std::array<eii::RfdDriver, 3> drivers{{
            {{values[0], 0xffffff}, {true, enabled[0]}},
            {{values[1], 0xffffff}, {true, enabled[1]}},
            {{values[2], 0xffffff}, {true, enabled[2]}}}};
        const auto ref = eii::resolveRfdBus(drivers);
        require(any && ref.known == 0xffffff, "reference bus unexpectedly unknown");
        require(ref.value == got, "bus value mismatch");
        require(eii::production::flagsFromBus(got) == eii::inputFlags(ref).value, "flag capture mismatch");

        eii::production::DataPipeline cp;
        eii::ControllerDataPipeline rp;
        const bool raw4 = (rng() & 1) != 0, old9 = (rng() & 1) != 0;
        cp.clock(got, raw4, old9);
        rp.clock(ref, {true, raw4}, {true, old9});
        require(cp.low20 == rp.low20.value && cp.flags == rp.flags.value &&
                cp.ucd4b == rp.ucd4b.high && cp.ucd9 == rp.ucd9.high, "pipeline latch mismatch");
    }
    // DRAM port slice 4a: the CPU-bus invalidation rule.
    {
        eii::DramCpuPort ref;
        eii::production::DramPort port;
        for (unsigned i = 0; i < 100000; ++i) {
            const bool active = (rng() & 1) != 0;
            const std::uint32_t address = rng() & 0x7ffff;
            const std::uint8_t data = std::uint8_t(rng());
            const bool read = (rng() & 1) != 0;
            ref.readReady = port.readReady = true;
            ref.writeReady = port.writeReady = true;
            ref.cpuBus(active, address, {data, 255}, read);
            port.cpuBus(active, address, data, read);
            require(ref.readReady == port.readReady, "readReady invalidation mismatch");
            require(ref.writeReady == port.writeReady, "writeReady invalidation mismatch");
        }
    }
    std::cout << "PASS: concrete register file, staging, ALU, RFD bus, pipeline and DRAM CPU-bus rule match the reference on 900000 randomised known-input operations\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
}

