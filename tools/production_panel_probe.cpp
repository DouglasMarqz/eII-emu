// Caller-supplied ROMs/media only. Seeded boot, two audible MIDI notes, then
// Play/Stop/Filter/Select compared to the reference at 20k CPU-tick boundaries.
// PCM equality is bitwise; this is not an analog calibration or Windows UI
// test.
#include "floppy_hfe.h"
#include "machine.h"
#include <bit>
#include <iostream>
namespace eii {
struct ControllerRuntimeTestAccess {
  static unsigned invalidCode(const ControllerTiming &c) {
    return c.production_ ? c.production_->invalidCode : 0;
  }
  static Known24 address(const ControllerTiming &c) {
    return c.production_ ? c.production_->upper.dram.aluAddress.output
                         : c.paths_[0].upper.branches[0].dram.aluAddress.output;
  }
};
} // namespace eii
struct Audio {
  std::uint64_t hash = 14695981039346656037ull, frames = 0;
  double peak = 0;
  void put(auto fs) {
    for (auto f : fs) {
      hash ^= std::bit_cast<std::uint32_t>(f.left);
      hash *= 1099511628211ull;
      ++frames;
      peak = std::max(peak, double(std::abs(f.left)));
    }
  }
};
static void show(const char *tag, const eii::Machine &m, const Audio &a) {
  std::cout << tag << " ticks=" << m.ticks << " lcd=" << m.lcd.line(0) << " / "
            << m.lcd.line(1) << " pc=" << std::hex << m.main.cpu.pc << std::dec
            << " invalid="
            << eii::ControllerRuntimeTestAccess::invalidCode(
                   m.channelTimers.controllerTiming)
            << " audio=" << a.hash << " peak=" << a.peak << "\n"
            << std::flush;
}
int main(int argc, char **argv) try {
  if (argc != 3) {
    std::cerr << "usage: production_panel_probe ROMS IMAGE\n";
    return 2;
  }
  const std::filesystem::path p = argv[1];
  auto bytes = eii::readFile(argv[2]);
  if (bytes.size() >= 8 &&
      std::equal(bytes.begin(), bytes.begin() + 8, "HXCPICFE"))
    bytes = eii::HfeImage(std::move(bytes)).extractEmuii();
  auto image = std::make_shared<const eii::EmuiiImage>(std::move(bytes));
  Audio ar, ap;
  auto make = [&](bool production, Audio &a) {
    auto m = std::make_unique<eii::Machine>(0);
    m->load(p / "main.bin", p / "scanner.bin", p / "ic135.bin",
            p / "ic137.bin");
    m->initializeDramPowerOn(1);
    m->enableProductionController(production);
    m->initializeControllerPowerOn(0xe1122026u);
    m->enableNormalRuntime();
    m->floppy.attach(0, 37, 400000);
    m->floppy.drives[0]->insertSensorDisk(true);
    m->diskRead = std::make_unique<eii::DiskReadPath>(87, 8);
    m->diskRead->mount(0, image, eii::EmuiiGap::Hxc2026);
    m->enableRuntimeAudio([&a](auto fs) { a.put(fs); });
    return m;
  };
  auto r = make(false, ar), c = make(true, ap);
  r->run(200000000);
  show("BOOT reference", *r, ar);
  c->run(200000000);
  show("BOOT production", *c, ap);
  auto compare = [&](const char *stage) {
    bool equal = r->main.pins == c->main.pins &&
                 r->scanner.pins == c->scanner.pins &&
                 r->main.cpu.pc == c->main.cpu.pc &&
                 r->scanner.cpu.pc == c->scanner.cpu.pc && ar.hash == ap.hash &&
                 r->lcd.ram == c->lcd.ram;
    for (unsigned i = 0; i < 64; ++i)
      equal &= r->channelTimers.controllerTiming.registerValue(i) ==
               c->channelTimers.controllerTiming.registerValue(i);
    for (unsigned i = 0; i < 8; ++i)
      equal &= r->channelTimers.controllerTiming.channelDacCode(i) ==
               c->channelTimers.controllerTiming.channelDacCode(i);
    if (!equal || c->channelTimers.controllerTiming.productionEngineInvalid()) {
      show(stage, *r, ar);
      show("DIVERGED production", *c, ap);
      auto ra = eii::ControllerRuntimeTestAccess::address(
               r->channelTimers.controllerTiming),
           pa = eii::ControllerRuntimeTestAccess::address(
               c->channelTimers.controllerTiming);
      std::cout << "ADDRESS ref=" << std::hex << ra.value << "/" << ra.known
                << " production=" << pa.value << "/" << pa.known << std::dec
                << "\n";
      return false;
    }
    return true;
  };
  if (!compare("BOOT"))
    return 1;
  const std::array<std::uint8_t, 3> onA{0x90, 60, 100}, onB{0x90, 64, 96};
  r->receiveMidi(onA);
  c->receiveMidi(onA);
  for (unsigned i = 0; i < 100; ++i) {
    r->run(20000);
    c->run(20000);
    if (!compare("NOTE60 reference"))
      return 1;
  }
  r->receiveMidi(onB);
  c->receiveMidi(onB);
  for (unsigned i = 0; i < 100; ++i) {
    r->run(20000);
    c->run(20000);
    if (!compare("NOTE64 reference"))
      return 1;
  }
  show("TWO NOTES equal", *c, ap);
  for (auto *m : {r.get(), c.get()})
    m->scannerControls.setButton(eii::ScannerButton::Play, true);
  for (unsigned i = 0; i < 200; ++i) {
    r->run(20000);
    c->run(20000);
    if (i == 39)
      for (auto *m : {r.get(), c.get()})
        m->scannerControls.setButton(eii::ScannerButton::Play, false);
    if (!compare("PLAY reference"))
      return 1;
  }
  show("PLAY equal", *c, ap);
  const std::array<std::uint8_t, 3> note{0x90, 60, 100};
  r->receiveMidi(note);
  c->receiveMidi(note);
  for (unsigned i = 0; i < 200; ++i) {
    r->run(20000);
    c->run(20000);
    if (!compare("MIDI reference"))
      return 1;
  }
  show("MIDI equal", *c, ap);
  for (auto button : {eii::ScannerButton::Stop, eii::ScannerButton::Filter,
                      eii::ScannerButton::Select}) {
    for (auto *m : {r.get(), c.get()})
      m->scannerControls.setButton(button, true);
    for (unsigned i = 0; i < 200; ++i) {
      r->run(20000);
      c->run(20000);
      if (i == 39)
        for (auto *m : {r.get(), c.get()})
          m->scannerControls.setButton(button, false);
      if (!compare("PANEL reference"))
        return 1;
    }
    show("PANEL equal", *c, ap);
  }
  std::cout << "PASS boot/actual PCM/Play/Stop/Filter/Select compare\n";
} catch (const std::exception &e) {
  std::cerr << e.what() << "\n";
  return 1;
}
