#pragma once
#include "floppy_hfe.h"
#include "machine.h"
#include "hdd_image_file.h"
#include "floppy_image_backing.h"
#include "midi_host_tools.h"
#include "host_pacing.h"
#include "host_thread_cpu.h"
#include "host_cpu_placement.h"
#include "panel_report_text.h"
#include "host_command_wake.h"
#include <atomic>
#include <cctype>
#include <future>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace eii {
// One worker owns Machine. The UI exchanges commands and immutable snapshots;
// it never reads a running CPU, LCD, drive or analog-control model directly.
struct PanelBoot {
    std::filesystem::path romFolder, disk, hddImage;
    bool discardHddChanges=false,directImageWrites=false;
    bool diagnostic = false, concreteStartup = true, realTime = true;
    bool liveSerial = false,reconstruction=false,nativeAudio=false;
    analog::RuntimeAudioMode audioMode=analog::RuntimeAudioMode::BasicVoice;
    analog::FilterCalibration calibration;
    // Independent oracle override for comparing identical physical startup.
    // The UI's unknown/all16 verification choice also forces this route.
    bool referenceExecution = false;
    std::array<double, 6> controls{0, 0, 0, 0, 2.5, 0}; // A..D, left/right wheel
};
struct PanelTraceMetrics {
    bool enabled=false,mainHalt=false,mainWait=false,invalid=false;
    std::uint16_t mainPc=0,scannerPc=0;
    std::uint64_t machineGeneration=0,audioGeneration=0;
    double ownerBusy=-1,quantumMs=0,meanQuantumMs=0,maxQuantumMs=0,latenessMs=0;
    host::AudioQueue::Stats audio{};
    std::array<KnownByte,8> reconstructionCode{};
    std::array<double,24> cv{};std::uint32_t cvKnown=0;
};
struct PanelHddState {
    bool mounted=false,dirty=false,idle=true,direct=false,writable=true;
    std::filesystem::path source,lastSaved;std::string format="img";
};
struct PanelMediaState {PanelHddState hdd,floppy;};
struct PanelSnapshot {
    PanelHddState floppy;
    PanelHddState hdd;
    PanelTraceMetrics trace;
    bool reconstruction=false,nativeAudio=false;
    std::uint64_t nativeOverflow=0,nativeOrderErrors=0;
    analog::ChipAudioWorker::Stats dsp;
    analog::RuntimeAudioMode audioMode=analog::RuntimeAudioMode::BasicVoice;
    std::array<std::string, 2> lcd{"                ", "                "};
    std::array<KnownByte, 3> leds{};
    std::array<AdcVoltage, 6> controls{};
    std::uint64_t revision = 0, reportRevision = 0, ticks = 0, dma = 0, dramReads = 0;
    double speedRatio = 0,hostDroppedPerSecond=0,hostMissingPerSecond=0,pacingDebtMs=0;
    std::uint64_t pacingRebases=0;
    std::uint64_t midiReceived=0,midiRead=0,midiOverruns=0,midiDropped=0;
    std::uint64_t midiWheelRevision=0,midiTraceEvents=0;
    std::uint64_t timerEvents = 0, concreteClocks = 0;
    bool referenceExecution = false;
    bool normalRuntimeEnabled = false, productionRuntimeEnabled = false;
    unsigned cylinder = 0;
    bool loaded = false, running = false, lcdOn = false, selected = false, spinning = false;
    std::string report, error, startup, media;
};
class FrontPanelSession {
  public:
    explicit FrontPanelSession(std::shared_ptr<host::AudioQueue> audio={})
        : audio_(std::move(audio)), thread_([this] { loop(); }) {}
    ~FrontPanelSession() {
        {
            std::lock_guard lock(mutex_);
            quit_ = true;
        }
        wake_.notify_one();
        thread_.join();
    }
    FrontPanelSession(const FrontPanelSession &) = delete;
    FrontPanelSession &operator=(const FrontPanelSession &) = delete;
    void boot(PanelBoot config) {
        Command c{Kind::Boot};
        c.boot = std::move(config);
        enqueue(std::move(c));
    }
    std::future<PanelHddState> pauseForHdd() {
        Command c{Kind::HddPause};c.hddReply=std::make_shared<std::promise<PanelHddState>>();
        auto result=c.hddReply->get_future();enqueue(std::move(c));return result;
    }
    std::future<std::string> saveHddCopy(std::filesystem::path path) {
        Command c{Kind::HddSave};c.path=std::move(path);c.reply=std::make_shared<std::promise<std::string>>();
        auto result=c.reply->get_future();enqueue(std::move(c));return result;
    }
    std::future<PanelMediaState> pauseForMedia(){
        Command c{Kind::HddPause};c.mediaReply=std::make_shared<std::promise<PanelMediaState>>();
        auto result=c.mediaReply->get_future();enqueue(std::move(c));return result;
    }
    std::future<std::string> floppyImage(std::filesystem::path path,bool direct,bool discard=false){
        Command c{Kind::FloppyMount};c.path=std::move(path);c.index=direct;c.ticks=discard;c.reply=std::make_shared<std::promise<std::string>>();
        auto result=c.reply->get_future();enqueue(std::move(c));return result;
    }
    std::future<std::string> saveFloppyCopy(std::filesystem::path path){
        Command c{Kind::FloppySave};c.path=std::move(path);c.reply=std::make_shared<std::promise<std::string>>();
        auto result=c.reply->get_future();enqueue(std::move(c));return result;
    }
    void audioModel(analog::RuntimeAudioMode mode) {
        if(mode!=analog::RuntimeAudioMode::BasicVoice&&!analog::nonlinearAudioMode(mode))throw std::invalid_argument("Invalid chip model");
        Command c{Kind::AudioModel};c.index=unsigned(mode);enqueue(std::move(c));
    }
    // Live +INPUT.V source (null = no capture); applied to this and later machines.
    void sampleSource(std::shared_ptr<analog::SampleInputSource> source){Command c{Kind::SampleSource};c.source=std::move(source);enqueue(std::move(c));}
    void nativeAudio(bool enabled){Command c{Kind::NativeAudio};c.index=enabled;enqueue(std::move(c));}
    void reconstruction(bool enabled){Command c{Kind::Reconstruction};c.index=enabled;enqueue(std::move(c));}
    void calibrate(const analog::FilterCalibration& t){t.validate();Command c{Kind::Calibration};c.calibration=t;enqueue(std::move(c));}
    void resume() { enqueue(Command{Kind::Resume}); }
    void pause() { enqueue(Command{Kind::Pause}); }
    void liveSerial(bool enabled) {Command c{Kind::LiveSerial};c.index=enabled;enqueue(std::move(c));}
    void traceMetrics(bool enabled){Command c{Kind::TraceMetrics};c.index=enabled;enqueue(std::move(c));}
    void captureState(std::string hostContext={}) {
        Command c{Kind::Capture};c.text=std::move(hostContext);enqueue(std::move(c));
    }
    void step(std::uint64_t ticks) {
        if (!ticks || ticks > 20000000)
            throw std::invalid_argument("step requires1..20000000ticks");
        Command c{Kind::Step};
        c.ticks = ticks;
        enqueue(std::move(c));
    }
    void pulse(ScannerButton button) {
        if (unsigned(button) >= 30)
            throw std::invalid_argument("panel button outside0..29");
        Command c{Kind::Pulse};
        c.index = unsigned(button);
        enqueue(std::move(c));
    }
    // Physical host contacts: firmware still performs scanning/debounce.
    void button(ScannerButton button, bool pressed) {
        if (unsigned(button) >= 30)
            throw std::invalid_argument("panel button outside0..29");
        Command c{Kind::Button};
        c.index = unsigned(button);
        c.ticks = pressed;
        enqueue(std::move(c));
    }
    void voltage(unsigned control, double volts) {
        if (control >= 6 || !std::isfinite(volts) || volts < 0 || volts > 5)
            throw std::invalid_argument("panel control requires0..5V");
        Command c{Kind::Voltage};
        c.index = control;
        c.volts = volts;
        enqueue(std::move(c));
    }
    void serial(std::string text) {
        if (text.size() > 256)
            throw std::invalid_argument("serial command too long");
        Command c{Kind::Serial};
        c.text = std::move(text);
        enqueue(std::move(c));
    }
    void midiOptions(bool wheelBridge,bool noteOffZero) {
        Command c{Kind::MidiOptions};c.index=wheelBridge;c.ticks=noteOffZero;enqueue(std::move(c));
    }
    void recordMidi(bool enabled) {
        traceRequested_=enabled;
        Command c{Kind::MidiRecord};c.ticks=enabled;enqueue(std::move(c));
    }
    std::future<std::string> exportMidi() {
        Command c{Kind::MidiExport};c.reply=std::make_shared<std::promise<std::string>>();
        auto result=c.reply->get_future();enqueue(std::move(c));return result;
    }
    bool midi(std::span<const std::uint8_t> message) {
        if(message.empty() || message.size()!=midiMessageLength(message[0]) ||
           std::any_of(message.begin()+1,message.end(),[](auto b){return b>=128;}))return false;
        Command c{Kind::Midi};if(traceRequested_)c.hostNs=hostNanos();c.text.assign(reinterpret_cast<const char*>(message.data()),message.size());
        {
            std::lock_guard lock(mutex_);
            if(quit_ || commands_.size()>=512)return false;
            commands_.push_back(std::move(c));
        }
        wake_.notify_one();return true;
    }
    PanelSnapshot snapshot() const {
        std::lock_guard lock(mutex_);
        return snapshot_;
    }

