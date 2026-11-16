// Diagnostic: how much of a normal-runtime boot is covered by certified idle
// spans, and how much still runs edge by edge. Read-only instrumentation; it
// uses the same concrete startup and normal runtime as the front panel.
#include "machine.h"
#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>

int main(int argc, char** argv) try {
    if (argc != 4) throw std::invalid_argument("usage: runtime_cost_probe ROM-folder disk-image ticks");
    const std::filesystem::path folder = argv[1];
    const auto ticks = std::stoull(argv[3]);
    auto m = std::make_unique<eii::Machine>(0);
    m->load(folder / "main.bin", folder / "scanner.bin", folder / "ic135.bin", folder / "ic137.bin");
    m->initializeDramPowerOn(1);
    m->enableNormalRuntime();
    const auto bytes = eii::readFile(argv[2]);
    if (bytes.size() < 8 || bytes.size() != 573440) throw std::invalid_argument("573440-byte EMUIIFD expected");
    m->floppy.attach(0, 37, 400000);
    m->floppy.drives[0]->insertSensorDisk(true);
    m->diskRead = std::make_unique<eii::DiskReadPath>(87, 8);
    m->diskRead->mount(0, std::make_shared<const eii::EmuiiImage>(eii::readFile(argv[2])), eii::EmuiiGap::Hxc2026);
    std::uint64_t frames = 0;
    m->enableRuntimeAudio([&](auto block) { frames += block.size(); });
    const auto start = std::chrono::steady_clock::now();
    m->run(ticks);
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const auto& timing = m->channelTimers.controllerTiming;
    const auto halfClocks = m->channelTimers.halfClocks;
    const auto sourcePeriods = halfClocks / 2;
    const auto skipped = timing.quietClocks;
    std::cout << "ticks=" << ticks << " halfClocks=" << halfClocks << " sourcePeriods=" << sourcePeriods << "\n";
    std::cout << "idleSpanPeriods=" << skipped << " (" << (sourcePeriods ? 100.0 * double(skipped) / double(sourcePeriods) : 0.0)
              << "%) edgePeriods=" << (sourcePeriods - skipped) << " ("
              << (sourcePeriods ? 100.0 * double(sourcePeriods - skipped) / double(sourcePeriods) : 0.0) << "%)\n";
    std::cout << "spanAttempts=" << timing.quietAttempts << " rejectPre=" << timing.quietRejectPre
              << " rejectVoice=" << timing.quietRejectVoice << " rejectRequest=" << timing.quietRejectRequest
              << " rejectDram=" << timing.quietRejectDram << "\n";
    std::cout << "voiceReject: inactive=" << timing.quietRejectVoiceInactive
              << " notIdle=" << timing.quietRejectVoiceNotIdle
              << " phase=" << timing.quietRejectVoicePhase
              << " control=" << timing.quietRejectVoiceControl << "\n";
    if (!eii::diagnosticsEnabled)
        std::cout << "note: counters are zero because this is a Release build (EII_DIAGNOSTICS=0)\n";
    if (eii::diagnosticsEnabled) {
        std::cout << "rdtsc fastCycles=" << timing.diagFastCycles << " fastEdges=" << timing.diagFastEdges
                  << " slowCycles=" << timing.diagSlowCycles << " slowEdges=" << timing.diagSlowEdges << "\n";
        std::cout << "rdtsc upperCycles=" << timing.diagUpperCycles << " upperCalls=" << timing.diagUpperCalls << "\n";
        std::cout << "slot9 samples (" << timing.diagSlot9Edges << " edges), control per phase:";
        for (unsigned i = 0; i < 16; ++i) {
            if (!timing.diagSlot9Known[i]) { std::cout << " .."; continue; }
            std::cout << " " << std::hex << timing.diagSlot9Control[i]
                      << (timing.diagSlot9Known[i] == 1 ? "" : "?") << std::dec;
        }
        std::cout << "\n";
        std::cout << "entry attempts at phase8=" << timing.diagEntryAttempts << " top control bytes:";
        auto hist = timing.diagEntryControl;
        for (unsigned k = 0; k < 6; ++k) {
            unsigned best = 0;
            std::uint64_t bestCount = 0;
            for (unsigned i = 0; i < 256; ++i)
                if (hist[i] > bestCount) { bestCount = hist[i]; best = i; }
            if (!bestCount) break;
            std::cout << " " << std::hex << best << std::dec << "=" << bestCount;
            hist[best] = 0;
        }
        std::cout << "\n";
        std::cout << "entryFail: pre=" << timing.diagEntryFailPre << " upper=" << timing.diagEntryFailUpper
                  << " control=" << timing.diagEntryFailControl << " shift=" << timing.diagEntryFailShift
                  << " alu=" << timing.diagEntryFailAlu << " dramCap=" << timing.diagEntryFailDramCap
                  << " ucd4=" << timing.diagEntryFailUcd4 << " compile=" << timing.diagEntryFailCompile << "\n";
        std::cout << "entryEntry8: ctlKnown=" << timing.diagPreControlKnown << " shiftKnown=" << timing.diagPreShiftKnown
                  << " pending=" << timing.diagPrePending << " flag=" << timing.diagPreFlag
                  << " branches=" << timing.diagPreBranches << " other=" << timing.diagPreOther << "\n";
        std::cout << "risingEdges change=" << timing.diagRisingChanged
                  << " noChange=" << timing.diagRisingNoChange << "\n";
    }
    std::cout << "wall=" << wall << "s ticksPerSecond=" << (wall > 0 ? std::uint64_t(double(ticks) / wall) : 0)
              << " realtimeRatio=" << (wall > 0 ? double(ticks) / 4000000.0 / wall : 0.0) << "\n";
    std::cout << "LCD [" << m->lcd.line(0) << "] [" << m->lcd.line(1) << "] DMA=" << m->diskDma.transfers
              << " audioFrames=" << frames << "\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << "\n";
    return 1;
}
