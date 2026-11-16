// Native POSIX audit: one ROM boot; each child receives the same COW snapshot.
// Read-only caller-supplied media, no firmware bypass or host input injection.
#include "floppy_hfe.h"
#include "machine.h"
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
static const char *names[] = {
    "Select",    "Stop",    "Record",  "Play",      "Define",   "Edit",
    "ExtClock",  "Setup",   "0",       "9YES",      "8",        "7NO",
    "6",         "5",       "4",       "3",         "2",        "1",
    "Transpose", "Tune",    "Dynamic", "PresetDef", "Sample",   "Disk",
    "VoiceDef",  "Special", "VCALFO",  "Filter",    "Realtime", "Enter"};
int main(int argc, char **argv) try {
  if (argc != 3)
    return 2;
  const std::filesystem::path p = argv[1];
  auto m = std::make_unique<eii::Machine>(0);
  m->load(p / "main.bin", p / "scanner.bin", p / "ic135.bin", p / "ic137.bin");
  m->initializeDramPowerOn(1);
  m->enableProductionController();
  m->initializeControllerPowerOn(0xe1122026u);
  m->enableNormalRuntime();
  m->floppy.attach(0, 37, 400000);
  m->floppy.drives[0]->insertSensorDisk(true);
  m->diskRead = std::make_unique<eii::DiskReadPath>(87, 8);
  m->diskRead->mount(0,
                     std::make_shared<const eii::EmuiiImage>(
                         eii::HfeImage(eii::readFile(argv[2])).extractEmuii()),
                     eii::EmuiiGap::Hxc2026);
  m->run(200000000);
  std::cout << "BOOT [" << m->lcd.line(0) << "][" << m->lcd.line(1) << "]\n"
            << std::flush;
  unsigned failed = 0;
  for (unsigned i = 0; i < 30; ++i) {
    const auto pid = fork();
    if (pid < 0)
      throw std::runtime_error("fork failed");
    if (pid == 0) {
      try {
        auto b = eii::ScannerButton(i);
        m->scannerControls.setButton(b, true);
        m->run(800000);
        m->scannerControls.setButton(b, false);
        m->run(11200000);
        std::cout << names[i] << " LCD=[" << m->lcd.line(0) << "]["
                  << m->lcd.line(1) << "] LEDs=";
        for (unsigned j = 0; j < 30; ++j) {
          auto s = eii::ScannerControls::ledState(m->scannerControls.leds,
                                                  eii::ScannerButton(j));
          if (s.known && s.high)
            std::cout << names[j] << ",";
        }
        std::cout << " raw=";
        for (auto l : m->scannerControls.leds)
          std::cout << std::hex << unsigned(l.value) << "/" << unsigned(l.known)
                    << ",";
        std::cout << std::dec << " invalid="
                  << m->channelTimers.controllerTiming.productionEngineInvalid()
                  << "\n"
                  << std::flush;
        _exit(0);
      } catch (const std::exception &e) {
        std::cerr << names[i] << ": " << e.what() << std::endl;
        _exit(1);
      }
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) ||
        WEXITSTATUS(status))
      ++failed;
  }
  std::cout << "PANEL AUDIT children failed=" << failed << "\n";
  return failed ? 1 : 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << std::endl;
  return 1;
}
