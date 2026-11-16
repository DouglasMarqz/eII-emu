#pragma once
// Concrete (production) DRAM port: CPU bus rule, strobe semantics and settle.
//
// All values are certain on this path, so every merge/possible branch of
// DramCpuPort collapses to a single case. The ALU address latch is still the
// reference component in this slice; it becomes concrete in the next one.
#include <cstdint>
#include "controller_alu.h"
#include "digital_power_on.h"

namespace eii::production {
#ifdef EII_SOUND_PATH_LAB
// Native lab hook; absent from normal builds, including Release diagnostics.
void observeSampleRead(std::uint32_t address,std::uint8_t data);
#endif
class DramPort {
    friend struct ProductionDramTestAccess;
public:
    std::uint8_t readLatch=0,channelLatch=0;
    // The MDO/read/channel latches can capture an unknown bus value before the
    // first real CAS read. The concrete engine keeps only these three known
    // flags; a full mask model is unnecessary because the owner observes only
    // the latch masks below.
    bool readKnown=true,channelKnown=true,mdoKnown=false;
    const std::uint8_t* memoryKnown=nullptr;
    const std::uint8_t* underlay=nullptr;
    const std::uint8_t* underlayKnown=nullptr;
    std::uint32_t underlaySize=0;
    bool memoryFault=false;
    std::uint32_t memoryFaultAddress=0;
    bool memoryFaultCpu=false;
    bool readReady=false,writeReady=false;
    std::uint64_t reads=0,writes=0,readCaptures=0;
    std::uint32_t writeAddress=0;
    std::uint8_t writeData=0;
    ControllerAluLatch aluAddress;
    // Reproducible pre-first-clock realization, same bit order and derived
    // levels as DramCpuPort::initializePowerOn for a fully known control word.
    void initializePowerOn(DigitalPowerOnBits& bits,unsigned control,bool delayed9) noexcept {
        aluAddress.initializePowerOn(bits,logicNot({true,(control&128)!=0}));
        readLatch=std::uint8_t(bits.next());
        channelLatch=std::uint8_t(bits.next());
        mad_=std::uint8_t(bits.next());
        bank_=std::uint8_t(bits.next()&7);
        row_=std::uint8_t(bits.next());
        rowBank_=std::uint8_t(bits.next()&7);
        rasQ_=(bits.next()&1)!=0;
        casQ_=(bits.next()&1)!=0;
        ras_=!(rasQ_&&!casQ_);
        cas_=casQ_&&!(control&0x2000);
        channelClock_=(control&0x100)==0;
        readClock_=!delayed9;
        rowValid_=casAddressValid_=mdoCurrent_=readReady=writeReady=false;
        mdo_=0;sourceFull_=true;mdoKnown=false;readKnown=true;channelKnown=true;memoryFault=false;
    }

    void cpuBus(bool active,std::uint32_t address,std::uint8_t data,bool read) noexcept {
        if(!active || !active_ || address!=cpuAddress_ || read!=cpuRead_) {
            readReady=writeReady=mdoCurrent_=false;
            if(rowCpu_) {rowValid_=false;casAddressValid_=false;}
        }
        active_=active;cpuAddress_=address;cpuData_=data;cpuRead_=read;
    }
    void quietCapture() noexcept {mad_=std::uint8_t(cpuAddress_);bank_=std::uint8_t((cpuAddress_>>16)&7);sourceFull_=true;}
    std::uint8_t mdoForTestValue() const noexcept {return mdo_;}
    bool mdoCurrentForTest() const noexcept {return mdoCurrent_;}
    void setRowFromCpuForTest(bool fromCpu,bool valid) noexcept {rowCpu_=fromCpu;rowValid_=valid;}
    bool rowValidForTest() const noexcept {return rowValid_;}
    bool casAddressValidForTest() const noexcept {return casAddressValid_;}
    bool rasForTest() const noexcept {return ras_;}
    bool casForTest() const noexcept {return cas_;}
    bool rasQForTest() const noexcept {return rasQ_;}
    bool casQForTest() const noexcept {return casQ_;}
    bool rowCpuForTest() const noexcept {return rowCpu_;}
    Known24 aluOutputForTest() const noexcept {return aluAddress.output;}
    bool activeForTest() const noexcept {return active_;}
    bool canQuietCapture(unsigned control)const noexcept{return idleCpuPort(control);}

