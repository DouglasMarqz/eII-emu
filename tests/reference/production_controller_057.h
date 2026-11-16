#pragma once
// Concrete (production) controller assembly.
//
// Wires the proven concrete components in the exact event order of
// ControllerTiming::risingClockImpl / fallingClock for the fully known path.
// Validated by tests/production_controller_tests.cpp over complete slots from a
// fully known, single-known-driver state. The emulator does not select this
// class by default; ControllerTiming remains the oracle and verification
// default. The GUI concrete startup selects the production controller.
#include <array>
#include <cstdint>
#include <memory>
#include "controller_prom.h"
#include "controller_registers.h"
#include "digital_power_on.h"
#include "prom.h"
#include "production_alu.h"
#include "production_alu_latch.h"
#include "production_bus.h"
#include "production_dac.h"
#include "production_upper.h"
#include "production_registers.h"
#include "production_write.h"

namespace eii::production_controller_reference_057 {
using namespace eii::production;
class Controller {
public:
    const ControllerProms* proms=nullptr;
    std::shared_ptr<const ControllerProms> ownedProms;
    const std::uint8_t* memory=nullptr;
    const std::uint8_t* known=nullptr;

    std::uint8_t phase=8,channel=0,shift=0,captured=0,pipeline=0;
    std::uint16_t control=0;
    bool acknowledgeEnable=false,lowAperture=false,readInactive=true,requestInactive=true;
    bool resetHeld=false,dramActive=false,dramRead=true;
    // A concrete engine has no representation for an unknown level/mask, a
    // floating RFD bus, contention or unknown backing memory. Any of those sets
    // invalid; the owner must not treat the following state as authoritative.
    bool invalid=false;
    // First reason the engine left the fully known set (diagnostic only).
    // 0=none,1=unknown CPU pin,2=unknown staging,3=unknown DRAM data,
    // 4=unknown backing byte,5=floating RFD bus,6=RFD contention,
    // 7=unknown request,8=unknown CHDB at DAC capture,9=power-on UCD04 ambiguity.
    unsigned invalidCode=0;
    struct Failure {std::uint64_t edge=0;std::uint16_t control=0;std::uint8_t phase=0,channel=0;};
    Failure firstFailure;
    void fail(unsigned code) noexcept {
        invalid=true;
        if(!invalidCode){invalidCode=code;firstFailure={risingEdges,control,phase,channel};}
    }
    std::uint64_t risingEdges=0,promClocks=0,registerWrites=0;
    std::uint32_t lastRfd=0;
    std::uint16_t lastLowerWord=0;  // diagnostics: lower PROM address
    RegisterFile registers;
    Staging staging;
    WriteHandshake writePort;
    AluLatch aluOutput;
    DataPipeline data;
    UpperPath upper;

