#define WITH_MIDI 1
#include "floppy_hfe.h"
#include "machine.h"
#include <chrono>
#include <iomanip>
#include <iostream>
static std::uint64_t hash(const auto &data) {
  std::uint64_t h = 14695981039346656037ull;
  for (auto b : data) {
    h ^= b;
    h *= 1099511628211ull;
  }
  return h;
}
std::unique_ptr<eii::Machine> machine;
int main(int argc, char **argv) try {
  if (argc < 3 || argc > 4)
    throw std::invalid_argument("usage: preset02_fault_probe ROM-folder "
                                "original-bank-image [spans|edges]");
  const std::filesystem::path folder = argv[1];
  machine = std::make_unique<eii::Machine>();
  auto& m=machine;
  // Match the GUI concrete startup and use caller-supplied media only.
  m->channelTimers.controllerTiming = eii::ControllerTiming(0);
  m->channelTimers.enableControllerTiming();
  m->channelTimers.controllerTiming.enableRegisterWrites();
  m->load(folder / "main.bin", folder / "scanner.bin", folder / "ic135.bin",
          folder / "ic137.bin");
  m->initializeDramPowerOn(1);
  m->enableProductionController();
  m->initializeControllerPowerOn(0xe1122026u);
  m->enableNormalRuntime();
  if (argc == 4) {
    const std::string mode = argv[3];
    if (mode != "spans" && mode != "edges")
      throw std::invalid_argument("expected spans or edges");
    m->channelTimers.enableControllerSpans(mode == "spans");
  }
  auto bytes = eii::readFile(argv[2]);
  if (bytes.size() >= 8 &&
      std::equal(bytes.begin(), bytes.begin() + 8, "HXCPICFE"))
    bytes = eii::HfeImage(std::move(bytes)).extractEmuii();
  m->floppy.attach(0, 37, 400000);
  m->floppy.drives[0]->insertSensorDisk(true);
  m->diskRead = std::make_unique<eii::DiskReadPath>(87, 8);
  m->diskRead->mount(0,
                     std::make_shared<const eii::EmuiiImage>(std::move(bytes)),
                     eii::EmuiiGap::Hxc2026);

  double peak = 0;
  std::uint64_t frames = 0;
  m->enableRuntimeAudio([&](auto block) {
    frames += block.size();
    for (auto f : block)
      peak = std::max(peak, double(std::abs(f.left)));
  });
  const auto measure = [&](const char *label, std::uint64_t ticks) {
    const auto start = std::chrono::steady_clock::now();
    const auto before = m->channelTimers.controllerTiming.voiceTransactions;
    m->run(ticks);
    const double wall =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();
    std::cout << label << " ticks=" << ticks << " wall=" << wall
              << " voiceTransactions="
              << m->channelTimers.controllerTiming.voiceTransactions - before
              << " frames=" << frames << " peak=" << peak << std::endl;
  };
  measure("boot", 200000000);
  const auto press = [&](eii::ScannerButton b) {
    m->scannerControls.setButton(b,true);m->run(200000);
    m->scannerControls.setButton(b,false);m->run(200000);
  };
  press(eii::ScannerButton::Digit0);press(eii::ScannerButton::Digit2);
  m->run(4000000);
  std::cout << "PRESET [" << m->lcd.line(0) << "][" << m->lcd.line(1) << "]" << std::endl;
  // Deterministic host stress, not a replay of the unavailable user's events.
  for(unsigned round=0;round<8;++round) {
    for(unsigned note=24;note<108;++note) {
      const double wheel = (round&1) ? double(note%32)/31.0*5 : 2.5;
      m->scannerControls.setVoltage(eii::ScannerAnalogInput::LeftWheel,wheel);
      m->scannerControls.setVoltage(eii::ScannerAnalogInput::RightWheel,double(note%16)/15.0*5);
      const std::array<std::uint8_t,3> on{0x90,std::uint8_t(note),100};
      m->receiveMidi(on);
      m->run(100000);
      const std::array<std::uint8_t,3> off{0x80,std::uint8_t(note),0};
      m->receiveMidi(off);
      m->run(100000);
    }
    std::cout << "ROUND " << round << " tick=" << m->ticks << std::endl;
  }
  std::cout << "COLLAPSED_CLOCKS "
            << m->channelTimers.controllerTiming.quietClocks << std::endl;
  std::cout << "END memory=" << hash(m->memory)
            << " known=" << hash(m->dramKnown)
            << " scanner=" << hash(m->scannerRam) << std::endl;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  if(machine)std::cerr << machine->report();
  return 1;
}
