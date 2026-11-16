// Original Grand Piano bank reload regression; assets are caller-supplied.
// Disturb one sample RAM byte in this probe only, then require firmware disk
// reads to restore the full sample-region hash and return to the preset LCD.
#include "floppy_hfe.h"
#include "machine.h"
#include <iostream>
int main(int argc, char **argv) try {
  if (argc < 3) {
    std::cerr << "usage: bank_reload_probe ROMS HFE [production|reference] "
                 "[reload]\n";
    return 2;
  }
  auto m = std::make_unique<eii::Machine>(0);
  const std::filesystem::path p = argv[1];
  m->load(p / "main.bin", p / "scanner.bin", p / "ic135.bin", p / "ic137.bin");
  m->initializeDramPowerOn(1);
  const bool prod = argc < 4 || std::string(argv[3]) != "reference";
  m->enableProductionController(prod);
  m->initializeControllerPowerOn(0xe1122026u);
  m->enableNormalRuntime();
  auto bytes = eii::HfeImage(eii::readFile(argv[2])).extractEmuii();
  m->floppy.attach(0, 37, 400000);
  m->floppy.drives[0]->insertSensorDisk(true);
  m->diskRead = std::make_unique<eii::DiskReadPath>(87, 8);
  m->diskRead->mount(0,
                     std::make_shared<const eii::EmuiiImage>(std::move(bytes)),
                     eii::EmuiiGap::Hxc2026);
  auto show = [&](const char *s) {
    std::cout << s << " ticks=" << m->ticks << " LCD=[" << m->lcd.line(0)
              << "][" << m->lcd.line(1) << "] PCs=" << std::hex
              << m->main.cpu.pc << "/" << m->scanner.cpu.pc << std::dec
              << " HALT=" << bool(m->main.pins & Z80_HALT) << "/"
              << bool(m->scanner.pins & Z80_HALT)
              << " WAIT=" << bool(m->main.pins & Z80_WAIT)
              << " INT=" << bool(m->main.pins & Z80_INT)
              << " PIO=" << m->mainToScanTransfers << "/"
              << m->scanToMainTransfers << " ready=" << m->main.pio[0].ready
              << m->main.pio[1].ready << "/" << m->scanner.pio[0].ready
              << m->scanner.pio[1].ready
              << " pending=" << m->main.pio[0].pending << m->main.pio[1].pending
              << "/" << m->scanner.pio[0].pending << m->scanner.pio[1].pending
              << " production=" << m->productionControllerEnabled()
              << " invalid="
              << m->channelTimers.controllerTiming.productionEngineInvalid()
              << std::endl;
  };
  m->run(200000000);
  show("BOOT");
  auto pulse = [&](eii::ScannerButton b, const char *tag) {
    m->scannerControls.setButton(b, true);
    m->run(800000);
    show(tag);
    m->scannerControls.setButton(b, false);
    m->run(3200000);
    show("released+1s");
  };
  pulse(eii::ScannerButton::Disk, "DISK");
  m->run(4000000);
  show("Disk menu");
  const auto before = m->diskDma.transfers;
  const auto sampleHash = [&] {
    std::uint64_t h = 14695981039346656037ull;
    for (unsigned a = 0x10000; a < 0x80000; ++a) {
      h ^= m->memory[a];
      h *= 1099511628211ull;
    }
    return h;
  };
  const auto originalSamples = sampleHash();
  if (argc > 4)
    m->memory[0x12345] ^= 0x5a; // Test only: demand a real media reload.

  pulse(eii::ScannerButton::Digit1, "DIGIT1");
  m->run(4000000);
  show("after1");
  if (argc > 4) {
    m->run(200000000);
    show("RELOAD END");
    std::cout << "DMA delta=" << (m->diskDma.transfers - before)
              << " samples restored=" << (sampleHash() == originalSamples)
              << "\n";
    if (m->lcd.line(0).find("P01 GrandPiano") == std::string::npos ||
        m->diskDma.transfers == before || sampleHash() != originalSamples)
      throw std::runtime_error("bank reload did not complete");
  }

} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
