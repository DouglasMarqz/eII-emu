// Native POSIX audit: one ROM boot; each child receives the same COW snapshot.
// Read-only caller-supplied media, actual scanner contacts, no firmware bypass.
#include "floppy_hfe.h"
#include "machine.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <sys/wait.h>
#include <unistd.h>
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
  // Same5ms quanta as the GUI; wall measurements are native-host evidence.
  for (const char* phase : {"boot", "idle"}) {
    std::vector<double> costs;
    const auto start = std::chrono::steady_clock::now();
    const unsigned n = phase[0] == 'b' ? 10000 : 400;
    for (unsigned i = 0; i < n; ++i) {
      const auto t = std::chrono::steady_clock::now();
      m->run(20000);
      costs.push_back(std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now()-t).count());
    }
    const auto seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now()-start).count();
    std::sort(costs.begin(), costs.end());
    std::cout << phase << " wall_s=" << seconds
              << " median_ms=" << costs[n/2] << " p95_ms=" << costs[n*95/100]
              << " max_ms=" << costs.back() << " quanta_over5ms="
              << std::count_if(costs.begin(), costs.end(), [](double t){return t>5;})
              << '/' << n << std::endl;
  }
  std::cout << "BOOT [" << m->lcd.line(0) << "][" << m->lcd.line(1) << "]\n"
            << std::flush;
  unsigned failed = 0;
  for (unsigned holdMs : {10u, 25u, 50u, 100u, 200u}) {
    const auto pid = fork();
    if (pid < 0)
      throw std::runtime_error("fork failed");
    if (pid == 0) {
      try {
        const auto b = eii::ScannerButton::Tune;
        m->scannerControls.setButton(b, true);
        int first = -1;
        for (unsigned ms=1;ms<=500;++ms) {
          if (ms == holdMs+1) m->scannerControls.setButton(b, false);
          m->run(4000);
          if (first<0 && m->lcd.line(0).find("TUNING")!=std::string::npos)
            first=int(ms);
        }
        std::cout << "Tune hold_ms=" << holdMs << " first_LCD_ms=" << first
                  << " LCD=[" << m->lcd.line(0) << "][" << m->lcd.line(1)
                  << "] invalid="
                  << m->channelTimers.controllerTiming.productionEngineInvalid()
                  << std::endl;
        if (first < 0 || m->channelTimers.controllerTiming.productionEngineInvalid())
          _exit(1);
        _exit(0);
      } catch (const std::exception &e) {
        std::cerr << holdMs << ": " << e.what() << std::endl;
        _exit(1);
      }
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) ||
        WEXITSTATUS(status))
      ++failed;
  }
  std::cout << "CONTACT TIMING children failed=" << failed << "\n";
  return failed ? 1 : 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << std::endl;
  return 1;
}