  private:
    enum class Kind { FloppyMount, FloppySave, HddPause, HddSave, Boot, Resume, Pause, Step, Pulse, Button, Voltage, Serial, Midi, Capture, LiveSerial, TraceMetrics, MidiOptions, MidiRecord, MidiExport, AudioModel, Calibration, Reconstruction, NativeAudio, SampleSource };
    struct Command {
        analog::FilterCalibration calibration;
        explicit Command(Kind k) : kind(k) {}
        Kind kind;
        PanelBoot boot;
        std::filesystem::path path;
        std::shared_ptr<std::promise<PanelHddState>> hddReply;
        std::shared_ptr<std::promise<PanelMediaState>> mediaReply;
        std::uint64_t ticks = 0;
        unsigned index = 0;
        double volts = 0;
        std::string text;
        std::uint64_t hostNs=0;
        std::shared_ptr<std::promise<std::string>> reply;
        std::shared_ptr<analog::SampleInputSource> source;
    };
    static ScannerAnalogInput input(unsigned n) {
        return n < 4    ? ScannerAnalogInput(unsigned(ScannerAnalogInput::SliderA) + n)
               : n == 4 ? ScannerAnalogInput::LeftWheel
                        : ScannerAnalogInput::RightWheel;
    }
    static std::uint64_t hostNanos() noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    std::atomic<bool> traceRequested_{false};
    host::MidiHostTools midiTools_;
    mutable std::mutex mutex_;
    host::CommandWake wake_;
    std::deque<Command> commands_;
    PanelSnapshot snapshot_;
    bool quit_ = false;
    // All remaining state is confined to the worker.
    std::unique_ptr<Machine> machine_;
    std::filesystem::path hddSource_,hddLastSaved_,floppyLastSaved_;
    std::shared_ptr<host::DirectImageFile> hddLease_;
    std::shared_ptr<host::FloppyImageBacking> floppyBacking_;
    static bool sameImage(const std::filesystem::path& a,const std::filesystem::path& b){
        if(a.empty()||b.empty())return false;std::error_code e;return std::filesystem::equivalent(a,b,e)&&!e;
    }
    PanelHddState floppyState()const {
        PanelHddState s;if(!floppyBacking_)return s;s.mounted=true;s.source=floppyBacking_->path;s.lastSaved=floppyLastSaved_;
        s.format=floppyBacking_->hfe?"hfe":"emuiifd";s.direct=floppyBacking_->direct;s.writable=floppyBacking_->writable;s.dirty=floppyBacking_->dirty;
        s.idle=!machine_||(machine_->diskRead&&!machine_->diskRead->writing()&&!machine_->diskDma.granted&&(!machine_->floppy.drives[0]||machine_->floppy.drives[0]->writeGateN()));return s;
    }
    static void installFloppy(Machine& m,const std::shared_ptr<host::FloppyImageBacking>& backing){
        if(!m.floppy.drives[0])m.floppy.attach(0,37,400000);
        m.floppy.drives[0]->insertSensorDisk(!backing->writable);
        if(!m.diskRead)m.diskRead=std::make_unique<DiskReadPath>(87,8,m.ticks);
        if(backing->writable)m.diskRead->mountWritable(0,backing->image,EmuiiGap::Hxc2026);
        else m.diskRead->mount(0,backing->image,EmuiiGap::Hxc2026);
        m.diskDma.floppyIo=backing->writable;
        m.diskRead->persistTrack=[backing](unsigned,unsigned c,unsigned side,std::span<const std::uint8_t> bytes){return backing->trackWritten(c,side,bytes);};
    }
    PanelHddState hddState() const {
        PanelHddState s;s.source=hddSource_;s.lastSaved=hddLastSaved_;
        if(machine_&&machine_->hardDisk){s.mounted=true;s.dirty=machine_->hardDisk->dirty;s.direct=bool(hddLease_);
            s.idle=machine_->hardDisk->phase==HardDisk::Free&&!machine_->diskDma.granted;}
        return s;
    }
    analog::FilterCalibration calibration_;
    std::shared_ptr<analog::SampleInputSource> sampleSource_;
    bool reconstruction_=false,nativeAudio_=false;
    bool running_ = false, realTime_ = true;
    bool referenceExecution_ = false,traceMetrics_=false;
    std::uint64_t machineGeneration_=0,audioGeneration_=0;
    host::HostPacer pacer_;
    std::chrono::steady_clock::time_point sampleTime_{},audioRateTime_{};
    host::AudioQueue::Stats lastAudioStats_{};double droppedRate_=0,missingRate_=0;
    std::uint64_t sampleTick_ = 0;
    double speedRatio_ = 0,sampleCpu_=-1,threadBusy_=-1,quantumMs_=0,meanQuantumMs_=0,maxQuantumMs_=0;
    std::uint64_t remaining_ = 0, revision_ = 0;
    std::array<std::uint64_t, 30> release_{};
    struct Contact {
        bool held = false;
        std::uint64_t tick = ~std::uint64_t(0);
        std::deque<bool> pending;
    };
    std::array<Contact, 30> contacts_{};
    std::string startup_, media_, error_, capturedReport_;
    host::PanelReportText reportText_;bool liveSerial_=false;
    std::shared_ptr<host::AudioQueue> audio_;
    std::thread thread_; // Starts last, after every field it can access exists.
    void enqueue(Command c) {
        {
            std::lock_guard lock(mutex_);
            // Analog positions are state, not events. Keep only the latest
            // pending value per control, without crossing an ordered command.
            if(c.kind==Kind::Calibration && !commands_.empty() && commands_.back().kind==Kind::Calibration){commands_.back()=std::move(c);return;}
            if (c.kind == Kind::Voltage) {
                for (auto it = commands_.rbegin(); it != commands_.rend() &&
                     it->kind == Kind::Voltage; ++it) {
                    if (it->index == c.index) {
                        it->volts = c.volts;
                        return;
                    }
                }
            }
            commands_.push_back(std::move(c));
        }
        wake_.notify_one();
    }
    void publish() {
        const auto now = std::chrono::steady_clock::now();
        if (machine_ && sampleTime_.time_since_epoch().count() && machine_->ticks >= sampleTick_) {
            const double elapsed = std::chrono::duration<double>(now - sampleTime_).count();
            if (elapsed >= 1.0) {
                speedRatio_ = double(machine_->ticks - sampleTick_) / 4000000.0 / elapsed;
                const double cpu=host::threadCpuSeconds();
                threadBusy_=sampleCpu_>=0&&cpu>=sampleCpu_?(cpu-sampleCpu_)/elapsed:-1;sampleCpu_=cpu;
                sampleTime_ = now;
                sampleTick_ = machine_->ticks;
            }
        }
        host::AudioQueue::Stats audioStats{};
        if(audio_) {
            const auto current=audioStats=audio_->stats();
            if(!audioRateTime_.time_since_epoch().count()){audioRateTime_=now;lastAudioStats_=current;}
            const double elapsed=std::chrono::duration<double>(now-audioRateTime_).count();
            if(elapsed>=1){droppedRate_=double(current.dropped-lastAudioStats_.dropped)/elapsed;missingRate_=double(current.missing-lastAudioStats_.missing)/elapsed;audioRateTime_=now;lastAudioStats_=current;}
        }
        PanelSnapshot next;next.hdd=hddState();next.floppy=floppyState();
        if(traceMetrics_){
            next.trace.enabled=true;next.trace.machineGeneration=machineGeneration_;next.trace.audioGeneration=audioGeneration_;
            next.trace.ownerBusy=threadBusy_;next.trace.quantumMs=quantumMs_;next.trace.meanQuantumMs=meanQuantumMs_;next.trace.maxQuantumMs=maxQuantumMs_;
            next.trace.audio=audioStats;
        }
        next.hostDroppedPerSecond=droppedRate_;next.hostMissingPerSecond=missingRate_;next.pacingRebases=pacer_.rebases();next.pacingDebtMs=pacer_.debtMs();
        next.speedRatio = speedRatio_;
        next.midiWheelRevision=midiTools_.wheelRevision;next.midiTraceEvents=midiTools_.total;
        next.revision = ++revision_;
        next.error = error_;
        next.running = running_ || remaining_ != 0;
        next.startup = startup_;
        next.media = media_;
        next.referenceExecution = referenceExecution_;
        if (machine_) {
            const auto &m = *machine_;
            next.loaded = true;next.reconstruction=m.reconstructionEnabled();next.nativeAudio=m.nativeAudioEnabled();next.nativeOverflow=m.nativeAudioOverflows();next.nativeOrderErrors=m.nativeAudioOrderingErrors();
            if(traceMetrics_){
                auto& t=next.trace;t.mainPc=m.main.cpu.pc;t.scannerPc=m.scanner.cpu.pc;
                t.mainHalt=m.main.pins&Z80_HALT;t.mainWait=m.main.pins&Z80_WAIT;
                t.invalid=m.channelTimers.controllerTiming.productionEngineInvalid();
                for(unsigned ch=0;ch<8;++ch)t.reconstructionCode[ch]=m.reconstructionControl[ch].activeCode();
                t.latenessMs=realTime_&&running_?pacer_.latenessMs(now,m.ticks):0;
                for(unsigned bank=0;bank<3;++bank)for(unsigned ch=0;ch<8;++ch){const auto& cv=m.controlVoltage.held[bank][ch];const auto index=bank*8+ch;t.cv[index]=cv.volts;if(cv.known)t.cvKnown|=1u<<index;}
            }
            next.audioMode=m.runtimeAudioMode();next.dsp=m.chipAudioStats();
            next.ticks = m.ticks;
            next.timerEvents = m.channelTimers.runtimeEvents;
            next.concreteClocks = m.channelTimers.controllerTiming.concreteSteps;
            next.normalRuntimeEnabled = m.normalRuntimeEnabled();
            next.productionRuntimeEnabled = m.productionControllerEnabled();
            next.lcd = {m.lcd.line(0), m.lcd.line(1)};
            next.lcdOn = m.lcd.display;
            next.dma = m.diskDma.transfers;
            next.dramReads = m.dramReadCycles;
            next.leds = m.scannerControls.leds;
            next.midiReceived=m.midiSerial.received;next.midiRead=m.midiSerial.reads;
            next.midiOverruns=m.midiSerial.overruns;next.midiDropped=m.midiWire.dropped;
            for (unsigned i = 0; i < 6; ++i)
                next.controls[i] = m.scannerControls.adc.inputs[unsigned(input(i))];
            if(capturedReport_.empty()) {
#if EII_DIAGNOSTICS
                auto diagnostic=m.report();
                for(unsigned i=0;i<3;++i)
                    diagnostic+="Panel LED"+std::to_string(i)+" raw value/mask="+
                        std::to_string(next.leds[i].value)+"/"+std::to_string(next.leds[i].known)+"\n";
                reportText_.diagnostic(diagnostic);
#else
                reportText_.serial(m.serialTx,liveSerial_);
#endif
            }
            next.report=reportText_.text();next.reportRevision=reportText_.revision();
            if (m.floppy.drives[0]) {
                const auto &d = *m.floppy.drives[0];
                next.cylinder = d.cylinder;
                next.selected = d.selected();
                next.spinning = d.spinning();
            }
        }
        std::lock_guard lock(mutex_);
        snapshot_ = std::move(next);
    }
    void captureReport(const std::string& hostContext={}) {
        const auto& m=*machine_;
        std::ostringstream out;
        out << "MANUAL STATE CAPTURE (not a live trace) CPU tick=" << m.ticks
            << " engine=" << (m.productionControllerEnabled()?"production":"reference")
            << " invalid=" << m.channelTimers.controllerTiming.productionEngineInvalid() << "\n";
        if(!hostContext.empty())out << hostContext << "\n";
        if (!error_.empty()) out << "STOP REASON: " << error_ << "\n";
        if (running_ && realTime_) {
            out << "HOST PACING wall-minus-simulated ms="<<pacer_.latenessMs(std::chrono::steady_clock::now(),m.ticks)
                <<" (positive=behind; negative=ahead) speed-window=1s rebases="<<pacer_.rebases()<<" discarded-wall-debt-ms="<<pacer_.debtMs()<<"\n";
        }
        out<<"HOST ENGINE owner-thread-busy="<<threadBusy_<<" quantum-ms="<<quantumMs_<<" mean-quantum-ms="<<meanQuantumMs_<<" max-quantum-ms="<<maxQuantumMs_<<" (normal quantum=5ms simulated; wall work includes preemption)\n";
        const auto wakeStats=wake_.stats();
        out<<"HOST WAIT native-event="<<wakeStats.nativeEvent<<" calls="<<wakeStats.waits<<" timeouts="<<wakeStats.timeouts<<" last-ms="<<wakeStats.lastMilliseconds<<" (positive waits round upward; no zero-time pacing poll)\n";
        for(const auto* n:{&m.main,&m.scanner}) {
            out << (n->scanner?"scanner":"main") << " PC=" << std::hex << n->cpu.pc
                << " IX=" << n->cpu.ix << " IY=" << n->cpu.iy << std::dec
                << " IFF=" << n->cpu.iff1 << "/" << n->cpu.iff2 << " HALT=" << bool(n->pins&Z80_HALT)
                << " WAIT=" << bool(n->pins&Z80_WAIT) << " INT=" << bool(n->pins&Z80_INT) << "\n";
            for(unsigned i=0;i<2;++i){const auto& p=n->pio[i];
                out << "PIO" << i << " mode=" << unsigned(p.mode) << " enabled=" << p.enabled
                    << " ready=" << p.ready << " inputFull=" << p.inputFull
                    << " pending=" << p.pending << " servicing=" << p.servicing << "\n";}
            out << "CTC interrupt states=";
            for(const auto& c:n->ctc.chn)out << unsigned(c.int_state) << ' ';
            out << "\n";
        }
        out << "main task flags IY+0/IY+9=" << unsigned(m.readMemory(false,m.main.cpu.iy))
            << "/" << unsigned(m.readMemory(false,std::uint16_t(m.main.cpu.iy+9))) << "\n";
        if(audio_) {
            const auto q=audio_->stats();
            out << "HOST AUDIO queue frames=" << q.queued << " (" << double(q.queued)/48.0
                << " ms) limit=" << q.limit << " highwater=" << q.highWater << " dropped=" << q.dropped << " missing=" << q.missing << " interval-dropped/s="<<droppedRate_<<" interval-missing/s="<<missingRate_<<"\n";
        }
        const auto dsp=m.chipAudioStats();
        out << "HOST CHIP model=" << (analog::nonlinearAudioMode(m.runtimeAudioMode())?(m.runtimeAudioMode()==analog::RuntimeAudioMode::NonlinearChip192?"SSM2045 nonlinear192k worker":"SSM2045 nonlinear96k worker"):"Legacy zero-Q")
            << " nativeDAC="<<m.nativeAudioEnabled()<<" eventOverflow="<<m.nativeAudioOverflows()<<" eventOrderErrors="<<m.nativeAudioOrderingErrors()<<" S3528=" << m.reconstructionEnabled() << " queueFrames=" << dsp.queuedFrames << " rendered=" << dsp.rendered << " dropped=" << dsp.dropped << " failures=" << dsp.failures << " load="<<dsp.load<<" blockMs="<<dsp.blockMs<<" solverSamples="<<dsp.solverSamples<<" iterations="<<dsp.iterations<<" fallbacks="<<dsp.fallbacks<<" recovered="<<dsp.recoveries<<" held="<<dsp.held<<" waitMs="<<dsp.waitMs<<" maxWaitMs="<<dsp.maxWaitMs<<" multimedia="<<dsp.multimedia<<" outputDC="<<dsp.outputDc<<" clippedFrames="<<dsp.clippedFrames<<" calibrationRev="<<dsp.calibrationRevision<<"\n";
        out << "MIDI HOST panelWheels=" << midiTools_.wheelBridge << " noteOffZero=" << midiTools_.noteOffZero
            << " capture=" << midiTools_.recording << " events=" << midiTools_.total
            << " overwritten=" << midiTools_.overwritten << "\n";
        out << "MIDI HOST CC64 enqueued seen_mask=" << std::hex << midiTools_.sustainSeenMask
            << " down_mask=" << midiTools_.sustainDownMask << std::dec
            << " (host observations, not firmware state)\n";
        out << "MIDI UART received/read/overrun=" << m.midiSerial.received << '/'
            << m.midiSerial.reads << '/' << m.midiSerial.overruns
            << " wire delivered/dropped=" << m.midiWire.delivered << '/' << m.midiWire.dropped
            << " queued bytes=" << m.midiWire.queuedBytes() << "\n";
        out << "LED cathodes BA/IC2,B2/IC3,B0/IC1=" << std::hex;
        for(const auto value:m.scannerControls.leds)out << unsigned(value.value) << '/' << unsigned(value.known) << ' ';
        out << std::dec << " (active low; value/known mask)\n";
        out << "DMA WR0..6=" << std::hex;
        for(auto v:m.diskDma.wr)out << unsigned(v) << ' ';
        out << " timing=" << unsigned(m.diskDma.timing[0]) << '/' << unsigned(m.diskDma.timing[1])
            << " interruptControl=" << unsigned(m.diskDma.interruptControl)
            << " start=" << m.diskDma.start[0] << '/' << m.diskDma.start[1]
            << " address=" << m.diskDma.address[0] << '/' << m.diskDma.address[1]
            << " length/count=" << m.diskDma.length << '/' << m.diskDma.count << std::dec
            << " loaded=" << m.diskDma.loaded << " forced=" << m.diskDma.forced
            << " EOB=" << m.diskDma.endOfBlock << " granted=" << m.diskDma.granted
            << " irqEnabled=" << m.diskDma.irqEnabled << " pending=" << m.diskDma.pending
            << " servicing=" << m.diskDma.servicing << "\n";
        out<<"FILTER SERVICE profile (nonlinear only):\n"<<calibration_.serialize();
        out << m.report();capturedReport_=out.str();reportText_.capture(capturedReport_);
    }
    void apply(Command &c) {
        switch (c.kind) {
        case Kind::HddPause:
            running_=false;remaining_=0;if(audio_)audio_->clear();
            if(c.mediaReply)c.mediaReply->set_value({hddState(),floppyState()});else c.hddReply->set_value(hddState());break;
        case Kind::FloppyMount:
            try {
                if(running_||remaining_)throw std::runtime_error("Pause before changing floppy media.");
                if(!machine_)throw std::runtime_error("Boot the machine before inserting a floppy.");
                if(floppyState().dirty&&!c.ticks)throw std::runtime_error("Floppy has unsaved changes. Save a copy or discard explicitly.");
                if(!floppyState().idle)throw std::runtime_error("Floppy write active. Finish the sampler disk operation first.");
                std::shared_ptr<host::FloppyImageBacking> fresh;
                if(!c.path.empty())fresh=std::make_shared<host::FloppyImageBacking>(c.path,c.index,
                    floppyBacking_&&sameImage(c.path,floppyBacking_->path)?floppyBacking_->lease:nullptr);
                if(fresh)installFloppy(*machine_,fresh);
                else {if(machine_->diskRead){machine_->diskRead->eject(0);machine_->diskRead->persistTrack={};}
                    if(machine_->floppy.drives[0])machine_->floppy.drives[0]->eject();machine_->diskDma.floppyIo=false;}
                floppyBacking_=std::move(fresh);floppyLastSaved_.clear();c.reply->set_value("Floppy media changed.");
            }catch(...){c.reply->set_exception(std::current_exception());}break;
        case Kind::FloppySave:
            try {
                if(running_||remaining_)throw std::runtime_error("Pause before saving a floppy copy.");
                if(!floppyBacking_)throw std::runtime_error("No floppy mounted.");
                if(!floppyState().idle)throw std::runtime_error("Finish the sampler floppy operation before saving.");
                floppyBacking_->saveCopy(c.path);floppyLastSaved_=c.path;machine_->diskRead->dirty[0]=false;c.reply->set_value("Floppy copy saved.");
            }catch(...){c.reply->set_exception(std::current_exception());}break;
        case Kind::HddSave:
            try {
                if(running_||remaining_)throw std::runtime_error("Pause before saving an HDD copy.");
                if(!machine_||!machine_->hardDisk)throw std::runtime_error("No HDD mounted.");
                if(!hddState().idle)throw std::runtime_error("HDD transfer is active. Resume, let the sampler finish, then pause and save.");
                host::saveNewHddImage(c.path,machine_->hardDisk->image);
                hddLastSaved_=c.path;machine_->hardDisk->dirty=false;c.reply->set_value("HDD copy saved.");
            }catch(...){c.reply->set_exception(std::current_exception());}
            break;
        case Kind::Boot: {
            // Build/validate off to the side; a failed load preserves the old
            // paused machine, not a half-initialized live replacement.
            running_ = false;
            remaining_ = 0;
            if((hddState().dirty||floppyState().dirty)&&!c.boot.discardHddChanges)
                throw std::runtime_error("Disk images have unsaved changes. Save copies or explicitly discard before reset.");
            if(!c.boot.hddImage.empty()&&c.boot.diagnostic)
                throw std::runtime_error("HDD boot requires main23.bin; disable Diagnostic EPROM.");
            auto fresh = std::make_unique<Machine>(c.boot.concreteStartup ? 0 : 255);
            const auto &p = c.boot.romFolder;
            fresh->load(p / (c.boot.diagnostic ? "diag.bin" : c.boot.hddImage.empty()?"main.bin":"main23.bin"), p / "scanner.bin",
                        p / "ic135.bin", p / "ic137.bin");
            if (c.boot.concreteStartup) {
                fresh->initializeDramPowerOn(1);
                if (!c.boot.referenceExecution)
                    fresh->enableProductionController(true);
                // Both routes use the same explicit controller power-on
                // realization, so the reference override is a like-for-like
                // oracle; only the normal route swaps in the concrete engine.
                fresh->initializeControllerPowerOn(0xe1122026u);
            }
            std::string media="No disk";
            std::shared_ptr<host::FloppyImageBacking> freshFloppy;
            if(!c.boot.disk.empty()){
                freshFloppy=std::make_shared<host::FloppyImageBacking>(c.boot.disk,c.boot.directImageWrites,
                    floppyBacking_&&sameImage(c.boot.disk,floppyBacking_->path)?floppyBacking_->lease:nullptr);
                installFloppy(*fresh,freshFloppy);
                media=freshFloppy->imported?"Experimental HD OS E2O import / read-only":freshFloppy->hfe?"HFE FM / writable":"EMUIIFD / writable";
            }
            std::shared_ptr<host::DirectImageFile> freshLease;
            if(!c.boot.hddImage.empty()){
                fresh->installHardDisk(host::loadHddImage(c.boot.hddImage));
                if(c.boot.directImageWrites){
                    freshLease=hddLease_&&sameImage(c.boot.hddImage,hddSource_)?hddLease_:
                        std::make_shared<host::DirectImageFile>(c.boot.hddImage,fresh->hardDisk->image);
                    fresh->hardDisk->persistImage=[freshLease](std::span<const std::uint8_t> bytes){freshLease->persist(bytes);};
                }
            }
            for (unsigned i = 0; i < 6; ++i)
                fresh->scannerControls.setVoltage(input(i), c.boot.controls[i]);
            const bool reference = c.boot.referenceExecution || !c.boot.concreteStartup;
            if (!reference) {
                fresh->enableNormalRuntime();
            }
            if(audio_) {
                fresh->enableRuntimeAudio([queue=audio_](auto frames){queue->push(frames);},c.boot.audioMode);
            }
            calibration_=c.boot.calibration;fresh->calibrateAudio(calibration_);
            reconstruction_=c.boot.reconstruction;fresh->reconstruction(reconstruction_);nativeAudio_=c.boot.nativeAudio;fresh->nativeAudio(nativeAudio_);
            fresh->sampleInput=sampleSource_;
            hddSource_=c.boot.hddImage;hddLastSaved_.clear();hddLease_=std::move(freshLease);floppyBacking_=std::move(freshFloppy);floppyLastSaved_.clear();
            machine_ = std::move(fresh);++machineGeneration_;if(audio_)++audioGeneration_;
            if(audio_)audio_->clear();
            midiTools_.resetInputState();
            referenceExecution_ = reference;
            realTime_ = c.boot.realTime;
            sampleTime_ = std::chrono::steady_clock::now();
            sampleTick_ = 0;
            sampleCpu_=threadBusy_=-1;quantumMs_=meanQuantumMs_=maxQuantumMs_=0;
            speedRatio_ = 0;
            release_.fill(0);
            contacts_ = {};
            media_ = media;
            if(machine_->productionControllerEnabled())
                startup_ = "Production: phase0 / RAM seed1 / controller seed e1122026";
            else if(c.boot.concreteStartup)
                startup_ = "Seeded reference: phase0 / RAM seed1 / controller seed e1122026";
            else
                startup_ = "Verification: unknown RAM / all16 controller phases";
            error_.clear();capturedReport_.clear();reportText_.reset();liveSerial_=c.boot.liveSerial;
            break;
        }
        case Kind::SampleSource:
            sampleSource_=std::move(c.source);if(machine_)machine_->sampleInput=sampleSource_;break;
        case Kind::NativeAudio:
            nativeAudio_=c.index!=0;if(machine_)machine_->nativeAudio(nativeAudio_);break;
        case Kind::Reconstruction:
            reconstruction_=c.index!=0;if(machine_)machine_->reconstruction(reconstruction_);break;
        case Kind::Calibration:
            calibration_=c.calibration;if(machine_)machine_->calibrateAudio(calibration_);break;
        case Kind::AudioModel:
            if(machine_&&audio_) {
                machine_->enableRuntimeAudio([queue=audio_](auto frames){queue->push(frames);},analog::RuntimeAudioMode(c.index));
                machine_->calibrateAudio(calibration_);machine_->reconstruction(reconstruction_);machine_->nativeAudio(nativeAudio_);++audioGeneration_;audio_->clear();
            }
            break;
        case Kind::Resume:
            if (machine_ && !running_) {
                running_ = true;
                sampleTime_ = std::chrono::steady_clock::now();
                sampleTick_ = machine_->ticks;pacer_.start(sampleTime_,sampleTick_);
                sampleCpu_=host::threadCpuSeconds();threadBusy_=-1;quantumMs_=meanQuantumMs_=maxQuantumMs_=0;
                audioRateTime_={};droppedRate_=missingRate_=0;
                speedRatio_ = 0;
            }
            break;
        case Kind::Pause:
            if(audio_)audio_->clear();
            running_ = false;
            remaining_ = 0;
            break;
        case Kind::Step:
            if (machine_) {
                running_ = false;
                remaining_ = c.ticks;
            }
            break;
        case Kind::Pulse:
            if (machine_) {
                machine_->scannerControls.setButton(ScannerButton(c.index), true);
                release_[c.index] = machine_->ticks + 800000;
                contacts_[c.index] = {true, machine_->ticks, {}};
            }
            break;
        case Kind::Button:
            if (machine_) {
                release_[c.index] = 0;
                auto& contact = contacts_[c.index];
                const bool pressed = c.ticks != 0;
                const bool last = contact.pending.empty() ? contact.held : contact.pending.back();
                if (pressed != last) {
                    if (contact.pending.empty() && contact.tick != machine_->ticks) {
                        machine_->scannerControls.setButton(ScannerButton(c.index), pressed);
                        contact.held = pressed;
                        contact.tick = machine_->ticks;
                    } else {
                        // A down/up pair may arrive in one worker batch. Give
                        // each edge an executing quantum instead of erasing it.
                        contact.pending.push_back(pressed);
                    }
                }
            }
            break;
        case Kind::Voltage:
            if (machine_)
                machine_->scannerControls.setVoltage(input(c.index), c.volts);
            break;
        case Kind::Midi: {
            const auto raw=std::span(reinterpret_cast<const std::uint8_t*>(c.text.data()),c.text.size());
            const auto packet=midiTools_.adapt(raw);
            bool accepted=false;
            if(machine_) {
                if(packet.wheel)machine_->scannerControls.setVoltage(input(packet.wheel->control),packet.wheel->volts);
                accepted=machine_->receiveMidi(std::span(packet.bytes.data(),raw.size()));
                if(accepted)midiTools_.observeEnqueued(std::span(packet.bytes.data(),raw.size()));
            }
            if(midiTools_.recording)midiTools_.observe(raw,packet,c.hostNs,hostNanos(),machine_?machine_->ticks:0,accepted);
            break;
        }
        case Kind::MidiOptions:
            midiTools_.wheelBridge=c.index;midiTools_.noteOffZero=c.ticks;break;
        case Kind::MidiRecord:
            midiTools_.record(c.ticks!=0);break;
        case Kind::MidiExport:
            c.reply->set_value(midiTools_.exportTsv());break;
        case Kind::TraceMetrics:
            traceMetrics_=c.index!=0;break;
        case Kind::LiveSerial:
            liveSerial_=c.index!=0;
            if(liveSerial_ || capturedReport_.empty()){capturedReport_.clear();reportText_.reset();}
            break;
        case Kind::Capture:
            if(machine_)captureReport(c.text);
            break;
        case Kind::Serial:
            if (machine_) {
                for (unsigned char b : c.text)
                    if (b < 128)
                        machine_->serialRx.push_back(b);
                machine_->serialRx.push_back('\r');
            }
            break;
        }
    }
    void loop() noexcept {
        host::nameCurrentThread(L"EII machine");
        host::placeCurrentThread(host::CpuRole::Machine);
        auto last = std::chrono::steady_clock::now();
        bool inputHold=false;
        for (;;) {
            std::deque<Command> pending;
            {
                std::unique_lock lock(mutex_);
                if (commands_.empty() && !running_ && !remaining_ && !quit_)
                    wake_.wait(lock, [&] { return quit_ || !commands_.empty(); });
                if (quit_)
                    return;
                pending.swap(commands_);
            }
            // Continuous input is published at the normal UI cadence. A host
            // packet or wheel movement must not force a full snapshot copy.
            bool changed = std::any_of(pending.begin(),pending.end(),
                [](const Command& c){return c.kind!=Kind::Midi && c.kind!=Kind::Voltage && c.kind!=Kind::Button && c.kind!=Kind::Calibration;});
            changed |= !pending.empty() && !running_ && !remaining_;
            try {
                for (auto &c : pending)
                    apply(c);
                // A command can interrupt the pacing wait. Apply it promptly,
                // but do not execute another quantum while already ahead of
                // wall time: dense MIDI must not build a future audio backlog.
                bool mayRun=true;inputHold=false;
                if(machine_ && running_ && realTime_) {
                    const auto now=std::chrono::steady_clock::now();pacer_.boundDebt(now,machine_->ticks);
                    mayRun=now>=pacer_.deadline(machine_->ticks);
                    // Sampling a live input: the captured stream paces emulated time.
                    if(mayRun && !machine_->sampleInputReady(20000)){mayRun=false;inputHold=true;}
                }
                if (machine_ && (running_ || remaining_) && mayRun) {
                    const auto n = running_ ? 20000 : std::min<std::uint64_t>(20000, remaining_);
                    const auto workStart=std::chrono::steady_clock::now();
                    machine_->run(n);
                    quantumMs_=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-workStart).count();
                    meanQuantumMs_=meanQuantumMs_?meanQuantumMs_*.95+quantumMs_*.05:quantumMs_;
                    maxQuantumMs_=std::max(maxQuantumMs_,quantumMs_);
                    if (!running_)
                        remaining_ -= n;
                    for (unsigned i = 0; i < 30; ++i) {
                        auto& contact = contacts_[i];
                        if (!contact.pending.empty() && contact.tick < machine_->ticks) {
                            contact.held = contact.pending.front();
                            contact.pending.pop_front();
                            contact.tick = machine_->ticks;
                            machine_->scannerControls.setButton(ScannerButton(i), contact.held);
                        }
                        if (release_[i] && machine_->ticks >= release_[i]) {
                            machine_->scannerControls.setButton(ScannerButton(i), false);
                            release_[i] = 0;
                            contacts_[i] = {false, machine_->ticks, {}};
                        }
                    }
                    changed |= !running_ && !remaining_;
                }
            } catch (const std::exception &e) {
                if(audio_)audio_->clear();
                error_ = e.what();
                running_ = false;
                remaining_ = 0;
                changed = true;
            }
            const auto now = std::chrono::steady_clock::now();
            if (changed || now - last >= std::chrono::milliseconds(50)) {
                publish();
                last = now;
            }
            if (running_ && realTime_ && machine_) {
                const auto now=std::chrono::steady_clock::now();pacer_.boundDebt(now,machine_->ticks);
                auto deadline=pacer_.deadline(machine_->ticks);
                if(inputHold)deadline=now+std::chrono::milliseconds(1); // wait for the capture stream
                std::unique_lock lock(mutex_);
                wake_.wait_until(lock, deadline, [&] { return quit_ || !commands_.empty(); });
            }
        }
    }
};
} // namespace eii