    bool valid() const noexcept {return !invalid;}
    void loadProms(const PromLatch& image,bool upperPart=true,bool lowerPart=true) {
        auto tables=std::make_shared<ControllerProms>();
        if(upperPart)tables->upper.load(image.upper);
        if(lowerPart)tables->lower.load(image.lower);
        ownedProms=std::move(tables);proms=ownedProms.get();
    }
    void attachCpuDram(const std::uint8_t* bytes,const std::uint8_t* knownMask,
                       const std::uint8_t* underlay=nullptr,const std::uint8_t* underlayKnown=nullptr,
                       std::uint32_t underlaySize=0) noexcept {
        memory=bytes;known=knownMask;upper.dram.memoryKnown=knownMask;
        upper.dram.underlay=underlay;upper.dram.underlayKnown=underlayKnown;
        upper.dram.underlaySize=underlaySize;
    }
    // Same seed consumption and derived levels as
    // ControllerTiming::initializePowerOn for path0, so a concrete engine can
    // start from the same reproducible pre-first-clock realization.
    void initializePowerOn(std::uint32_t seed,CpuRegisterStaging& stagingOut) {
        DigitalPowerOnBits bits(seed);
        staging=Staging{};
        staging.address=std::uint8_t(bits.next()&63);
        staging.data=bits.next()&0xffffff;
        stagingOut=CpuRegisterStaging{};
        stagingOut.address={staging.address,63};
        stagingOut.data={staging.data,0xffffff};
        shift=std::uint8_t(bits.next());
        control=std::uint16_t(bits.next());
        pipeline=std::uint8_t(bits.next()&3);
        for(unsigned a=0;a<64;++a)registers.write(std::uint8_t(a),bits.next()&0xffffff);
        data.low20=bits.next()&0xfffff;
        data.flags=std::uint8_t(bits.next()&7);
        data.ucd4b=(bits.next()&1)!=0;
        data.ucd9=(bits.next()&1)!=0;
        aluOutput.output=bits.next()&0xffffff;
        aluOutput.clock=(control&32)==0;
        (void)bits.next(); // reference sampledRfd latch
        upper.cpu.initializePowerOn(bits,{true,(control&512)!=0});
        writePort.pending=(bits.next()&1)!=0;
        writePort.flag=(bits.next()&1)!=0;
        writePort.settle((channel&8)!=0,(shift&8)!=0);
        const bool ack=(bits.next()&1)!=0,delayed9=(bits.next()&1)!=0;
        upper.dram.initializePowerOn(bits,control,delayed9);
        upper.upper=std::uint8_t(control>>8);
        upper.pipeline=pipeline;
        upper.request=(captured&1)!=0;
        upper.ack=ack;upper.delayed9=delayed9;
        upper.dac.initializePowerOn(bits,channel,ack);
        acknowledgeEnable=ack;
        // Before the first joint UCD04 sample the two latches may differ; the
        // reference then enumerates both addends. If UCD05 does not clock the
        // ALU latch on the first edge, the ambiguous sum is never captured and
        // the subsequent joint sample links the two values. A concrete engine
        // is only representable when no ambiguous sum can be captured.
        invalid=((control&32)!=0) && (data.ucd4b!=((control&16)!=0));
        invalidCode=0;firstFailure={};if(invalid)fail(9);
        resetHeld=false;lastRfd=0;registerWrites=0;risingEdges=0;promClocks=0;
    }
    void reset(bool asserted) noexcept {
        resetHeld=asserted;
        if(asserted) {
            channel=0;captured=0;
            upper.reset();
            writePort.settle(false,(shift&8)!=0);
        }
    }
    void driveCpuBus(LogicLevel reqInactive,LogicLevel readLevel) noexcept {
        if(!reqInactive.known||!readLevel.known){fail(1);return;}
        requestInactive=reqInactive.high;
        readInactive=readLevel.high;
        upper.driveCpu(requestInactive);
    }
    void cpuClock() noexcept {upper.cpuClock();}
    void cpuRegisterWrite(const CpuRegisterStaging& value,bool highByte) noexcept {
        if(value.address.known!=63 || value.data.known!=0xffffff)fail(2);
        staging.address=value.address.value;
        staging.data=value.data.value&0xffffff;
        if(highByte)writePort.cpuStrobe();
    }
    void driveDramCpu(bool active,std::uint32_t address,KnownByte value,bool read) noexcept {
        dramActive=active;dramRead=read;
        if(value.known!=255){fail(3);return;}
        upper.driveDram(active,address,value.value,read);
    }
    bool cpuDramReady() const noexcept {
        if(!dramActive)return true;
        return dramRead?upper.dram.readReady:upper.dram.writeReady;
    }
    KnownByte cpuDramReadData() const noexcept {
        return {upper.dram.readLatch,std::uint8_t(upper.dram.readKnown?255:0)};
    }
    bool cpuDramWrite(std::uint32_t& address,KnownByte& value) const noexcept {
        if(!dramActive||dramRead||!cpuDramReady())return false;
        address=upper.dram.writeAddress;
        value={upper.dram.writeData,255};
        return true;
    }
    LogicLevel observedWait() const noexcept {return {true,upper.cpu.waitInactive()};}
    KnownByte selectedChannel() const noexcept {return {channel,15};}
    LogicLevel selectedAcknowledge() const noexcept {return {true,upper.ack};}
    KnownByte channelBusValue() const noexcept {
        return {upper.dram.channelLatch,std::uint8_t(upper.dram.channelKnown?255:0)};
    }
    KnownByte channelDacCode(unsigned ch) const noexcept {return upper.dac.code(ch);}
    Known24 dataPipelineValue() const noexcept {return {data.low20,0xfffff};}
    Known24 sampledRfdValue() const noexcept {return {lastRfd,0xffffff};}
    KnownByte delayedFlagValue() const noexcept {return {data.flags,7};}
    LogicLevel delayedUcd4() const noexcept {return {true,data.ucd4b};}
    Known24 aluOutputValue() const noexcept {return {aluOutput.output,0xffffff};}
    Known24 registerValue(std::uint8_t address) const noexcept {
        return {registers.read(address),0xffffff};
    }
    unsigned fullyKnownRegisters() const noexcept {return 64;}
    std::array<std::uint64_t,2> certainRegisterWriteRange() const noexcept {
        return {registerWrites,registerWrites};
    }
    bool hasUpperProm() const noexcept {return proms && proms->upper.loaded();}
    bool hasLowerProm() const noexcept {return proms && proms->lower.loaded();}
    unsigned trajectoryCount() const noexcept {return 1;}
    unsigned upperBranchCount() const noexcept {return 1;}

