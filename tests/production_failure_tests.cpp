#include "machine.h"
#include <iostream>
#include <stdexcept>
static void require(bool b, const char *why) {
  if (!b)
    throw std::runtime_error(why);
}
static auto fixture() {
  auto m = std::make_unique<eii::Machine>(0);
  eii::PromLatch p;
  p.upper.fill(0);
  p.lower.fill(0x40);
  for (unsigned a = 0; a < 512; ++a)
    if ((a & 256) && ((a >> 5) & 7) == 2)
      p.lower[a] |= 4;
  m->channelTimers.controllerTiming.loadProms(p);
  m->enableCpuDram();
  m->initializeDramPowerOn(1);
  m->enableProductionController();
  m->initializeControllerPowerOn(0xe1122026u);
  m->enableNormalRuntime();
  return m;
}
int main() try {
  {
    auto m = fixture();
    m->channelTimers.controllerTiming.driveCpuBus({false, false}, {true, true});
    bool stopped = false;
    try {
      m->run(1);
    } catch (const std::runtime_error &e) {
      const std::string s = e.what();
      stopped =
          s.find("unknown CPU input") != s.npos && s.find("phase") != s.npos;
    }
    require(stopped && m->ticks == 0 && m->productionControllerEnabled(),
            "invalid controller must stop before execution without replacing "
            "the engine");
  }
  {
    auto m = fixture();
    m->run(1000);
    require(m->ticks == 1000 &&
                !m->channelTimers.controllerTiming.productionEngineInvalid(),
            "valid concrete fixture runs");
    for (bool enabled : {false, true}) {
      bool bad = false;
      try {
        m->enableProductionController(enabled);
      } catch (const std::logic_error &) {
        bad = true;
      }
      require(bad && m->productionControllerEnabled(),
              "live engine switch must reject stale reference state");
    }
  }
  {
    eii::production::Controller c;
    c.control = 0x4a;
    c.phase = 8;
    c.channel = 0;
    c.aluOutput.output = 0x22;
    c.registers.fill(0x11);
    c.control = 0;
    std::array<bool, 8> q{};
    c.rising(q, 0);
    require(c.invalid && c.invalidCode == 6, "RFD contention must invalidate");
    require(c.risingEdges == 1 && c.data.low20 == 0,
            "invalid bus must not be latched as a chosen known value");
    c.rising(q, 0);
    require(c.risingEdges == 1, "failed engine must not keep advancing");
  }
  // Exercise real sample row/column strobes against an unknown backing byte.
  // The CPU bus is inactive: checking only the CPU request address misses it.
  std::vector<std::uint8_t> bytes(0x80000), known(bytes.size(), 255);
  const unsigned a = 0x75324;
  bytes[a] = 0xb6;
  known[a] = 0;
  eii::production::DramPort d;
  d.memoryKnown = known.data();
  auto rise = [&](unsigned ctl) { d.rising(ctl, a, bytes.data()); };
  auto fall = [&](unsigned ctl) { d.falling(ctl, false, bytes.data()); };
  rise(0x80);
  fall(0x80);
  rise(0x80);
  fall(0x80);
  rise(0x80);
  d.settle(0x8080, bytes.data());
  fall(0x8080);
  rise(0x8080);
  d.settle(0x9880, bytes.data());
  fall(0x9880);
  rise(0x9880);
  require(d.memoryFault && !d.mdoKnown,
          "unknown sample CAS must not invent known data");
  require(d.memoryFaultAddress==a && !d.memoryFaultCpu,
          "first fault records exact physical sample CAS address and ownership");
  std::cout << "PASS: production failures stop before CPU execution; explicit "
               "first-fault reason, no live reference switch, no contention "
               "latch and unknown sample-CAS rejection\n";
} catch (const std::exception &e) {
  std::cerr << e.what() << "\n";
  return 1;
}
