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
int main(int argc, char **argv) try {
  if (argc < 3 || argc > 4)
    throw std::invalid_argument("usage: production_note_benchmark ROM-folder "
                                "original-bank-image [spans|edges]");
  const std::filesystem::path folder = argv[1];
  auto m = std::make_unique<eii::Machine>();
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

  measure("idle1", 8000000);
  measure("idle2", 8000000);
#ifdef WITH_MIDI
  for (unsigned n = 48; n < 56; ++n) {
    const std::array<std::uint8_t, 3> on{0x90, std::uint8_t(n), 100};
    m->receiveMidi(on);
  }
  measure("note1", 8000000);
  measure("note2", 8000000);
  for (unsigned n = 48; n < 56; ++n) {
    const std::array<std::uint8_t, 3> off{0x80, std::uint8_t(n), 0};
    m->receiveMidi(off);
  }
  measure("release", 8000000);
#endif
  std::cout << "COLLAPSED_CLOCKS "
            << m->channelTimers.controllerTiming.quietClocks << std::endl;
  std::cout << "END memory=" << hash(m->memory)
            << " known=" << hash(m->dramKnown)
            << " scanner=" << hash(m->scannerRam) << std::endl;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