    void rising(const std::array<bool,8>& requests,std::uint8_t inputFlagBits) {
        if(invalid)return;
        ++risingEdges;
        if(proms)++promClocks;
        const bool ch3=(channel&8)!=0;
        const unsigned ucd3=control&8,ucd4a=control&16;
        // ALU sum: RFD19..0 + A + CI with the carry into bit23. UCD04.1A/B are
        // the two latches of the same raw PROM bit (IC138 IC152 sample it
        // together), so the known path uses the delayed pipeline value for
        // both, exactly like controllerAlu() with sameUcd4Sample.
        const bool ucd4b=data.ucd4b;
        const std::uint32_t sum=eii::production::aluOutput(data.low20,ucd3!=0,ucd4a!=0,ucd4b,
                                                           data.flags,captured);
        // IC92 write handshake and IC115 write aperture.
        writePort.settle(ch3,(shift&8)!=0);
        const bool enabled=writeEnable(false,writePort.mpwenN,ch3,(phase&4)!=0,(control&4)!=0);
        const std::uint8_t address=staging.selectedAddress(channel,control,writePort.mpwenN);
        // RFD bus: the three possible drivers. The ALU side is the IC100..102
        // output latch (p.alu.output), not the RFD data pipeline. The CPU
        // latches drive when -MPWEN is LOW, hence !mpwenN.
        const std::uint32_t busData[3]={registers.read(address),staging.data,aluOutput.output};
        const bool busEnabled[3]={!enabled,!writePort.mpwenN,(control&64)==0};
        unsigned active=0;std::uint32_t activeValue=0;bool disagreement=false;
        for(unsigned i=0;i<3;++i)if(busEnabled[i]) {
            if(!active)activeValue=busData[i];
            else if((activeValue^busData[i])&0xffffff)disagreement=true;
            ++active;
        }
        bool any=false;
        const std::uint32_t rfd=resolveBus(busData,busEnabled,any);
        // A floating bus or disagreeing drivers have no single known value.
        if(!any){fail(5);return;}
        if(disagreement){fail(6);return;}
        lastRfd=rfd&0xffffff;
        const std::uint8_t capturedFlags=flagsFromBus(rfd);
        aluOutput.driveClock(true,sum);
        if(lowAperture&&enabled) {
            registers.write(address,rfd);
            ++registerWrites;
        }
        writePort.sourceClock();
        const bool carry=phase==15;
        // Upper sequencer.
        const std::uint16_t lowerWord=std::uint16_t((channel&8)?256:0)|
            std::uint16_t((phase&7)<<5)|std::uint16_t(((captured&1)<<4)|((captured&2)<<2)|
            (captured&4)|((captured&8)>>2)|((captured&16)>>4));
        upper.rising(proms->upper,phase,channel,(shift&1)!=0,requests[channel&7],resetHeld,
                     readInactive,control,sum,memory);
        if(upper.dram.memoryFault){fail(4);return;}
        // Lower PROM lookup and the combined control word.
        lastLowerWord=lowerWord;
        // The reference masks with the captured KNOWN bits, which are all five
        // in this path, so the mask is always the full 0x1ff.
        const unsigned lowKnown=0x1e0u|0x1fu;
        const std::uint8_t lowByte=proms?proms->lower.lookup(lowerWord,lowKnown).value:0;
        const std::uint8_t highByte=upper.upper;
        const std::uint16_t nextControl=std::uint16_t((unsigned(highByte)<<8)|lowByte);
        // Data pipeline: IC141/148/150/152 latch the settled bus.
        data.clock(rfd,(nextControl&16)!=0,(control&512)!=0);
        // Captured request/flags, channel carry and phase advance.
        const std::uint8_t inputByte=std::uint8_t(((capturedFlags&15)<<1)|unsigned(requests[channel&7]));
        if(resetHeld)captured=0;
        else if(shift&1)captured=inputByte;
        // IC152 pipeline: OLD CPU request/read level, set before the completion
        // clock in upper.rising (the reference reads upper.pipeline()).
        pipeline=upper.pipeline;
        acknowledgeEnable=upper.ack;
        control=nextControl;
        if(resetHeld)channel=0;
        else if(carry)channel=(channel&8)?0:std::uint8_t(channel+1);
        phase=carry?8:std::uint8_t(phase+1);
        shift=std::uint8_t((shift<<1)|unsigned(carry));
        writePort.settle((channel&8)!=0,(shift&8)!=0);
        lowAperture=false;
        (void)inputFlagBits;
    }
    void falling() {
        if(invalid)return;
        lowAperture=true;
        // IC99 gated clock: NAND(UCD05.1,-C10MHz.1) is high while UCD05 is
        // low, so the next rising captures. The reference drops the ALU latch
        // clock here; without it the latch would capture only once.
        aluOutput.driveClock((control&32)==0,0);
        upper.falling(control,memory);
        if(upper.dram.memoryFault)fail(4);
    }
};
}