    void falling(unsigned control,bool delayed9,const std::uint8_t* memory) noexcept {
        if(idleCpuPort(control)&&!delayed9)return;
        rasQ_=!bit(control,12);
        aluAddress.driveClock({true,!bit(control,7)},{});
        channelClock(!bit(control,8));
        readClock(!delayed9);
        settle(control,memory);
    }
    void rising(unsigned oldControl,std::uint32_t sum,const std::uint8_t* memory) noexcept {
        if(idleCpuPort(oldControl)){quietCapture();return;}
        channelClock(true);
        readClock(true);
        const bool fromCpu=!bit(oldControl,15);
        const std::uint32_t source=fromCpu?cpuAddress_:aluAddress.output.value&0x7ffff;
        // Reference requires row_.known==255 and rowBank_.known==7, so a
        // partially known ALU address must not produce a valid row.
        sourceFull_=fromCpu||((aluAddress.output.known&0x7ffff)==0x7ffff);
        const std::uint8_t low=std::uint8_t(source),high=std::uint8_t(source>>8);
        mad_=bit(oldControl,11)?high:low;
        bank_=std::uint8_t((source>>16)&7);
        casQ_=bit(oldControl,12);
        aluAddress.driveClock({true,true},{sum,0xfffff});
        settle(oldControl,memory);
    }
public:
    static bool bit(unsigned control,unsigned b) noexcept {return (control>>b)&1u;}
    void settle(unsigned control,const std::uint8_t* memory) noexcept {
        // Both strobes idle and staying idle: no edge, capture or write can
        // occur; the general path below only clears the two address-valid
        // flags (its !rasNow tail) for any control word.
        if(rasQ_&&!casQ_&&!ras_&&!cas_){rowValid_=false;casAddressValid_=false;return;}
        const bool rasNow=!(rasQ_&&!casQ_);
        const bool casNow=casQ_&&!bit(control,13);
        const bool rasRise=!ras_&&rasNow;
        const bool casRise=!cas_&&casNow;
        const bool cpuSelected=!bit(control,15);
        const bool bankEnabled=cpuSelected||!bit(control,14);
        if(!bankEnabled) {rowValid_=false;casAddressValid_=false;}
        if(rasRise) {
            row_=mad_;rowBank_=bank_;rowCpu_=cpuSelected;
            rowValid_=bankEnabled&&(!cpuSelected||active_)&&sourceFull_;
        }
        if(casRise) {
            // Reference clears MDO on every possible CAS rise before deciding
            // whether this edge captures a real read.
            mdo_=0;mdoKnown=false;mdoCurrent_=false;
        }
        // Reference also refuses the capture when MAD is not fully known.
        if(casRise && rowValid_ && rowCpu_==cpuSelected && sourceFull_) {
            const std::uint32_t address=(std::uint32_t(rowBank_)<<16)|(std::uint32_t(mad_)<<8)|row_;
            if(!cpuSelected || (active_&&address==cpuAddress_)) {
                casAddress_=address;casAddressValid_=cpuSelected;
                if(!bit(control,10)) {
                    // Validate at the actual CAS read, for both CPU and sample
                    // ownership. DMA may have written the byte since request.
                    const bool hidden=address<underlaySize;
                    const auto* bytes=hidden?underlay:memory;
                    const auto* mask=hidden?underlayKnown:memoryKnown;
                    if(!bytes || (hidden && !mask) || (mask && mask[address]!=255)) {
                        if (!memoryFault) {
                            memoryFaultAddress=address;
                            memoryFaultCpu=cpuSelected;
                        }
                        memoryFault=true;mdo_=0;mdoKnown=false;mdoCurrent_=false;
                    } else {
                        mdo_=bytes[address];mdoKnown=true;mdoCurrent_=cpuSelected&&cpuRead_;
                        if(cpuSelected)++reads;
#ifdef EII_SOUND_PATH_LAB
                        else observeSampleRead(address,mdo_);
#endif
                    }
                }
            }
        }
        if(casNow && bit(control,10)) {mdo_=0;mdoKnown=false;mdoCurrent_=false;}
        if(casNow && rowValid_ && casAddressValid_ && active_ && !cpuRead_ && cpuSelected &&
           bit(control,10) && !writeReady) {
            writeAddress=casAddress_;writeData=cpuData_;writeReady=true;++writes;
        }
        if(!casNow) casAddressValid_=false;
        if(!rasNow) {rowValid_=false;casAddressValid_=false;}
        ras_=rasNow;cas_=casNow;
    }
    void channelClock(bool level) noexcept {
        if(!channelClock_&&level) {
            if(mdoKnown){channelLatch=mdo_;channelKnown=true;}
            else {channelLatch=0;channelKnown=false;}
        }
        channelClock_=level;
    }
    void readClock(bool level) noexcept {
        if(!readClock_&&level) {
            ++readCaptures;
            if(mdoKnown){readLatch=mdo_;readKnown=true;}
            else {readLatch=0;readKnown=false;}
            if(active_&&cpuRead_&&mdoCurrent_)readReady=readKnown;
        }
        readClock_=level;
    }
private:
    // Same no-strobe predicate as DramCpuPort's reference event path. An idle
    // source edge only clocks the CPU address mux; no DRAM/DAC latch changes,
    // memory access or ALU address capture is possible in this exact state.
    bool idleCpuPort(unsigned control)const noexcept {
        return !(control&0xff80) && rasQ_&&!casQ_&&!ras_&&!cas_ &&
            readClock_&&channelClock_&&!aluAddress.mayCapture();
    }
    bool active_=false,cpuRead_=true,rowCpu_=true,rowValid_=false,casAddressValid_=false,mdoCurrent_=false,sourceFull_=true;
    bool rasQ_=false,casQ_=false;
    // Reference starts with unknown RAS/CAS levels, so its first high level is
    // a merge rather than an edge; start high to match that.
    bool ras_=true,cas_=true;
    // The reference starts with unknown clock levels, so its first high level
    // is a merge, not a certain capture. Start high to match that.
    bool readClock_=true,channelClock_=true;
    std::uint32_t cpuAddress_=0,casAddress_=0;
    std::uint8_t cpuData_=0,mad_=0,bank_=0,row_=0,rowBank_=0,mdo_=0;
};
}

