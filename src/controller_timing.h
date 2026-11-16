#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include "diagnostics.h"
#include "controller_timing_types.h"
#include "controller_prom.h"
#include "prom.h"
#include "channel_requests.h"
#include "controller_registers.h"
#include "controller_data.h"
#include "controller_alu.h"
#include "dram_cpu.h"
#include "controller_upper_paths.h"
#include "voice_transaction.h"
#include "production_controller.h"

namespace eii {
// S-6149-001 7-17/7-18: IC111/112 S161, IC110 LS164, IC140 S174.
// Physical startup phase is not reset by CRST. Keep all 16 initial phases
// unless an explicit characterized phase is supplied (also useful in tests).
class ControllerTiming {
    friend struct ControllerRuntimeTestAccess;
public:
    std::uint64_t risingEdges=0;
    KnownByte channel{},microCount{},shift{},captured{},requestPipeline{};
    KnownWord control{};
    LogicLevel observedWait{},acknowledgeEnable{};
    std::uint64_t promClocks=0;
    void enablePortEvents(bool enable=true) noexcept {
        portEvents_=enable;
        for(auto& p:paths_) {p.dram.enableEventTiming(enable);for(auto& b:p.upper.branches)b.dram.enableEventTiming(enable);}
    }
    void enableConcreteRuntime(bool enable=true) noexcept {concreteRuntime_=enable;}
    bool normalRuntimeEnabled() const noexcept {return production_!=nullptr || (concreteRuntime_ && voiceTransactions_);}
    // Optional whole-controller concrete engine. It must be selected before the
    // first clock and is initialized by initializePowerOn(seed). The reference
    // remains the default and the oracle; this engine has no representation for
    // unknown levels/masks, floating buses, contention or unknown DRAM.
    void enableProductionEngine(bool enable=true) {
        if(risingEdges || lowAperture_)
            throw std::logic_error("Controller engine selection requires a fresh boot; live switching is unsupported");
        productionWanted_=enable;
        if(!enable)production_.reset();
    }
    bool productionEngineEnabled() const noexcept {return production_!=nullptr;}
    bool productionEngineInvalid() const noexcept {return production_&&!production_->valid();}
    void requireValidProduction(std::uint64_t cpuTick) {
        if(!productionEngineInvalid())return;
        syncProduction();
        static constexpr const char* reasons[]={"none","unknown CPU input","unknown register staging",
            "unknown CPU data","unknown or missing DRAM backing byte at CAS","floating RFD bus",
            "conflicting RFD bus drivers","unknown channel request","unknown CHDB capture","ambiguous power-on UCD04"};
        const auto& e=*production_;const auto& f=e.firstFailure;
        const char* reason=e.invalidCode<10?reasons[e.invalidCode]:"unsupported state";
        throw std::runtime_error("Production controller stopped: "+std::string(reason)+
            " (code "+std::to_string(e.invalidCode)+", CPU tick "+std::to_string(cpuTick)+
            ", source edge "+std::to_string(f.edge)+", phase "+std::to_string(f.phase)+
            ", channel "+std::to_string(f.channel)+", control "+std::to_string(f.control)+
            (e.invalidCode==4 ? ", DRAM address "+std::to_string(e.upper.dram.memoryFaultAddress)+
                (e.upper.dram.memoryFaultCpu?", CPU CAS":", sample CAS") : "")+
            "). Boot/reset is required; the reference engine was not substituted.");
    }
    void enableVoiceTransactions(bool enable=true) noexcept {
        materializeVoiceState();voiceTransactions_=enable;for(auto& p:paths_)p.voiceActive=false;
    }
    // Observation/debug boundary, not a required production source-clock job.
    void materializeVoiceState() noexcept {for(auto& p:paths_)materializeVoice(p);}
    std::uint64_t voiceTransactions=0,voiceTransactionClocks=0;
    std::uint64_t concreteSteps=0;
    void initializePowerOn(std::uint32_t seed,CpuRegisterStaging& cpuStaging) {
        if(risingEdges || lowAperture_ || count_!=1 || !hasUpperProm() || !hasLowerProm() ||
           std::any_of(cpuStaging.writes.begin(),cpuStaging.writes.end(),[](auto n){return n!=0;}))
            throw std::logic_error("controller power-on requires explicit single phase, both PROMs and no executed clocks");
        DigitalPowerOnBits bits(seed);auto& p=paths_[0];
        p.voiceActive=false;
        staging_.address={std::uint8_t(bits.next()&63),63};staging_.data={bits.next()&0xffffff,0xffffff};cpuStaging=staging_;
        p.shift={std::uint8_t(bits.next()),255};
        p.control={std::uint16_t(bits.next()),65535};p.pipeline={std::uint8_t(bits.next()&3),3};
        p.registers.initializePowerOn(bits);
        p.data.low20={bits.next()&0xfffff,0xfffff};p.data.flags={std::uint8_t(bits.next()&7),7};
        p.data.ucd4b={true,bool(bits.next()&1)};p.data.ucd9={true,bool(bits.next()&1)};p.sameUcd4Sample=false;
        p.alu.initializePowerOn(bits,logicNot({true,bool(p.control.value&32)}));
        p.sampledRfd={bits.next()&0xffffff,0xffffff};
        p.cpu.initializePowerOn(bits,{true,bool(p.control.value&512)});
        p.writePort.pending={true,bool(bits.next()&1)};p.writePort.flag={true,bool(bits.next()&1)};
        p.writePort.settle({true,bool(p.channel&8)},{true,bool(p.shift.value&8)});
        const LogicLevel ack{true,bool(bits.next()&1)},delayed9{true,bool(bits.next()&1)};
        p.dram.initializePowerOn(bits,p.control,delayed9);
        initializeUpperPaths();auto& b=p.upper.branches[0];b.ack=ack;b.delayed9=delayed9;
        b.dac.initializePowerOn(bits,p.channel,ack);p.acknowledgeEnable=ack;
        if(productionWanted_) initializeProductionEngine(seed);
        publish();
    }
    void attachCpuDram(DramBackingView view) noexcept {
        dramMemory_=view;dramEnabled_=true;registerWritesEnabled_=true;initializeUpperPaths();
        if(production_)production_->attachCpuDram(view.bytes?view.bytes->data():nullptr,
                                                 view.known?view.known->data():nullptr,view.underlay,view.underlayKnown,view.underlaySize);
    }
    void driveDramCpu(bool active,std::uint32_t address,KnownByte data,bool read) noexcept {
        dramActive_=active;dramRead_=read;
        if(production_){production_->driveDramCpu(active,address,data,read);return;}
        for(unsigned i=0;i<count_;++i) {
            paths_[i].dram.cpuBus(active,address,data,read);
            if(exactUpper_)paths_[i].upper.driveDram(active,address,data,read);
        }
    }
    bool cpuDramReady() const noexcept {
        if(production_)return production_->cpuDramReady();
        if(!dramActive_)return true;
        for(unsigned i=0;i<count_;++i) {
            if(exactUpper_) {for(const auto& b:paths_[i].upper.branches)if(!(dramRead_?b.dram.readReady:b.dram.writeReady))return false;}
            else if(!(dramRead_?paths_[i].dram.readReady:paths_[i].dram.writeReady))return false;
        }
        return true;
    }
    KnownByte cpuDramReadData() const noexcept {
        if(production_)return production_->cpuDramReadData();
        auto data=dramPort(0).readLatch;
        for(unsigned i=0;i<count_;++i) {
            if(exactUpper_)for(const auto& b:paths_[i].upper.branches)data=mergeDramByte(data,b.dram.readLatch);
            else data=mergeDramByte(data,paths_[i].dram.readLatch);
        }
        return data;
    }
    bool cpuDramWrite(std::uint32_t& address,KnownByte& data) const noexcept {
        if(production_)return production_->cpuDramWrite(address,data);
        if(!dramActive_||dramRead_||!cpuDramReady())return false;
        address=dramPort(0).writeAddress;data=dramPort(0).writeData;
        for(unsigned i=0;i<count_;++i) {
            if(exactUpper_) {
                for(const auto& b:paths_[i].upper.branches) {
                    if(b.dram.writeAddress!=address)return false;
                    data=mergeDramByte(data,b.dram.writeData);
                }
            } else {
                if(paths_[i].dram.writeAddress!=address)return false;
                data=mergeDramByte(data,paths_[i].dram.writeData);
            }
        }
        return data.known==255;
    }
    KnownByte channelBusValue() const noexcept {
        if(production_)return production_->channelBusValue();
        auto out=dramPort(0).channelLatch;
        for(unsigned i=0;i<count_;++i) {
            if(exactUpper_)for(const auto& b:paths_[i].upper.branches)out=mergeDramByte(out,b.dram.channelLatch);
            else out=mergeDramByte(out,paths_[i].dram.channelLatch);
        }
        return out;
    }
    KnownByte channelDacCode(unsigned channel) const noexcept {
        if(production_)return production_->channelDacCode(channel);
        if(!exactUpper_||channel>=8)return {};
        auto out=paths_[0].upper.branches[0].dac.codes[channel];
        for(unsigned i=0;i<count_;++i)for(const auto& b:paths_[i].upper.branches)out=mergeDramByte(out,b.dac.codes[channel]);
        return out;
    }
    Known24 dataPipelineValue() const noexcept {
        if(production_)return production_->dataPipelineValue();
        auto value=voiceBeat(paths_[0])?voiceBeat(paths_[0])->data.low20:paths_[0].data.low20;
        for(unsigned i=1;i<count_;++i)value=merge24(value,paths_[i].data.low20);
        return value;
    }
    Known24 sampledRfdValue() const noexcept {
        if(production_)return production_->sampledRfdValue();
        auto value=voiceBeat(paths_[0])?voiceBeat(paths_[0])->rfd:paths_[0].sampledRfd;
        for(unsigned i=1;i<count_;++i)value=merge24(value,paths_[i].sampledRfd);
        return value;
    }
    KnownByte delayedFlagValue() const noexcept {
        if(production_)return production_->delayedFlagValue();
        auto value=voiceBeat(paths_[0])?voiceBeat(paths_[0])->data.flags:paths_[0].data.flags;
        for(unsigned i=1;i<count_;++i)value=merge(value,paths_[i].data.flags);
        return value;
    }
    LogicLevel delayedUcd4() const noexcept {
        if(production_)return production_->delayedUcd4();
        auto value=voiceBeat(paths_[0])?voiceBeat(paths_[0])->data.ucd4b:paths_[0].data.ucd4b;
        for(unsigned i=1;i<count_;++i){const auto x=paths_[i].data.ucd4b;value.known&=x.known && x.high==value.high;}
        return value;
    }
    bool ucd4SamplesLinked() const noexcept {
        if(production_)return true;
        for(unsigned i=0;i<count_;++i)if(!paths_[i].sameUcd4Sample)return false;
        return true;
    }
    void enableRegisterWrites() noexcept {registerWritesEnabled_=true;}
    void cpuRegisterWrite(const CpuRegisterStaging& staging,bool highByte) noexcept {
        staging_=staging;
        if(production_) {
            production_->cpuRegisterWrite(staging,highByte);
            return;
        }
        if(highByte)for(unsigned i=0;i<count_;++i)paths_[i].writePort.cpuStrobe();
    }
    // Advance complete rising/falling pairs in a certified idle voice tail.
    // CPU bus writes and timer deadlines bound the caller's interval. The
    // ninth slot, uncertain pins, active requests and all real DRAM/DAC
    // transitions remain on the edge engine. This updates physical future
    // state, rather than replaying a cached snapshot from an earlier slot.
    bool productionQuietCandidate() const noexcept {
        return production_ && production_->control==0x4a && production_->phase>=10 &&
            production_->phase<15 && production_->channel<8;
    }
    unsigned advanceQuietClocks(unsigned limit) noexcept {
        // Instrumentation is compiled out when EII_DIAGNOSTICS is 0, which is
        // the Release default; the counters below then stay zero at no cost.
        if constexpr(eii::diagnosticsEnabled) ++quietAttempts;
        if(production_) {
            const auto n=production_->advanceQuietClocks(limit);
            if(n){concreteSteps+=n;quietClocks+=n;lowAperture_=true;syncProduction();}
            return n;
        }
        if(!limit || !normalRuntimeEnabled() || count_!=1 || !portEvents_ ||
           !lowAperture_ || !tables_ || !tables_->quietUpperTail){
            if constexpr(eii::diagnosticsEnabled) ++quietRejectPre;
            return 0;
        }
        auto& p=paths_[0];
        if(!p.voiceActive){
            if constexpr(eii::diagnosticsEnabled) ++quietRejectVoiceInactive;
            return 0;
        }
        if(!p.voice.idle()){
            if constexpr(eii::diagnosticsEnabled) ++quietRejectVoiceNotIdle;
            return 0;
        }
        if(p.phase<10 || p.phase>15){
            if constexpr(eii::diagnosticsEnabled) ++quietRejectVoicePhase;
            return 0;
        }
        if(p.control.known!=65535 || p.control.value!=0x4a){
            if constexpr(eii::diagnosticsEnabled) ++quietRejectVoiceControl;
            return 0;
        }
        auto& b=p.upper.branches[0];
        const auto request=b.cpu.requestToPipeline();
        if(!b.request.known || b.request.high || (b.pipeline.known&1)!=1 || !(b.pipeline.value&1) ||
           !request.known || !request.high || !readInactive_.known ||
           !b.ack.known || b.ack.high || !b.delayed9.known || b.delayed9.high){
            if constexpr(eii::diagnosticsEnabled) ++quietRejectRequest;
            return 0;
        }
        const unsigned n=std::min(limit,16u-p.phase);
        if(!b.dram.advanceQuietClocks(p.control,b.delayed9)){
            if constexpr(eii::diagnosticsEnabled) ++quietRejectDram;
            return 0;
        }
        b.pipeline={std::uint8_t(1|(unsigned(readInactive_.high)<<1)),3};
        b.cpu.completionClock({true,false});p.cpu.completionClock({true,false});
        p.pipeline=b.pipeline;p.writePort.flag=p.writePort.pending;
        p.voiceLastPhase=(p.phase+n-1)&7;p.voiceLastValid=true;
        p.voiceFalling=true;p.voiceOldUcd9=false;
        p.phase+=n;p.shift.value=std::uint8_t(unsigned(p.shift.value)<<n);
        if(p.phase==16) {
            p.phase=8;++p.channel;p.shift.value|=1;
            materializeVoice(p);p.voiceActive=false;
        }
        p.writePort.settle({true,bool(p.channel&8)},{true,bool(p.shift.value&8)});
        risingEdges+=n;promClocks+=n;concreteSteps+=n;voiceTransactionClocks+=n;
        quietClocks+=n;publish();return n;
    }
    std::uint64_t quietClocks=0;
    // Why a certified idle span was not taken. Diagnostic counters only; their
    // increments compile out when EII_DIAGNOSTICS is 0 and they never change
    // scheduling or state.
    std::uint64_t quietAttempts=0,quietRejectPre=0,quietRejectVoice=0,quietRejectRequest=0,quietRejectDram=0;
    std::uint64_t quietRejectVoiceInactive=0,quietRejectVoiceNotIdle=0,quietRejectVoicePhase=0,quietRejectVoiceControl=0;
    // Ninth-slot diagnostic samples: control word and known flag per phase.
    std::array<std::uint16_t,16> diagSlot9Control{};
    std::array<std::uint8_t,16> diagSlot9Known{};
    std::uint64_t diagSlot9Edges=0;
    // Voice-transaction entry diagnostics: low control byte seen at phase8.
    std::uint64_t diagEntryAttempts=0;
    std::array<std::uint64_t,256> diagEntryControl{};
    std::uint64_t diagRisingNoChange=0,diagRisingChanged=0;
    std::uint16_t diagLowValue=0,diagLowKnown=0;
    std::uint8_t diagLowPhase=0,diagLowCaptured=0;
    unsigned diagLowChannel=0;
    // Single-trajectory full-knownness: a whole-controller concrete engine can
    // only represent an edge when every resulting latch/mask is definite.
    std::uint64_t diagBusUnknown=0,diagDataUnknown=0,diagAluUnknown=0,diagUpperUnknown=0;
    // Diagnostics: seed the register file with known contents so a slot test can
    // start from a fully known machine state (the production precondition).
    void seedRegistersForTest(std::uint32_t value) noexcept {
        for(unsigned a=0;a<64;++a)paths_[0].registers.endWrite({std::uint8_t(a),63},{value&0xffffff,0xffffff},{true,true});
    }
#if EII_DIAGNOSTICS
    // Slot-test preconditions: the concrete path assumes a fully known machine
    // state. These setters make each reference latch explicit and known so the
    // differential test compares like with like instead of consensus masks.
    void seedDataPipelineForTest(std::uint32_t low20,std::uint8_t flags,
                                 bool ucd4b,bool ucd9) noexcept {
        auto& d=paths_[0].data;
        d.low20={low20&0xfffff,0xfffff};
        d.flags={std::uint8_t(flags&7),7};
        d.ucd4b={true,ucd4b};d.ucd9={true,ucd9};
        paths_[0].sameUcd4Sample=true;
    }
    void seedAluOutputForTest(std::uint32_t value) noexcept {
        paths_[0].alu.seedForTest({value&0xffffff,0xffffff});
    }
    void seedPipelineForTest(std::uint8_t value) noexcept {
        paths_[0].pipeline={std::uint8_t(value&3),3};
        for(auto& b:paths_[0].upper.branches)b.pipeline={std::uint8_t(value&3),3};
    }
    void seedCapturedForTest(std::uint8_t value,std::uint8_t known=31) noexcept {
        paths_[0].captured={value,known};
    }
    // IC92 may be idle with no pending CPU write; the real handshake can only
    // learn that from a completed aperture, so the slot test sets it directly.
    void seedWritePortForTest(bool pending,bool flag) noexcept {
        auto& w=paths_[0].writePort;
        w.pending={true,pending};w.flag={true,flag};
        w.settle({true,bool(paths_[0].channel&8)},
                 {bool(paths_[0].shift.known&8),bool(paths_[0].shift.value&8)});
    }
    std::uint8_t capturedKnownForTest() const noexcept {return paths_[0].captured.known;}
    LogicLevel aluClockForTest() const noexcept {return paths_[0].alu.clockForTest();}
    LogicLevel writePendingForTest() const noexcept {return paths_[0].writePort.pending;}
    LogicLevel writeFlagForTest() const noexcept {return paths_[0].writePort.flag;}
    LogicLevel writeMpwenForTest() const noexcept {return paths_[0].writePort.mpwenN;}
#endif
    std::uint64_t diagFastCycles=0,diagSlowCycles=0,diagFastEdges=0,diagSlowEdges=0;
    std::uint64_t diagUpperCycles=0,diagUpperCalls=0;
    std::array<std::uint32_t,9> diagState() const noexcept {
        const auto& p=paths_[0];
        // Phase is excluded: it advances on every edge and would make every
        // comparison differ without saying whether PROM-visible state changed.
        return {p.control.value,p.control.known,p.captured.value,p.captured.known,
                p.channel,p.shift.value,p.shift.known,p.pipeline.value,p.pipeline.known};
    }
    std::uint64_t diagEntryFailPre=0,diagEntryFailUpper=0,diagEntryFailControl=0,diagEntryFailShift=0;
    std::uint64_t diagEntryFailAlu=0,diagEntryFailDramCap=0,diagEntryFailUcd4=0,diagEntryFailCompile=0;
    std::uint64_t diagPreControlKnown=0,diagPreShiftKnown=0,diagPrePending=0,diagPreFlag=0,diagPreBranches=0,diagPreOther=0;
    void fallingClock() noexcept {
        if(!registerWritesEnabled_)return;
        lowAperture_=true;
        if(production_){production_->falling();return;}
        for(unsigned i=0;i<count_;++i) {
            auto& p=paths_[i];
            // The preceding source-high phase forced the gated clock high;
            // falling cannot latch. Initial unknown output also stays unknown.
            if(p.voiceActive)p.voiceFalling=true;
            else p.alu.driveClock(logicNot({bool(p.control.known&32),bool(p.control.value&32)}),{});
            if(exactUpper_)p.upper.falling(p.control,dramMemory_);
            else if(dramEnabled_)p.dram.falling(p.control,p.data.ucd9,dramMemory_);
        }
    }
    Known24 aluOutputValue() const noexcept {
        if(production_)return production_->aluOutputValue();
        auto value=voiceBeat(paths_[0])?voiceBeat(paths_[0])->alu:paths_[0].alu.output;
        for(unsigned i=1;i<count_;++i)value=merge24(value,paths_[i].alu.output);
        return value;
    }
    Known24 registerValue(std::uint8_t address) const noexcept {
        if(production_)return production_->registerValue(address);
        auto value=paths_[0].registers.read({address,63});
        for(unsigned i=1;i<count_;++i)value=merge24(value,paths_[i].registers.read({address,63}));
        return value;
    }
    unsigned fullyKnownRegisters() const noexcept {
        if(production_)return production_->fullyKnownRegisters();
        unsigned n=0;for(unsigned a=0;a<64;++a)n+=registerValue(a).known==0xffffff;return n;
    }
    std::array<std::uint64_t,2> certainRegisterWriteRange() const noexcept {
        if(production_)return production_->certainRegisterWriteRange();
        std::uint64_t lo=paths_[0].registerWrites,hi=lo;
        for(unsigned i=1;i<count_;++i){lo=std::min(lo,paths_[i].registerWrites);hi=std::max(hi,paths_[i].registerWrites);}
        return {lo,hi};
    }
    void loadProms(const PromLatch& image,bool upper=true,bool lower=true) {
        auto tables=std::make_shared<ControllerProms>();
        if(upper)tables->upper.load(image.upper);
        if(lower)tables->lower.load(image.lower);
        tables->voices.load(tables->lower);tables->certifyQuietUpperTail();
        tables_=std::move(tables);initializeUpperPaths();
        if(production_)production_->proms=tables_.get();
        for(auto& p:paths_)p.voiceActive=false;
    }
    bool hasUpperProm() const noexcept {return tables_ && tables_->upper.loaded();}
    bool hasLowerProm() const noexcept {return tables_ && tables_->lower.loaded();}
    void driveCpuBus(LogicLevel requestInactive,LogicLevel readInactive) noexcept {
        readInactive_=readInactive;
        if(production_){production_->driveCpuBus(requestInactive,readInactive);return;}
        if(!requestInactive.known || !readInactive.known) {
            materializeVoiceState();for(auto& p:paths_)p.voiceActive=false;
        }
        for(unsigned i=0;i<count_;++i){paths_[i].cpu.drive(requestInactive);if(exactUpper_)paths_[i].upper.driveCpu(requestInactive);}
    }
    // No host or CPU can observe the source edges inside one CPU interval.
    // ChannelTimers reads the live channel/ACK pins below; publish the other
    // diagnostic/CPU-visible aggregates once at the interval boundary.
    void beginCpuInterval() noexcept {deferPublication_=true;}
    void endCpuInterval() noexcept {
        deferPublication_=false;
        if(production_ && blockPublication_) {
            // WAIT is consumed on the next CPU tick. Other public mirrors are
            // observations: hardware/PCM uses live production getters.
            observedWait=production_->observedWait();
        } else if(production_)syncProduction();
        else publish();
    }
    void enableBlockPublication(bool enable=true) noexcept {
        if(production_)syncProduction();
        blockPublication_=enable;
    }
    void materializeProductionObservation() noexcept {if(production_)syncProduction();}
    // Production fast entry: read the eight channel request latches once,
    // straight into the engine levels (same fail(7) policy for any unknown
    // request, same publication/step accounting as productionRising()).
    template<class Channels> void productionRisingFrom(const Channels& channels) noexcept {
        bool known=true;
        for(unsigned i=0;i<8;++i)known&=channels[i].request.known;
        if(!known)production_->fail(7);
        production_->risingRequest(channels[production_->channel&7].request.high);
        syncProduction();
        if(production_->invalid)return;
        ++concreteSteps;lowAperture_=false;
    }
    KnownByte selectedChannel() const noexcept {
        if(production_)return production_->selectedChannel();
        return count_==1?KnownByte{paths_[0].channel,15}:channel;
    }
    LogicLevel selectedAcknowledge() const noexcept {
        if(production_)return production_->selectedAcknowledge();
        return count_==1?paths_[0].acknowledgeEnable:acknowledgeEnable;
    }
    void cpuClock() noexcept {
        if(production_){production_->cpuClock();syncProduction();return;}
        for(unsigned i=0;i<count_;++i){paths_[i].cpu.cpuClock();if(exactUpper_)paths_[i].upper.cpuClock();}
        publish();
    }
    ControllerTiming(std::uint8_t initialPhase=255) noexcept {
        count_=initialPhase<16?1:16;
        for(unsigned i=0;i<count_;++i) paths_[i].phase=count_==1?initialPhase:i;
        publish();
    }
    void reset(bool asserted) noexcept {
        resetHeld_=asserted;
        if(production_){production_->reset(asserted);syncProduction();return;}
        if(asserted){materializeVoiceState();for(auto& p:paths_)p.voiceActive=false;}
        if(asserted) for(unsigned i=0;i<count_;++i) {
            paths_[i].channel=0;paths_[i].captured={0,31};
            if(exactUpper_)paths_[i].upper.reset();
            paths_[i].writePort.settle({true,false},
                {bool(paths_[i].shift.known&8),bool(paths_[i].shift.value&8)});
        }
        // IC111 /CLR and IC110 /CLR are tied high, NOT to CRST.
        publish();
    }
    // flags.value/known bits 0..3 = RFD23,22,21,20. The input latch
    // captures these as PRECAR,LOOPMD,DECFLG,BACKMD. With the register
    // datapath enabled the physical RFD bus supplies flags instead.
    void risingClock(const std::array<LogicLevel,8>& requests,KnownByte flags={}) noexcept {
        if constexpr(eii::diagnosticsEnabled) {
            const auto before=diagState();
            risingClockImpl(requests,flags);
            if(before==diagState())++diagRisingNoChange;else ++diagRisingChanged;
            return;
        }
        risingClockImpl(requests,flags);
    }
    void risingClockImpl(const std::array<LogicLevel,8>& requests,KnownByte flags={}) noexcept {
        if(production_){productionRising(requests);return;}
#if EII_DIAGNOSTICS
        const auto diagT0=__builtin_ia32_rdtsc();
#endif
        ++risingEdges;
        if(tables_)++promClocks;
        if(concreteRuntime_ && count_==1 && exactUpper_ && registerWritesEnabled_ &&
           ((voiceTransactions_ && voiceStep(paths_[0],requests)) || concreteStep(paths_[0],requests))) {
            ++concreteSteps;lowAperture_=false;publish();
#if EII_DIAGNOSTICS
            diagFastCycles+=__builtin_ia32_rdtsc()-diagT0;++diagFastEdges;
#endif
            return;
        }
        for(unsigned i=0;i<count_;++i) {
            auto& p=paths_[i];
            // Diagnostic: sample what the ninth slot actually asks of the PROM
            // per phase, to design its transaction. Compiled out in Release.
            if constexpr(eii::diagnosticsEnabled) {
                if(p.channel>=8 && p.phase<16) {
                    diagSlot9Control[p.phase]=p.control.value;
                    diagSlot9Known[p.phase]=std::uint8_t(p.control.known==65535?1:2);
                    ++diagSlot9Edges;
                }
            }
            KnownByte pathFlags=flags;
            materializeVoice(p);
            p.voiceActive=false;
            const bool needSum=(registerWritesEnabled_&&p.alu.mayCapture()) ||
                (dramEnabled_&&(exactUpper_?p.upper.mayCaptureAddress():p.dram.aluAddress.mayCapture()));
            const auto sum=needSum?controllerAlu(p.data,p.captured,
                {bool(p.control.known&8),bool(p.control.value&8)},
                {bool(p.control.known&16),bool(p.control.value&16)},p.sameUcd4Sample):Known24{};
            if(dramEnabled_&&!exactUpper_)p.dram.rising(p.control,sum,dramMemory_);
            if(registerWritesEnabled_) {
                const LogicLevel ch3{true,bool(p.channel&8)};
                const LogicLevel state3{bool(p.shift.known&8),bool(p.shift.value&8)};
                p.writePort.settle(ch3,state3);
                const auto enabled=registerWriteEnable({true,false},p.writePort.mpwenN,ch3,
                    {true,bool(p.phase&4)},{bool(p.control.known&4),bool(p.control.value&4)});
                const auto address=staging_.selectedAddress({p.channel,15},
                    {std::uint8_t(p.control.value),std::uint8_t(p.control.known)},p.writePort.mpwenN);
                // IC148/150/152, IC141 and gated IC140 see the OLD low-phase
                // bus, before /WE releases and before SRAM/PROM outputs change.
                p.sampledRfd=resolveRfdBus({RfdDriver{p.registers.read(address),logicNot(enabled)},
                    RfdDriver{staging_.data,logicNot(p.writePort.mpwenN)},
                    RfdDriver{p.alu.output,logicNot({bool(p.control.known&64),bool(p.control.value&64)})}});
                pathFlags=inputFlags(p.sampledRfd);
                // Sample bus/write data BEFORE the simultaneously clocked
                // ALU output latch changes; arithmetic sees OLD pipeline.
                p.alu.driveClock({true,true},sum);
                if(lowAperture_) {
                    p.registers.endWrite(address,p.sampledRfd,enabled);
                    if(enabled.known && enabled.high)++p.registerWrites;
                }
                // IC92 samples before the counter/shift outputs change.
                p.writePort.sourceClock();
            }
            const bool carry=p.phase==15;
            // IC136/138 and pipeline IC152 sample simultaneously from OLD
            // address feedback, counter, IC140 capture and request pipeline.
            KnownWord nextControl{};
            // IC152 also pipelines OLD UCD08.1 to UCD08.2. The output
            // board ACK decoder must not consume the new PROM latch early.
            const LogicLevel nextAck{bool(p.control.known&0x100),bool(p.control.value&0x100)};
            if(tables_) {
                const unsigned highValue=((p.control.value>>7)&0x180)|((p.phase&7)<<4)|
                    ((p.pipeline.value&1)<<3)|((p.pipeline.value&2)<<1)|
                    ((p.channel&8)?2:0)|(p.captured.value&1);
                const unsigned highKnown=((p.control.known>>7)&0x180)|0x72|
                    ((p.pipeline.known&1)<<3)|((p.pipeline.known&2)<<1)|(p.captured.known&1);
                const unsigned lowValue=((p.channel&8)?256:0)|((p.phase&7)<<5)|
                    ((p.captured.value&1)<<4)|((p.captured.value&2)<<2)|
                    (p.captured.value&4)|((p.captured.value&8)>>2)|((p.captured.value&16)>>4);
                const unsigned lowKnown=0x1e0|((p.captured.known&1)<<4)|
                    ((p.captured.known&2)<<2)|(p.captured.known&4)|
                    ((p.captured.known&8)>>2)|((p.captured.known&16)>>4);
                if constexpr(eii::diagnosticsEnabled) {
                    diagLowValue=std::uint16_t(lowValue);diagLowKnown=std::uint16_t(lowKnown);
                    diagLowPhase=p.phase;diagLowCaptured=p.captured.value;diagLowChannel=p.channel;
                }
                const auto high=exactUpper_?KnownByte{}:tables_->upper.lookup(highValue,highKnown);
                const auto low=tables_->lower.lookup(lowValue,lowKnown);
                nextControl={std::uint16_t((unsigned(high.value)<<8)|low.value),
                             std::uint16_t((unsigned(high.known)<<8)|low.known)};
            }
            if(exactUpper_) {
                const auto req=requests[p.channel&7];
                p.upper.rising(tables_->upper,p.phase,p.channel,
                    {bool(p.shift.known&1),bool(p.shift.value&1)},req,resetHeld_,readInactive_,p.control,sum,dramMemory_);
                const auto high=p.upper.upper();
                nextControl.value=(nextControl.value&255)|(unsigned(high.value)<<8);
                nextControl.known=(nextControl.known&255)|(unsigned(high.known)<<8);
            }
            // IC152 receives raw +UCD04.D from IC137, not old UCD04.1A.
            // Its .1B output and IC138 .1A sample the same PROM output.
            if(registerWritesEnabled_) {
                p.data.clock(p.sampledRfd,
                {bool(nextControl.known&16),bool(nextControl.value&16)},
                {bool(p.control.known&512),bool(p.control.value&512)});
                p.sameUcd4Sample=true;
            }
            const auto mpRequest=p.cpu.requestToPipeline();
            const KnownByte nextPipeline{std::uint8_t(unsigned(mpRequest.high)|(unsigned(readInactive_.high)<<1)),
                                         std::uint8_t(unsigned(mpRequest.known)|(unsigned(readInactive_.known)<<1))};
            const auto req=requests[p.channel&7];
            KnownByte input{std::uint8_t((pathFlags.value&15)<<1 | unsigned(req.high)),
                            std::uint8_t((pathFlags.known&15)<<1 | unsigned(req.known))};
            if(resetHeld_) p.captured={0,31};
            else if(p.shift.known&1) {
                // NAND(STATE0,!CLK) rises at CLK rising while the OLD
                // STATE0 is high. Sample before counters/shift update.
                if(p.shift.value&1) p.captured=input;
            } else p.captured=merge(p.captured,input); // gated clock uncertain
            if(resetHeld_) p.channel=0;
            else if(carry) p.channel=(p.channel&8)?0:p.channel+1;
            p.pipeline=exactUpper_?p.upper.pipeline():nextPipeline;
            if(exactUpper_) {const auto req=p.upper.request();p.captured.value=(p.captured.value&~1)|unsigned(req.high);p.captured.known=(p.captured.known&~1)|unsigned(req.known);}
            p.acknowledgeEnable=exactUpper_?p.upper.ack():nextAck;p.control=nextControl;
            if(dramEnabled_&&!exactUpper_)p.dram.settle(p.control,dramMemory_);
            p.cpu.completionClock({bool(p.control.known&0x200),bool(p.control.value&0x200)});
            p.phase=carry?8:p.phase+1; // /LD = !RCO, parallel data = 1000
            p.shift.value=std::uint8_t((p.shift.value<<1)|unsigned(carry));
            p.shift.known=std::uint8_t((p.shift.known<<1)|1);
            if(registerWritesEnabled_)p.writePort.settle({true,bool(p.channel&8)},
                {bool(p.shift.known&8),bool(p.shift.value&8)});
            if constexpr(eii::diagnosticsEnabled) {
                if(count_==1) {
                    if(p.sampledRfd.known!=0xffffff) ++diagBusUnknown;
                    if((p.data.low20.known&0xfffff)!=0xfffff || (p.data.flags.known&7)!=7 ||
                       !p.data.ucd4b.known || !p.data.ucd9.known) ++diagDataUnknown;
                    if((p.alu.output.known&0xffffff)!=0xffffff) ++diagAluUnknown;
                    bool full=true;
                    for(const auto& b:p.upper.branches)
                        full&=b.upper.known==255 && (b.pipeline.known&3)==3 &&
                              b.request.known && b.ack.known && b.delayed9.known;
                    if(!full) ++diagUpperUnknown;
                }
            }
        }
        lowAperture_=false;
        publish();
#if EII_DIAGNOSTICS
        diagSlowCycles+=__builtin_ia32_rdtsc()-diagT0;++diagSlowEdges;
#endif
    }
#if EII_DIAGNOSTICS
    KnownWord controlForTest() const noexcept {return paths_[0].control;}
    std::uint8_t shiftForTest() const noexcept {return paths_[0].shift.value;}
    std::uint8_t capturedForTest() const noexcept {return paths_[0].captured.value;}
    std::uint8_t pipelineForTest() const noexcept {return paths_[0].pipeline.value;}
    std::uint8_t phaseForTest() const noexcept {return paths_[0].phase;}
    KnownByte channelForTest() const noexcept {return KnownByte{paths_[0].channel,15};}
    Known24 registerForTest(std::uint8_t a) const noexcept {return paths_[0].registers.read({a,63});}
    bool requestForTest() const noexcept {return paths_[0].upper.branches[0].request.high;}
    const ControllerProms& tablesForTest() const noexcept {return *tables_;}
    bool upperLatchForTest() const noexcept {return paths_[0].upper.branches[0].ack.high;}
#endif
    unsigned trajectoryCount() const noexcept {return production_?1u:count_;}
    unsigned upperBranchCount() const noexcept {
        if(production_)return 1;
        if(!exactUpper_)return 0;
        unsigned total=0;for(unsigned i=0;i<count_;++i)total+=paths_[i].upper.branches.size();return total;
    }
private:
    struct Path {
        std::uint8_t phase=0,channel=0;
        KnownByte shift{},captured{0,31},pipeline{};
        KnownWord control{};
        CpuRequestHandshake cpu;
        LogicLevel acknowledgeEnable{};
        RegisterWriteHandshake writePort;
        ControllerRegisterFile registers;
        ControllerDataPipeline data;
        bool sameUcd4Sample=false; // No relation between unclocked power-up latches.
        Known24 sampledRfd{};
        ControllerAluLatch alu;
        DramCpuPort dram;
        ControllerUpperPaths upper;
        VoiceTransaction voice;
        bool voiceActive=false,voiceLastValid=false,voiceFalling=false,voiceOldUcd9=false;
        unsigned voiceLastPhase=0;
        std::uint64_t registerWrites=0;
    };
    std::array<Path,16> paths_{};
    bool concreteRuntime_=false;
    bool voiceTransactions_=false;
    static const VoiceTransaction::Beat* voiceBeat(const Path& p) noexcept {
        return p.voiceActive && p.voiceLastValid?&p.voice.beat(p.voiceLastPhase):nullptr;
    }
    static void materializeVoice(Path& p) noexcept {
        if(const auto* beat=voiceBeat(p)) {
            p.sampledRfd=beat->rfd;p.data=beat->data;p.data.ucd9={true,p.voiceOldUcd9};
            p.alu.materialize(beat->alu,{true,!p.voiceFalling || !(p.control.value&32)});
        }
    }
    // One physical controller trajectory. SRAM words may still contain
    // unknown bits; those masks pass through the existing arithmetic and bus
    // resolver. Only the control/handshake pins must be definite. No histories
    // are discarded and a partial control word returns to the reference path.
    bool voiceStep(Path& p,const std::array<LogicLevel,8>& requests) noexcept {
        const auto cancel=[&](){materializeVoice(p);p.voiceActive=false;return false;};
        if(!lowAperture_)return cancel();
        // Eligibility is proved once. During a voice slot only known CPU
        // inputs can change the upper sequencer; request capture occurs at
        // entry only. Unknown CPU pins/reset/PROM changes cancel at their API
        // boundaries, and the ninth slot never enters this transaction.
        if(!p.voiceActive) {
            if(p.phase!=8 || resetHeld_ || p.channel>=8 || p.control.known!=65535 || p.shift.known!=255 ||
               !p.writePort.pending.known || !p.writePort.flag.known || p.upper.branches.size()!=1){
                if constexpr(eii::diagnosticsEnabled) {
                    if(p.phase==8 && !resetHeld_ && p.channel<8) {
                        if(p.control.known!=65535) ++diagPreControlKnown;
                        else if(p.shift.known!=255) ++diagPreShiftKnown;
                        else if(!p.writePort.pending.known) ++diagPrePending;
                        else if(!p.writePort.flag.known) ++diagPreFlag;
                        else if(p.upper.branches.size()!=1) ++diagPreBranches;
                        else ++diagPreOther;
                    }
                }
                return false;
            }
            const auto& b=p.upper.branches[0];
            if((b.pipeline.known&3)!=3 || !b.request.known || !b.cpu.done.known || !b.cpu.completionKnown() ||
               !b.cpu.requestInactive.known || !readInactive_.known){
                if constexpr(eii::diagnosticsEnabled) ++diagEntryFailUpper;
                return false;
            }
        }
        auto& b=p.upper.branches[0];
        const unsigned old=p.control.value;
        if(!p.voiceActive) {
            // Diagnostic: which low control byte the slot presents at entry.
            if constexpr(eii::diagnosticsEnabled)
                if(p.phase==8) {++diagEntryAttempts;++diagEntryControl[old&255];}
            p.voiceActive=false;p.voiceLastValid=false;
            if((old&255)!=0x4a || p.shift.value!=1 || p.alu.mayCapture() ||
               b.dram.aluAddress.mayCapture() || !p.sameUcd4Sample){
                if constexpr(eii::diagnosticsEnabled) {
                    if((old&255)!=0x4a) ++diagEntryFailControl;
                    else if(p.shift.value!=1) ++diagEntryFailShift;
                    else if(p.alu.mayCapture()) ++diagEntryFailAlu;
                    else if(b.dram.aluAddress.mayCapture()) ++diagEntryFailDramCap;
                    else ++diagEntryFailUcd4;
                }
                return false;
            }
            std::array<Known24,4> rf;
            for(unsigned slot=0;slot<4;++slot)rf[slot]=p.registers.read({std::uint8_t(p.channel*4+slot),63});
            if(!p.voice.compile(tables_->voices,rf,p.alu.output,requests[p.channel&7],0)){
                if constexpr(eii::diagnosticsEnabled) ++diagEntryFailCompile;
                return false;
            }
            p.voiceActive=true;++voiceTransactions;
        }
        if(!p.voiceActive)return false;
        const auto& beat=p.voice.beat(p.phase&7);
        ++voiceTransactionClocks;
        p.writePort.mpwenN={true,true};
        if(old&4) {
            p.registers.endWrite({std::uint8_t(p.channel*4+(old&3)),63},beat.rfd,{true,true});
            ++p.registerWrites;
        }
        p.writePort.flag=p.writePort.pending;
        const auto req=requests[p.channel&7];
        p.upper.rising(tables_->upper,p.phase,p.channel,{true,bool(p.shift.value&1)},
                       req,resetHeld_,readInactive_,p.control,beat.sum,dramMemory_);
        const auto& next=p.upper.branches[0];
        p.voiceLastValid=true;p.voiceLastPhase=p.phase&7;p.voiceFalling=false;p.voiceOldUcd9=bool(old&512);
        p.sameUcd4Sample=true;
        if(p.shift.value&1)p.captured=beat.captured;
        const bool carry=p.phase==15;
        if(carry)++p.channel;
        p.pipeline=next.pipeline;
        p.captured.value=(p.captured.value&~1)|unsigned(next.request.high);
        p.captured.known=(p.captured.known&~1)|unsigned(next.request.known);
        p.acknowledgeEnable=next.ack;
        p.control={std::uint16_t((unsigned(next.upper.value)<<8)|beat.nextControl.value),
                   std::uint16_t((unsigned(next.upper.known)<<8)|255)};
        p.cpu.completionClock({bool(p.control.known&512),bool(p.control.value&512)});
        if(carry){materializeVoice(p);p.voiceActive=false;}
        p.phase=carry?8:p.phase+1;p.shift.value=std::uint8_t((p.shift.value<<1)|unsigned(carry));
        p.writePort.settle({true,bool(p.channel&8)},{true,bool(p.shift.value&8)});
        return true;
    }
    bool concreteStep(Path& p,const std::array<LogicLevel,8>& requests) noexcept {
        if(p.control.known!=65535 || p.shift.known!=255 ||
           !p.writePort.pending.known || !p.writePort.flag.known ||
           p.upper.branches.size()!=1)return false;
        auto& b=p.upper.branches[0];
        if((b.pipeline.known&3)!=3 || !b.request.known)return false;
        const unsigned old=p.control.value;
        const bool ch3=p.channel&8;
        const bool mpwen=!(ch3 && (p.shift.value&8) && p.writePort.flag.high);
        p.writePort.mpwenN={true,mpwen};
        if(!mpwen)p.writePort.pending={true,false};
        const bool write=(old&4) && (!mpwen || !ch3 || (p.phase&4));
        const KnownByte address=mpwen?KnownByte{std::uint8_t((p.channel<<2)|(old&3)),63}:staging_.address;
        const bool needSum=p.alu.mayCapture() || b.dram.aluAddress.mayCapture();
        const auto sum=needSum?controllerAlu(p.data,p.captured,{true,bool(old&8)},
                                           {true,bool(old&16)},p.sameUcd4Sample):Known24{};
        // Execute enabled bus drivers, not the three-state gate network.
        Known24 rfd{};bool driven=false;
        if(!write){rfd=p.registers.read(address);driven=true;}
        if(!mpwen){rfd=driven?merge24(rfd,staging_.data):staging_.data;driven=true;}
        if(!(old&64)){rfd=driven?merge24(rfd,p.alu.output):p.alu.output;driven=true;}
        rfd.value&=0xffffff;rfd.known=driven?(rfd.known&0xffffff):0;
        p.sampledRfd=rfd;
        const auto flags=inputFlags(rfd);
        p.alu.driveClock({true,true},sum);
        if(lowAperture_ && write){p.registers.endWrite(address,rfd,{true,true});++p.registerWrites;}
        p.writePort.flag=p.writePort.pending;
        const unsigned lowValue=(ch3?256:0)|((p.phase&7)<<5)|
            ((p.captured.value&1)<<4)|((p.captured.value&2)<<2)|
            (p.captured.value&4)|((p.captured.value&8)>>2)|((p.captured.value&16)>>4);
        const unsigned lowKnown=0x1e0|((p.captured.known&1)<<4)|
            ((p.captured.known&2)<<2)|(p.captured.known&4)|
            ((p.captured.known&8)>>2)|((p.captured.known&16)>>4);
        const auto low=tables_->lower.lookup(lowValue,lowKnown);
        const auto req=requests[p.channel&7];
#if EII_DIAGNOSTICS
        const auto diagTU=__builtin_ia32_rdtsc();
#endif
        p.upper.rising(tables_->upper,p.phase,p.channel,{true,bool(p.shift.value&1)},
                       req,resetHeld_,readInactive_,p.control,sum,dramMemory_);
#if EII_DIAGNOSTICS
        diagUpperCycles+=__builtin_ia32_rdtsc()-diagTU;++diagUpperCalls;
#endif
        // rising() may make a request unknown; that remains a fallback input
        // on the next clock, rather than choosing one possible request.
        const auto& next=p.upper.branches[0];
        p.data.clock(rfd,{bool(low.known&16),bool(low.value&16)},{true,bool(old&512)});
        p.sameUcd4Sample=true;
        if(resetHeld_)p.captured={0,31};
        else if(p.shift.value&1)p.captured={std::uint8_t((flags.value&15)<<1|unsigned(req.high)),
                                          std::uint8_t((flags.known&15)<<1|unsigned(req.known))};
        const bool carry=p.phase==15;
        p.channel=resetHeld_?0:carry?(ch3?0:p.channel+1):p.channel;
        p.pipeline=next.pipeline;
        p.captured.value=(p.captured.value&~1)|unsigned(next.request.high);
        p.captured.known=(p.captured.known&~1)|unsigned(next.request.known);
        p.acknowledgeEnable=next.ack;
        p.control={std::uint16_t((unsigned(next.upper.value)<<8)|low.value),
                   std::uint16_t((unsigned(next.upper.known)<<8)|low.known)};
        p.cpu.completionClock({bool(p.control.known&512),bool(p.control.value&512)});
        p.phase=carry?8:p.phase+1;p.shift.value=std::uint8_t((p.shift.value<<1)|unsigned(carry));
        p.writePort.settle({true,bool(p.channel&8)},{true,bool(p.shift.value&8)});
        return true;
    }
    unsigned count_=16;
    bool exactUpper_=false,portEvents_=false;
    std::unique_ptr<production::Controller> production_;
    bool productionWanted_=false;
    void initializeProductionEngine(std::uint32_t seed) {
        auto engine=std::make_unique<production::Controller>();
        engine->proms=tables_.get();
        engine->attachCpuDram(dramMemory_.bytes?dramMemory_.bytes->data():nullptr,
                              dramMemory_.known?dramMemory_.known->data():nullptr,
                              dramMemory_.underlay,dramMemory_.underlayKnown,dramMemory_.underlaySize);
        engine->phase=paths_[0].phase;
        CpuRegisterStaging staging;
        engine->initializePowerOn(seed,staging);
        production_=std::move(engine);
    }
    void productionRising(const std::array<LogicLevel,8>& requests) noexcept {
        std::array<bool,8> levels{};
        for(unsigned i=0;i<8;++i) {
            if(!requests[i].known)production_->fail(7);
            levels[i]=requests[i].high;
        }
        production_->rising(levels,0);
        if(production_->invalid) {
            // Keep the observation surface explicit rather than choosing values.
            syncProduction();
            return;
        }
        syncProduction();
        ++concreteSteps;
        lowAperture_=false;
    }
    // Copy the live concrete state into path0 only at observation boundaries.
    // The hot path never iterates16 trajectories or carries consensus masks.
    void syncProduction() noexcept {
        if(deferPublication_)return;
#if EII_DIAGNOSTICS
        auto& p=paths_[0];
        const auto& e=*production_;
        p.phase=e.phase;p.channel=e.channel;
        p.shift={e.shift,255};p.captured={e.captured,31};
        p.pipeline={e.pipeline,3};p.control={e.control,65535};
        p.acknowledgeEnable=e.selectedAcknowledge();
#else
        const auto& e=*production_;
#endif
        risingEdges=e.risingEdges;promClocks=e.promClocks;
        diagLowValue=e.lastLowerWord;diagLowKnown=0x1ff;diagLowPhase=e.phase;
        diagLowCaptured=e.captured;diagLowChannel=e.channel;
        publish();
    }
    const DramCpuPort& dramPort(unsigned i) const noexcept {return exactUpper_?paths_[i].upper.branches[0].dram:paths_[i].dram;}
    void initializeUpperPaths() {
        materializeVoiceState();
        for(auto& p:paths_)p.voiceActive=false;
        exactUpper_=dramEnabled_&&hasUpperProm();
        if(exactUpper_)for(unsigned i=0;i<count_;++i){auto& p=paths_[i];p.upper.initialize(p.control,p.pipeline,
            {bool(p.captured.known&1),bool(p.captured.value&1)},p.cpu,p.dram);
            for(auto& b:p.upper.branches)b.dram.enableEventTiming(portEvents_);
        }
    }
    LogicLevel pathWait(unsigned i) const noexcept {return exactUpper_?paths_[i].upper.wait():paths_[i].cpu.waitInactive();}
    bool resetHeld_=true;
    bool registerWritesEnabled_=false,lowAperture_=false;
    bool dramEnabled_=false,dramActive_=false,dramRead_=true;
    DramBackingView dramMemory_;
    CpuRegisterStaging staging_;
    LogicLevel readInactive_{true,true};
    std::shared_ptr<const ControllerProms> tables_;
    static KnownByte merge(KnownByte a,KnownByte b) noexcept {
        return {a.value,std::uint8_t(a.known & b.known & ~(a.value^b.value))};
    }
    bool deferPublication_=false,blockPublication_=false;
    void publish() noexcept {
        if(deferPublication_)return;
        if(production_) {
            const auto& e=*production_;
            channel={e.channel,15};microCount={e.phase,15};
            shift={e.shift,255};captured={e.captured,31};requestPipeline={e.pipeline,3};
            control={e.control,65535};observedWait=e.observedWait();
            acknowledgeEnable=e.selectedAcknowledge();
            return;
        }
        channel={paths_[0].channel,15};microCount={paths_[0].phase,15};
        shift=paths_[0].shift;captured=paths_[0].captured;requestPipeline=paths_[0].pipeline;
        control=paths_[0].control;observedWait=pathWait(0);
        acknowledgeEnable=paths_[0].acknowledgeEnable;
        for(unsigned i=1;i<count_;++i) {
            channel=merge(channel,{paths_[i].channel,15});
            microCount=merge(microCount,{paths_[i].phase,15});
            shift=merge(shift,paths_[i].shift);captured=merge(captured,paths_[i].captured);
            requestPipeline=merge(requestPipeline,paths_[i].pipeline);
            const auto next=paths_[i].control;
            control.known&=std::uint16_t(next.known & ~(control.value^next.value));
            const auto ack=paths_[i].acknowledgeEnable;
            acknowledgeEnable.known=acknowledgeEnable.known && ack.known && acknowledgeEnable.high==ack.high;
            const auto wait=pathWait(i);
            observedWait.known=observedWait.known && wait.known && observedWait.high==wait.high;
        }
    }
};
}

