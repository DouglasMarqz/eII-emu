#pragma once
#include "am6072.h"
#include "diagnostics.h"
#include "s3528.h"
#include "clocked_playback.h"
#include "runtime_audio.h"
#include "midi_serial.h"
#include "sample_input.h"
#include "sample_input_analog.h"
#include <memory>
#include <array>
#include <cstdint>
#include <filesystem>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "z80.h"
#include "z80ctc.h"
#include "ctc_event_runtime.h"
#include "prom.h"
#include "control_voltage.h"
#include "channel_timers.h"
#include "controller_registers.h"
#include "scanner_controls.h"
#include "floppy_mechanics.h"
#include "disk_read_path.h"
#include "disk_dma.h"
#include "hard_disk.h"
#include "disk_sio.h"

namespace eii {
struct Lcd {
    std::array<std::uint8_t,128> ram{};
    std::uint8_t address=0;
    bool increment=true,display=false;
    std::uint64_t writes=0;
    Lcd() {ram.fill(' ');}
    void write(bool data,std::uint8_t value);
    std::uint8_t read(bool data);
    std::string line(int row) const;
};
// PIO register model with board-specific clocked mode-2 handshakes and IRQs.
// Other handshake modes, bit interrupts and subcycle timing remain incomplete.
struct PioPort {
    std::uint8_t mode=1,output=0,input=255,mask=255,interruptMask=255,vector=0,control=0;
    bool expectDirection=false,expectMask=false,enabled=false,ready=false,pending=false,servicing=false;
    bool inputFull=false;
    void command(std::uint8_t value);
    std::uint8_t read();
};
struct Node {
    z80_t cpu{};
    z80ctc_t ctc{};
    CtcEventRuntime ctcEvents;
    std::uint64_t pins=0;
    std::array<PioPort,2> pio{};
    std::array<std::uint8_t,8192> rom{};
    std::array<std::uint64_t,65536> fetched{};
    std::array<std::uint64_t,256> ins{},outs{};
    bool scanner=false,ioActive=false,ioUnknownObserved=false,pioIrqActive=false;
    std::uint8_t ioValue=255;
    std::array<std::uint64_t,2> pioAcks{};
    void reset();
};
struct PlaybackSwitchInputs {
    std::array<LogicLevel,8> phi1{},phi2{};
    std::array<KnownByte,8> reconstructionCode{};
    std::array<analog::KnownSignal,8> reconstructionCutoffHz{};
};
class Machine {
public:
    Node main,scanner;
    Lcd lcd;
    std::array<std::uint8_t,2048> scannerRam{};
    std::vector<std::uint8_t> memory;
    std::vector<std::uint8_t> dramKnown;
    // The 512 KB bank not currently addressed (only with installSecondBank).
    std::vector<std::uint8_t> otherBank_,otherBankKnown_;
    std::array<std::uint8_t,0x2800> otherUnderlay_{},otherUnderlayKnown_{};
    bool secondBank_=false,rtsBank_=false;
    // Keep existing CPU-local backing separate from physically installed
    // bank0 DRAM beneath ROM0000..1fff and SRAM2000..27ff.
    std::array<std::uint8_t,0x2800> dramUnderlay{},dramUnderlayKnown{};
    // EII+ 1 MB memory (docs/MEMORY_1MB.md): 32 x 41256 replace the 4164s;
    // the 9th DRAM address bit (74LS153 row/column mux, 74S74 sync) comes from
    // SIO channel B /RTS, so every DRAM agent (CPU, playback, disk DMA) sees
    // the same 512 KB bank. Modelled by exchanging the whole 512 KB DRAM array
    // (window 2800h-7FFFFh plus the bank0 underlay) when WR5 D1 changes. Main
    // boot 0A8Bh detects it; 0AEEh swaps (with the channel timers stopped).
    // Off by default: a standard EII has 512 KB.
    void installSecondBank(bool installed);
    bool secondBankInstalled() const noexcept {return secondBank_;}
    bool rtsBankSelected() const noexcept {return rtsBank_;}
    std::uint64_t bankSwaps=0;
    DramBackingView dramBacking() const noexcept {
        return {&memory,&dramKnown,dramUnderlay.data(),dramUnderlayKnown.data(),0x2800};
    }
    bool cpuDramEnabled=false,dramBusActive=false,dramServiced=false;
    std::uint32_t dramBusAddress=0;
    std::uint64_t dramWaitClocks=0,dramReadCycles=0,dramWriteCycles=0;
    std::array<std::uint8_t,4> pages{};
    bool pageReset=true;
    PromLatch prom; // Runtime images/reference kernel; running control lives in controllerTiming.
    ControlVoltageBus controlVoltage;
    std::array<analog::S3528Control,8> reconstructionControl{};
    ChannelTimers channelTimers;
    CpuRegisterStaging registerStaging;
    bool stagingWritePending=false;
    std::uint8_t stagingWritePort=0,stagingWriteData=0;
    MidiSerial2651 midiSerial;
    MidiWire midiWire;
    // 7-14 PIOB7 +MODE ->7-43/44. Known MODE=0 enables the held
    // DAC0 bus and forces decode; sampling/unknown mode is not audio.
    bool channel0PlaybackEnabled() const noexcept {
        const auto& b=main.pio[1];
        return (b.mode==0 || (b.mode==3 && !(b.mask&128))) && !(b.output&128);
    }
    // 7-14 PIO B7 +IMODE level at the pin: an input-mode/masked bit floats.
    LogicLevel channel0Mode() const noexcept {
        const auto& b=main.pio[1];
        if(b.mode==0 || (b.mode==3 && !(b.mask&128)))return {true,bool(b.output&128)};
        return {};
    }
    // 7-43 SAR/logic (IC11 MC14559B). The comparator (+IDATA.D) is an
    // explicit external level until the analog input model drives it.
    // Channel-0 DAC input bus +IDAC7..0: IC43 74HCT374 (/OE=+IMODE, 7-44) or
    // the SAR via IC12 74HCT244 (/OE=/IMODE, 7-43). Unknown IMODE merges.
    KnownByte channel0DacBus() const noexcept {
        const auto mode=channel0Mode();
        const auto stored=channelTimers.controllerTiming.channelDacCode(0),sar=sampleSar.register_();
        if(mode.known)return mode.high?sar:stored;
        return {stored.value,std::uint8_t(stored.known&sar.known&~(stored.value^sar.value))};
    }
    // +IDAC.V (LF356 output) for the current bus, ENCODE and a given ACV0.
    analog::KnownSignal channel0IdacVolts(analog::KnownSignal amplitudeCv) const {
        return analog::Am6072::eiiSteadyVoltsMode(channel0DacBus(),amplitudeCv,sampleSar.encode(channel0Mode()));
    }
    // 7-44: the channel-0 output path (DAC -> deglitch IINT -> SSM2045 ->
    // S3528 -> C0OUT) runs in both modes; with IMODE=1 the DAC input is the
    // SAR bus (the input monitor of the PDF43/44 offset trim). Unknown = mute.
    bool channel0AudioEnabled() const noexcept {return channel0Mode().known;}
    bool receiveMidi(std::span<const std::uint8_t> bytes) {return midiWire.enqueue(bytes,ticks);}
    std::string serialTx;
    std::deque<std::uint8_t> serialRx;
    bool serialReady=false;
    std::uint8_t serialByte=0;
    std::uint64_t serialNext=0;
    std::uint64_t ticks=0,unmappedIo=0,commands=0;
    ScannerControls scannerControls;
    FloppyBus floppy;
    DiskSio diskSio;
    DiskDma diskDma;
    std::unique_ptr<HardDisk> hardDisk;
    void installHardDisk(std::vector<std::uint8_t> image){hardDisk=std::make_unique<HardDisk>(std::move(image));diskDma.hdIo=true;}
    std::uint64_t dmaDramReads=0,dmaDramWrites=0,dmaDramWaitClocks=0;
    std::unique_ptr<DiskReadPath> diskRead;
    // IC23 Q outputs: /SDOS and /SDIS are their complements.
    bool scanToMainPulse=false,mainToScanPulse=false;
    std::uint64_t scanToMainTransfers=0,mainToScanTransfers=0;
    std::function<void(bool,bool,std::uint16_t,std::uint8_t,std::uint16_t)> trace;
    explicit Machine(std::uint8_t initialControllerPhase=255);
    Machine(const Machine&)=delete;
    Machine& operator=(const Machine&)=delete;
    Machine(Machine&&)=delete;
    Machine& operator=(Machine&&)=delete;
    void enableCpuDram() noexcept;
    void enableRuntimeDevices(bool enable=true) noexcept {
        if(runtimeDevices_) {main.ctcEvents.flush(main.ctc);scanner.ctcEvents.flush(scanner.ctc);}
        runtimeDevices_=enable;
        channelTimers.controllerTiming.enablePortEvents(enable);
        diskSio.enableEventTiming(enable);floppy.enableEventTiming(enable);
        refreshPioIrq(main);refreshPioIrq(scanner);
        if(diskRead)diskRead->enableEventTiming(enable);
        main.ctcEvents.reset(main.ctc,ticks);scanner.ctcEvents.reset(scanner.ctc,ticks);
    }
    // One owner-selected route for ordinary execution, shared by UI/probes.
    // Verification remains the default Machine constructor behavior.
    void enableNormalRuntime() noexcept {
        channelTimers.enableEventTiming();enableRuntimeDevices();
        channelTimers.controllerTiming.enableConcreteRuntime();
        channelTimers.controllerTiming.enableVoiceTransactions();
        if constexpr(!diagnosticsEnabled)
            channelTimers.controllerTiming.enableBlockPublication();
    }
    bool normalRuntimeEnabled() const noexcept {
        return runtimeDevices_ && channelTimers.eventTimingEnabled() &&
            channelTimers.controllerTiming.normalRuntimeEnabled();
    }
    // Optional concrete power-on realization, chosen before the first tick.
    // This is a reproducible initial condition, not a measured EII bit pattern.
    // The default constructor retains unknown DRAM for verification.
    void initializeDramPowerOn(std::uint32_t seed);
    void updateMemoryBank() noexcept;
    // Opt-in whole-controller concrete engine. Must be selected before the
    // first clock and initialized through initializeControllerPowerOn(seed).
    void enableProductionController(bool enable=true) {
        channelTimers.controllerTiming.enableProductionEngine(enable);
    }
    bool productionControllerEnabled() const noexcept {
        return channelTimers.controllerTiming.productionEngineEnabled();
    }
    void initializeControllerPowerOn(std::uint32_t seed) {
        if(ticks || channelTimers.halfClocks || channelTimers.sourceClocks || channelTimers.writes ||
           std::any_of(registerStaging.writes.begin(),registerStaging.writes.end(),[](auto n){return n!=0;}))
            throw std::logic_error("digital power-on must precede CPU/source clocks and programming");
        channelTimers.controllerTiming.initializePowerOn(seed,registerStaging);
        channelTimers.initializeOutputPowerOn(seed);
    }
    void load(const std::filesystem::path& mainRom,const std::filesystem::path& scannerRom,
              const std::filesystem::path& p135={},const std::filesystem::path& p137={});
    void run(std::uint64_t cycles);
    std::uint32_t physical(std::uint16_t address,bool dma=false) const;
    std::uint8_t readMemory(bool scan,std::uint16_t address) const;
    void writeMemory(bool scan,std::uint16_t address,std::uint8_t value);
    std::uint8_t input(Node& n,std::uint8_t port);
    void output(Node& n,std::uint8_t port,std::uint8_t value);
    analog::PlaybackInputs playbackAnalogInputs() const noexcept;
#ifdef EII_PLAYBACK_TIMING_LAB
    // Laboratory only: callback observes the preceding50ns interval before
    // each controller half-edge. Must not mutate/re-enter Machine. Empty by
    // default; attaching selects the complete edge scheduler, not DSP audio.
    void playbackTimingObserver(std::function<void(std::uint64_t)> observer) {
        playbackTimingObserver_=std::move(observer);
    }
#endif
    PlaybackSwitchInputs playbackSwitchInputs() const;
    void enableClockedPlayback(analog::ClockedPlaybackOptions options,analog::ClockedPlayback::Sink sink={});
    void disableClockedPlayback() noexcept {playback_.reset();}
    const analog::ClockedPlayback* clockedPlayback() const noexcept {return playback_.get();}
    // Nonlinear modes invoke sink on their dedicated DSP owner: sink must not read Machine.
    // channelSink: individual outputs J1..J8 (sheet 7-52), before sink, same frames.
    void enableRuntimeAudio(analog::RuntimeAudioEngine::Sink sink,
                            analog::RuntimeAudioMode mode=analog::RuntimeAudioMode::BasicVoice,
                            analog::RuntimeAudioEngine::ChannelSink channelSink={}) {
        channelTimers.audioSink({});
        runtimeAudio_.reset(); // Join old DSP before replacing its sink.
        if(analog::workerAudioMode(mode)) {
            // DSP thread must never materialize/read a live controller.
            runtimeAudio_=std::make_unique<analog::RuntimeAudioEngine>(std::move(sink),mode,std::move(channelSink));
        } else runtimeAudio_=std::make_unique<analog::RuntimeAudioEngine>(
            [this,sink=std::move(sink)](auto frames) {
                channelTimers.controllerTiming.materializeProductionObservation();
                if(sink)sink(frames);
            },mode,std::move(channelSink));
    }
    void channelOutputs(bool on){if(runtimeAudio_)runtimeAudio_->channelOutputs(on);}
    void analogDrift(bool on){if(runtimeAudio_)runtimeAudio_->analogDrift(on);}
    void reconstruction(bool enabled){if(runtimeAudio_)runtimeAudio_->reconstruction(enabled);}
    void nativeAudio(bool enabled);
    bool nativeAudioEnabled()const noexcept{return runtimeAudio_&&runtimeAudio_->nativeAudioEnabled();}
#if EII_PLAYBACK_TIMING_LAB
    void nativeFrameObserver(std::function<void(const analog::NativeFrame&)> observer){if(runtimeAudio_)runtimeAudio_->nativeFrameObserver(std::move(observer));}
#endif
    std::uint64_t nativeAudioOverflows()const noexcept{return runtimeAudio_?runtimeAudio_->nativeOverflows():0;}
    std::uint64_t nativeAudioOrderingErrors()const noexcept{return runtimeAudio_?runtimeAudio_->nativeOrderingErrors():0;}
    bool reconstructionEnabled()const noexcept{return runtimeAudio_&&runtimeAudio_->reconstructionEnabled();}
    void calibrateAudio(const analog::FilterCalibration& c){if(runtimeAudio_)runtimeAudio_->calibrate(c);}
    analog::RuntimeAudioMode runtimeAudioMode()const noexcept {return runtimeAudio_?runtimeAudio_->mode():analog::RuntimeAudioMode::BasicVoice;}
    analog::ChipAudioWorker::Stats chipAudioStats()const {return runtimeAudio_?runtimeAudio_->chipStats():analog::ChipAudioWorker::Stats{};}

