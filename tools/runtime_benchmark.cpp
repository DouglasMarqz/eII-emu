#include "machine.h"
#include "floppy_hfe.h"
#include <chrono>
#include <iostream>
#include <iomanip>
static std::uint64_t hash(const auto &data) {
    std::uint64_t h = 14695981039346656037ull;
    for (auto b : data) {
        h ^= b;
        h *= 1099511628211ull;
    }
    return h;
}
int main(int argc, char **argv) try {
    if (argc < 4 || argc > 8) {
        std::cerr
            << "usage: runtime_benchmark ROM-folder image CPU-ticks [all|0..15] [unknown|seed32] [edge|event|devices|realized|concrete|voice] [--audio-monitor|--audio-basic]\n";
        return 2;
    }
    const std::string number = argv[3];
    if (number.empty() || number.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("decimal tick budget required");
    const auto ticks = std::stoull(number);
    if (!ticks || ticks > 1000000000)
        throw std::invalid_argument("ticks1..1billion");
    auto m = std::make_unique<eii::Machine>();
    const std::string phase = argc >= 5 ? argv[4] : "0";
    if (phase != "all") {
        const auto n = std::stoul(phase);
        if (n > 15 || phase.find_first_not_of("0123456789") != std::string::npos)
            throw std::invalid_argument("phase0..15/all required");
        m->channelTimers.controllerTiming = eii::ControllerTiming(std::uint8_t(n));
    }
    m->channelTimers.enableControllerTiming();
    m->channelTimers.controllerTiming.enableRegisterWrites();
    const std::filesystem::path folder = argv[1];
    m->load(folder / "main.bin", folder / "scanner.bin", folder / "ic135.bin",
            folder / "ic137.bin");
    const std::string power = argc >= 6 ? argv[5] : "1";
    if (power != "unknown") {
        const auto seed = std::stoull(power);
        if (!seed || seed > UINT32_MAX ||
            power.find_first_not_of("0123456789") != std::string::npos)
            throw std::invalid_argument("seed32/unknown required");
        m->initializeDramPowerOn(std::uint32_t(seed));
    }
    auto bytes = eii::readFile(argv[2]);
    if (bytes.size() >= 8 && std::equal(bytes.begin(), bytes.begin() + 8, "HXCPICFE"))
        bytes = eii::HfeImage(std::move(bytes)).extractEmuii();
    m->floppy.attach(0, 37, 400000);
    m->floppy.drives[0]->insertSensorDisk(true);
    m->diskRead = std::make_unique<eii::DiskReadPath>(87, 8);
    m->diskRead->mount(0, std::make_shared<const eii::EmuiiImage>(std::move(bytes)),
                       eii::EmuiiGap::Hxc2026);
    const std::string engine=argc>=7?argv[6]:"edge";
    if(engine!="edge" && engine!="event" && engine!="devices" && engine!="realized" && engine!="concrete" && engine!="voice")throw std::invalid_argument("unknown runtime engine");
    if(engine=="voice")m->enableNormalRuntime();
    else {
        if(engine!="edge")m->channelTimers.enableEventTiming();
        if(engine=="realized")m->initializeControllerPowerOn(0xe1122026u);
        if(engine=="devices" || engine=="realized" || engine=="concrete")m->enableRuntimeDevices();
        if(engine=="concrete")m->channelTimers.controllerTiming.enableConcreteRuntime();
    }
    std::uint64_t audioFrames=0;double audioPeak=0;
    if(argc==8) {
        const std::string audioMode=argv[7];
        if(audioMode!="--audio-monitor" && audioMode!="--audio-basic")throw std::invalid_argument("expected audio mode");
        m->enableRuntimeAudio([&](auto frames){
            audioFrames+=frames.size();for(auto f:frames)audioPeak=std::max(audioPeak,double(std::abs(f.left)));
        },audioMode=="--audio-monitor"?eii::analog::RuntimeAudioMode::DacMonitor:eii::analog::RuntimeAudioMode::BasicVoice);
    }
    const auto begin = std::chrono::steady_clock::now();
    m->run(ticks);
    const auto seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    std::cerr << std::setprecision(9) << "BENCH ticks=" << ticks << " wall=" << seconds
              << " realtime=" << double(ticks) / 4000000 / seconds << "\n";
    std::cerr << "TIMER_EVENTS " << m->channelTimers.runtimeEvents << "\n";
    std::cerr << "CONCRETE_STEPS " << m->channelTimers.controllerTiming.concreteSteps << "\n";
    std::cerr << "VOICE_TRANSACTIONS " << m->channelTimers.controllerTiming.voiceTransactions << " clocks=" << m->channelTimers.controllerTiming.voiceTransactionClocks << "\n";
    if(argc==8)std::cerr<<"HOST_AUDIO frames="<<audioFrames<<" peak="<<audioPeak<<"\n";
    const auto report = m->report();
    for (unsigned char c : report) {
        if (c == '\n' || c == '\r' || c == '\t' || (c >= 32 && c < 127))
            std::cout << char(c);
        else
            std::cout << "\\x" << std::hex << unsigned(c) << std::dec;
    }
    for (unsigned a = 0; a < 64; ++a) {
        const auto v = m->channelTimers.controllerTiming.registerValue(std::uint8_t(a));
        std::cout << "RF " << a << " " << v.value << " " << v.known << "\n";
    }
    std::cout << "STATE_FNV memory=" << hash(m->memory) << " known=" << hash(m->dramKnown)
              << " scanner=" << hash(m->scannerRam) << "\n";
} catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
}
