// Exercises the exact worker used by the native Windows front panel.
#include "front_panel_session.h"
#include <iostream>
using namespace std::chrono_literals;
int main(int argc, char **argv) try {
    if (argc != 3) {
        std::cerr << "usage: front_panel_probe prepared-ROM-folder disk.hfe|disk.emuiifd\n";
        return 2;
    }
    eii::FrontPanelSession session;
    eii::PanelBoot config;
    config.romFolder = argv[1];
    config.disk = argv[2];
    session.boot(config);
    session.resume();
    std::array<std::string, 2> previous{};
    unsigned stage = 0;
    std::uint64_t readyAt = 0;
    const auto deadline = std::chrono::steady_clock::now() + 10min;
    while (std::chrono::steady_clock::now() < deadline) {
        auto s = session.snapshot();
        if (!s.error.empty())
            throw std::runtime_error(s.error);
        if (s.lcd != previous) {
            std::cout << "LCD tick=" << s.ticks << " [" << s.lcd[0] << "] [" << s.lcd[1] << "]\n"
                      << std::flush;
            previous = s.lcd;
        }
        if (stage == 0 && s.lcd[0].starts_with("P01 ") && !s.lcd[0].starts_with("P01 NULL")) {
            readyAt = s.ticks;
            std::cout << "READY " << s.startup << " / " << s.media << "\n";
            session.pulse(eii::ScannerButton::Filter);
            stage = 1;
        }
        if (stage == 1 && s.ticks >= readyAt + 6000000) {
            session.voltage(0, 2.5);
            stage = 2;
        }
        if (stage == 2 && s.ticks >= readyAt + 12000000) {
            session.pause();
            stage = 3;
        }
        if (stage == 3 && !s.running) {
            std::cout << "PAUSED tick=" << s.ticks << " DMA=" << s.dma
                      << " ADC_A=" << s.controls[0].volts << "\n"
                      << s.report;
            return 0;
        }
        if (s.ticks > 220000000)
            throw std::runtime_error("no nonempty preset readiness within220M ticks");
        std::this_thread::sleep_for(20ms);
    }
    throw std::runtime_error("wall-time limit reached");
} catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
}