    std::string report() const;
private:
    void sampleInputLifecycleTick();
    bool sampleWaitStep(Node& n,bool dramHold);
    bool sampleReadSelected(std::uint64_t pins) const noexcept {
        return (pins&(Z80_IORQ|Z80_RD|Z80_M1))==(Z80_IORQ|Z80_RD) && (Z80_GET_ADDR(pins)&0xf8)==0x08;
    }
    std::uint8_t sampleReadValue(Node& n);
    bool dmaReadyN();
    void nativeControls();
    std::unique_ptr<analog::RuntimeAudioEngine> runtimeAudio_;
    bool runtimeDevices_=false,floppyQuiet_=false,floppySkipped_=false;
    bool dmaBus_=false;
    std::uint64_t cpuResumePins_=0;
    std::unique_ptr<analog::ClockedPlayback> playback_;
#ifdef EII_PLAYBACK_TIMING_LAB
    std::function<void(std::uint64_t)> playbackTimingObserver_;
#endif
    void step(Node& n);
    template<bool Scanner> void stepNode(Node& n);
    void updateFloppy();
    void pioWrite(Node& n,unsigned port,std::uint8_t value);
    static void refreshPioIrq(Node& n) noexcept {
        n.pioIrqActive=false;
        for(const auto& p:n.pio)n.pioIrqActive|=p.servicing||(p.enabled&&!p.expectMask&&p.pending);
    }
    void pioLinkTick();
    std::uint64_t pioInterrupts(Node& n,std::uint64_t pins);
public:
    // Sample input state (7-42..7-44) lives at the end of Machine so the
    // hot playback/CPU members keep their layout.
    SampleSar sampleSar;
    // 7-42 analog input path; +INPUT.V comes from an optional host signal
    // (none = jack open, 0 V). sampleComparatorExternal replaces +IDATA.D
    // with sampleComparator for digital tests only.
    analog::SampleInputAnalog sampleAnalog;
    std::shared_ptr<analog::SampleInputSource> sampleInput;
    // Live input pacing: false while the next `n` ticks of input are not yet
    // captured (only while the converter samples a live source).
    bool sampleInputReady(std::uint64_t n) {return !sampleInput||sampleInput->ready(ticks,n);}
    bool sampleComparatorExternal=false;
    LogicLevel sampleComparator{};
    // 7-9 scanner PIO IC46 B7 -> -PGAIN1.D, B6 -> +PGAIN2.D (CN8-1/3).
    LogicLevel scannerPioBPin(unsigned bit) const noexcept {
        const auto& b=scanner.pio[1];
        if(b.mode==0 || (b.mode==3 && !(b.mask&(1u<<bit))))return {true,bool(b.output&(1u<<bit))};
        return {};
    }
    std::uint64_t sampleReads=0,sampleUnknownReads=0,sampleWaitClocks=0,sampleReadyUnknown=0,sampleWaitUnknown=0;
    // Sheet7-6/PDF69: IC45 CT1 <- -PRXC.D, NOT ZC0. Sheet7-16/PDF79
    // IC130 /Q supplies the SMPTE receive clock; idle high without input.
    // External receiver/clock decoding remains unimplemented; explicit pin
    // input is provided for validation/future hardware, not a host tempo hack.
    bool scannerSmpteClockHigh=true;
private:
    std::uint64_t sampleIsmplSeen_=0,sampleAmbiguousSeen_=0;
    KnownByte sampleMonitorCode_{};
    std::uint64_t sampleFlushAt_=0,sampleFlushEvents_=0;
    bool nativeMonitorSent_=false,nativeMonitorActive_=false;analog::KnownSignal nativeMonitorValue_{};
    void publishSamplingMonitor(LogicLevel mode);
    bool sampleTracking_=false,sampleWaitPin_=false,sampleAnalogActive_=false,sampleQuiet_=false;
};
std::vector<std::uint8_t> readFile(const std::filesystem::path& path);
}



