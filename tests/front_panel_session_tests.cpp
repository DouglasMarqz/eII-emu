#include "front_panel_session.h"
#include <fstream>
#include <iostream>
using namespace std::chrono_literals;
static void require(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}
template <class F> static void rejects(F f) {
    bool bad = false;
    try {
        f();
    } catch (const std::exception &) {
        bad = true;
    }
    require(bad, "invalid command accepted");
}
template <class F> static eii::PanelSnapshot until(eii::FrontPanelSession &s, F predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 30s;
    for (;;) {
        auto snap = s.snapshot();
        if (predicate(snap))
            return snap;
        if (std::chrono::steady_clock::now() > deadline)
            throw std::runtime_error("worker timeout ticks="+std::to_string(snap.ticks)+" running="+std::to_string(snap.running)+" error="+snap.error);
        std::this_thread::sleep_for(1ms);
    }
}
struct Fixture {
    std::filesystem::path folder =
        std::filesystem::temp_directory_path() /
        ("eii-panel-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture() {
        std::filesystem::create_directory(folder);
        std::vector<std::uint8_t> main(8192, 0);
        const std::uint8_t serial[] = {0x3e, 0xff, 0xd3, 4, 0x3e, 0, 0xd3, 4, 0x76};
        std::copy(std::begin(serial), std::end(serial), main.begin());
        put("main.bin", main);
        std::vector<std::uint8_t> scanner(8192, 0);
        const std::uint8_t code[] = {
            0x3e, 0x0c, 0xd3, 0xbe,                      // LCD on
            0x3e, 0xcf, 0xd3, 0xdf, 0x3e, 0, 0xd3, 0xdf, // PIO B mode3/all output
            0x3e, 0x21, 0xd3, 0xde,                      // select physical row33
            // loop at16: LCD addr0, read real matrix, display1 when Filter pressed
            0x3e, 0x80, 0xd3, 0xbe, 0xdb, 0xb8, 0xe6, 0x20, 0x28, 4, 0x3e, '0', 0x18, 2, 0x3e, '1',
            0xd3, 0xbf, 0xc3, 16, 0};
        std::copy(std::begin(code), std::end(code), scanner.begin());
        put("scanner.bin", scanner);
        put("ic135.bin", std::vector<std::uint8_t>(512));
        std::vector<std::uint8_t> lower(512,0x40);
        // Permit the ninth-slot CPU staging write aperture; otherwise the
        // synthetic PROM creates register/staging contention at startup.
        for(unsigned a=0;a<512;++a)if((a&256)&&((a>>5)&7)==2)lower[a]|=4;
        put("ic137.bin", lower);
        put("bad.hfe", {1, 2, 3});
    }
    void put(const char *name, const std::vector<std::uint8_t> &b) {
        std::ofstream f(folder / name, std::ios::binary);
        f.write(reinterpret_cast<const char *>(b.data()), b.size());
    }
    ~Fixture() {
        std::error_code ec;
        std::filesystem::remove_all(folder, ec);
    }
};
int main() try {
    Fixture f;
    {
        auto queue=std::make_shared<eii::host::AudioQueue>();
        eii::FrontPanelSession models(queue);
        eii::PanelBoot boot;boot.romFolder=f.folder;
        models.boot(boot);until(models,[](const auto& v){return v.loaded;});
        models.reconstruction(true);
        auto applied=until(models,[](const auto& v){return v.reconstruction;});
        require(applied.audioMode==eii::analog::RuntimeAudioMode::BasicVoice&&applied.ticks==0,"Legacy S3528 GUI command applies without firmware reset");
        models.reconstruction(false);until(models,[](const auto& v){return !v.reconstruction;});
        models.audioModel(eii::analog::RuntimeAudioMode::NonlinearChip);
        auto v=until(models,[](const auto& v){return v.audioMode==eii::analog::RuntimeAudioMode::NonlinearChip;});
        require(v.ticks==0,"model selection does not advance firmware");
        models.reconstruction(true);until(models,[](const auto& v){return v.reconstruction;});
        models.step(20000);until(models,[](const auto& v){return v.ticks==20000;});
        eii::analog::FilterCalibration tuning;tuning[eii::analog::FilterParam::Qscale]=.6;
        models.calibrate(tuning);models.captureState();
        v=until(models,[&](const auto& v){return v.report.find(tuning.serialize())!=std::string::npos;});
        require(v.ticks==20000,"live calibration does not advance or restart firmware");
        models.audioModel(eii::analog::RuntimeAudioMode::BasicVoice);
        v=until(models,[](const auto& v){return v.audioMode==eii::analog::RuntimeAudioMode::BasicVoice;});
        require(v.ticks==20000,"DSP shutdown/switch preserves machine state");
        require(v.reconstruction,"S3528 remains applied when switching SSM to Legacy");
        models.reconstruction(false);until(models,[](const auto& v){return !v.reconstruction;});
        boot.audioMode=eii::analog::RuntimeAudioMode::NonlinearChip192;boot.calibration=tuning;models.boot(boot);
        until(models,[](const auto& v){return v.ticks==0&&v.audioMode==eii::analog::RuntimeAudioMode::NonlinearChip192;});
    }
    eii::FrontPanelSession s;
    eii::PanelBoot config;
    config.romFolder = f.folder;
    config.liveSerial = true;
    config.controls[0] = 2.5;
    s.boot(config);
    auto a = until(s, [](const auto &a) { return a.loaded; });
    require(!a.running && a.ticks == 0 && a.startup.find("seed1") != std::string::npos,
            "boot is paused with explicit startup");
    require(a.controls[0].known && a.controls[0].volts == 2.5, "boot slider pin applied");
    s.step(1000);
    a = until(s, [](const auto &a) { return a.ticks == 1000 && !a.running; });
    require(!a.referenceExecution && a.normalRuntimeEnabled && a.productionRuntimeEnabled,
            "normal worker actually executes runtime devices/controller, not the reference route");
    {
        eii::FrontPanelSession oracle;
        auto reference=config;reference.referenceExecution=true;
        oracle.boot(reference);
        until(oracle, [](const auto& v){return v.loaded;});
        oracle.step(1000);
        const auto v=until(oracle, [](const auto& v){return v.ticks==1000 && !v.running;});
        require(v.referenceExecution && !v.normalRuntimeEnabled && !v.timerEvents && !v.concreteClocks,
                "explicit reference override retains gate path with same startup");
        require(a.report==v.report && a.lcd==v.lcd && a.leds==v.leds,
                "normal/reference worker reports agree at identical physical startup and ticks");
    }
    require(a.report.find("\\xff\\x00") != std::string::npos,
            "arbitrary serial bytes are escaped for the UI report");
    require(a.lcdOn && a.lcd[0][0] == '0',
            "snapshot contains real Z80 LCD output and released matrix");
    s.captureState("HOST MIDI test-driver-errors=3");
    a = until(s, [](const auto& a){return a.report.find("MANUAL STATE CAPTURE")!=std::string::npos;});
    require(a.report.find("HOST MIDI test-driver-errors=3")!=std::string::npos,
            "manual capture includes host driver context without continuous instrumentation");
    require(a.report.find("engine=production")!=std::string::npos &&
            a.report.find("DMA WR0..6=")!=std::string::npos &&
            a.report.find("CTC interrupt states=")!=std::string::npos && a.ticks==1000,
            "manual capture reports actual engine without advancing the machine");
    s.pulse(eii::ScannerButton::Filter);
    s.step(400000);
    a = until(s, [](const auto &a) { return a.ticks == 401000 && !a.running; });
    require(a.lcd[0][0] == '1', "panel pulse reaches physical scanner row");
    s.step(420000);
    a = until(s, [](const auto &a) { return a.ticks == 821000 && !a.running; });
    require(a.lcd[0][0] == '0', "pulse releases after200ms simulated; scanner observes release");
    // Real contacts are not fixed-duration pulses: hold, release and retrigger
    // must each be observed by the executing Z80 scanner.
    auto ticks = a.ticks;
    for (unsigned press = 0; press < 2; ++press) {
        s.button(eii::ScannerButton::Filter, true);
        s.step(press ? 1000 : 840000);
        ticks += press ? 1000 : 840000;
        a = until(s, [&](const auto& v){return v.ticks == ticks && !v.running;});
        require(a.lcd[0][0] == '1', "physical press remains held past old200ms timeout and retriggers");
        s.button(eii::ScannerButton::Filter, false);
        s.step(1000); ticks += 1000;
        a = until(s, [&](const auto& v){return v.ticks == ticks && !v.running;});
        require(a.lcd[0][0] == '0', "physical release reaches scanner without200ms wait");
    }
    // Both edges may be queued before the worker can execute anything.
    // They must not cancel each other out in the same command batch.
    s.button(eii::ScannerButton::Filter, true);
    s.button(eii::ScannerButton::Filter, false);
    s.step(1000); ticks += 1000;
    a = until(s, [&](const auto& v){return v.ticks == ticks && !v.running;});
    require(a.lcd[0][0] == '1', "batched down/up gives scanner an executing interval");
    s.step(1000); ticks += 1000;
    a = until(s, [&](const auto& v){return v.ticks == ticks && !v.running;});
    require(a.lcd[0][0] == '0', "batched release follows without losing the contact edge");
    for (unsigned i = 0; i < 4000; ++i) {
        s.voltage(4, double(i % 1000) / 200.0);
        s.voltage(5, double(999 - i % 1000) / 200.0);
    }
    s.voltage(4, 1.75); s.voltage(5, 3.25);
    s.step(1000); ticks += 1000;
    a = until(s, [&](const auto& v){return v.ticks == ticks && !v.running;});
    require(a.controls[4].volts == 1.75 && a.controls[5].volts == 3.25,
            "wheel flood retains latest independent values before ordered step");
    s.voltage(0, 1.25);
    a = until(s, [](const auto &a) { return a.controls[0].volts == 1.25; });
    s.midiOptions(true,true);s.recordMidi(true);
    const std::array<std::uint8_t,3> modWheel{0xb0,1,127},pitchCenter{0xe0,0,64},noteOff{0x80,60,64};
    require(s.midi(modWheel)&&s.midi(pitchCenter)&&s.midi(noteOff),"host MIDI adapters accept valid input");
    const auto midiTrace=s.exportMidi().get();
    require(midiTrace.find("803c40\t903c00")!=std::string::npos,
            "optional capture retains both original note-off and actual adapted wire bytes");
    a=until(s,[](const auto& v){return v.controls[4].volts==2.5&&v.controls[5].volts==5;});
    require(a.midiWheelRevision==2,"MIDI bridge drives actual scanner wheel pins and UI revision");
    s.recordMidi(false);s.midiOptions(false,false);
    const std::array<std::uint8_t,3> sustainDown{0xb2,64,127};
    require(s.midi(sustainDown),"host pedal packet accepted");s.captureState();
    a=until(s,[](const auto& v){return v.report.find("CC64 enqueued seen_mask=4 down_mask=4")!=std::string::npos;});
    for(const auto& message:eii::host::MidiHostTools::releaseAllMessages())require(s.midi(message),"release burst accepted by worker queue");
    s.captureState();
    a=until(s,[](const auto& v){return v.report.find("CC64 enqueued seen_mask=ffff down_mask=0")!=std::string::npos;});

    const auto before = a.ticks;
    s.resume();
    a = until(s, [&](const auto &a) { return a.ticks > before && a.running; });
    const auto revision = a.revision;
    s.pause();
    a = until(s, [&](const auto &a) { return a.revision > revision && !a.running; });
    const auto stopped = a.ticks;
    std::this_thread::sleep_for(30ms);
    require(s.snapshot().ticks == stopped, "pause freezes simulation");
    {
        auto queue=std::make_shared<eii::host::AudioQueue>();
        eii::FrontPanelSession paced(queue);
        paced.boot(config);
        until(paced, [](const auto& v){return v.loaded;});
        const auto start=std::chrono::steady_clock::now();
        paced.resume();
        const std::array<std::uint8_t,3> cc{0xb0,1,64};
        unsigned messages=0;
        while(std::chrono::steady_clock::now()-start<150ms){
            require(paced.midi(cc),"paced MIDI burst accepted");++messages;
            paced.voltage(4, double(messages % 100) / 20.0);
            paced.voltage(5, double(99 - messages % 100) / 20.0);
            std::this_thread::sleep_for(200us);
        }
        paced.captureState();
        until(paced, [](const auto& v){return v.report.find("HOST PACING wall-minus-simulated ms=")!=std::string::npos;});
        paced.pause();
        const auto v=until(paced, [](const auto& v){return !v.running&&v.ticks>0;});
        const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        require(messages>0&&v.error.empty(),"worker remains responsive during host MIDI burst");
        require(v.controls[4].volts==double(messages%100)/20.0 &&
                v.controls[5].volts==double(99-messages%100)/20.0,
                "paced wheel flood finishes with latest independent positions");
        require(double(v.ticks)<=elapsed*4000000+40000,
                "MIDI wakeups must not run more than two quanta ahead of wall time");
        require(v.revision<15,"MIDI/wheel flood does not publish a full snapshot per packet");
        paced.captureState();
        const auto captured=until(paced, [](const auto& v){return v.report.find("HOST AUDIO queue frames=")!=std::string::npos;});
        require(captured.report.find("queued bytes=")!=std::string::npos,
                "manual capture exposes host audio and MIDI wire backlogs");
    }
    config.disk = f.folder / "bad.hfe";
    s.boot(config);
    a = until(s, [](const auto &a) { return !a.error.empty(); });
    require(a.ticks == stopped && a.loaded && !a.running,
            "failed media load preserves paused machine");
    config.disk.clear();
    config.concreteStartup = false;
    s.boot(config);
    a = until(s, [](const auto &a) { return a.error.empty() && a.ticks == 0; });
    require(a.startup.find("unknown RAM") != std::string::npos && a.referenceExecution,
            "unknown/all16 verification retains reference execution");
    const std::array<std::uint8_t,3> note{0x90,60,100};
    require(s.midi(note),"host note packet accepted by one-owner worker");
    require(!s.midi(std::span(note.data(),2)),"truncated MIDI packet rejected");
    const std::array<std::uint8_t,1> badMidi{0x40};
    require(!s.midi(badMidi),"raw data without status rejected at host packet boundary");
    // A malformed controller must stop the worker visibly, rather than keep
    // running an invalid concrete state behind a responsive host window.
    f.put("ic137.bin", std::vector<std::uint8_t>(512));
    config.concreteStartup = true;
    s.boot(config);
    until(s, [](const auto& v){return v.error.empty() && v.ticks==0;});
    s.step(1000);
    a=until(s, [](const auto& v){return !v.error.empty();});
    require(!a.running && a.ticks<1000 && a.productionRuntimeEnabled &&
            a.error.find("conflicting RFD bus drivers")!=std::string::npos,
            "worker publishes production fault without silently substituting reference");
    s.captureState();
    a=until(s, [](const auto& v){return v.report.find("invalid=1")!=std::string::npos;});
    require(a.report.find("engine=production")!=std::string::npos &&
            a.report.find("STOP REASON: Production controller stopped:")!=std::string::npos,
            "manual capture preserves exact stop reason after dismissing error dialog");
    rejects([&] { s.step(0); });
    rejects([&] { s.step(20000001); });
    rejects([&] { s.voltage(6, 0); });
    rejects([&] { s.voltage(0, 6); });
    rejects([&] { s.voltage(0, NAN); });
    rejects([&] { s.pulse(eii::ScannerButton(30)); });
    rejects([&] { s.button(eii::ScannerButton(30), true); });
    std::cout << "PASS: one-owner worker;actual Z80 LCD/matrix path;timed panel release;slider ADC "
                 "pin;pause/resume/exact stepping;failed media rollback;explicit vs unknown "
                 "startup;command guards;clean shutdown.\n";
} catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
}
