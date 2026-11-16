#pragma once
#include <vector>
#include <tuple>
#include "controller_alu.h"
#include "controller_prom.h"
#include "dram_strobe_program.h"

namespace eii {
struct DramBackingView {
    const std::vector<std::uint8_t>* bytes=nullptr;
    const std::vector<std::uint8_t>* known=nullptr;
    // Physical DRAM hidden by the CPU-local ROM/SRAM decoder. The sample
    // address path reaches it through ordinary bank0 RAS/CAS (7-23..26).
    const std::uint8_t* underlay=nullptr;
    const std::uint8_t* underlayKnown=nullptr;
    std::uint32_t underlaySize=0;
    KnownByte read(std::uint32_t a) const noexcept {
        if(a<underlaySize) {
            if(!underlay || !underlayKnown)return {};
            return {underlay[a],underlayKnown[a]};
        }
        if(!bytes || !known || a>=bytes->size() || a>=known->size())return {};
        return {(*bytes)[a],(*known)[a]};
    }
};
inline KnownByte mergeDramByte(KnownByte a,KnownByte b) noexcept {
    return {a.value,std::uint8_t(a.known&b.known&~(a.value^b.value))};
}
// Shared CPU/playback read path of7-23/24/25: ALUGO/IC69 address,
// IC49/68 RAS/CAS, IC103 CPU and IC40 CHDB latches. CPU writes supported;
// sampling/record writes and physical RC/propagation/refresh are absent.
class DramCpuPort {
public:
    // Counters are diagnostics, not state that can affect future bus behavior.
    bool sameState(const DramCpuPort& b) const noexcept {
        return std::tie(readLatch,channelLatch,channelClock_,rowCpu_,aluAddress,readReady,writeReady,writeAddress,writeData,
            active_,cpuRead_,rowValid_,mdoCurrent_,casAddressValid_,cpuAddress_,casAddress_,
            cpuData_,mad_,bank_,row_,rowBank_,mdo_,rasQ_,casQ_,ras_,cas_,readClock_) ==
            std::tie(b.readLatch,b.channelLatch,b.channelClock_,b.rowCpu_,b.aluAddress,b.readReady,b.writeReady,b.writeAddress,b.writeData,
            b.active_,b.cpuRead_,b.rowValid_,b.mdoCurrent_,b.casAddressValid_,b.cpuAddress_,b.casAddress_,
            b.cpuData_,b.mad_,b.bank_,b.row_,b.rowBank_,b.mdo_,b.rasQ_,b.casQ_,b.ras_,b.cas_,b.readClock_);
    }
    void initializePowerOn(DigitalPowerOnBits& bits,KnownWord control,LogicLevel delayed9) noexcept {
        aluAddress.initializePowerOn(bits,logicNot({bool(control.known&128),bool(control.value&128)}));
        readLatch={std::uint8_t(bits.next()),255};channelLatch={std::uint8_t(bits.next()),255};
        mad_={std::uint8_t(bits.next()),255};bank_={std::uint8_t(bits.next()&7),7};
        row_={std::uint8_t(bits.next()),255};rowBank_={std::uint8_t(bits.next()&7),7};
        rasQ_={true,bool(bits.next()&1)};casQ_={true,bool(bits.next()&1)};
        ras_=logicNot(logicAnd(rasQ_,logicNot(casQ_)));
        cas_=logicAnd(casQ_,{true,!(control.value&0x2000)});
        channelClock_={true,!(control.value&0x100)};readClock_=logicNot(delayed9);
        // These are adapter validity records, not physical power-on latches.
        // No valid DRAM transfer has occurred before the first source clock.
        rowValid_=casAddressValid_=mdoCurrent_=readReady=writeReady=false;mdo_={};settleValid_=false;
    }
    void enableEventTiming(bool enable=true) noexcept {eventTiming_=enable;settleValid_=false;}
    KnownByte readLatch{},channelLatch{};
    std::uint64_t sampleReads=0;
    ControllerAluLatch aluAddress;
    bool readReady=false,writeReady=false;
    std::uint64_t reads=0,writes=0,readCaptures=0;
    std::uint32_t writeAddress=0;
    KnownByte writeData{};
    void cpuBus(bool active,std::uint32_t address,KnownByte data,bool read) noexcept {
        if(eventTiming_ && (active!=active_ || (active && (address!=cpuAddress_ || read!=cpuRead_ || (!read && !(data==cpuData_))))))settleValid_=false;
        if(!active || !active_ || address!=cpuAddress_ || read!=cpuRead_) {
            readReady=writeReady=mdoCurrent_=false;
            if(rowCpu_) {rowValid_=false;casAddressValid_=false;}
        }
        active_=active;cpuAddress_=address;cpuData_=data;cpuRead_=read;
    }
    // An interval certified by the controller as having no strobe/latch
    // edge. IC69 still captures the current CPU address at each rising CLK;
    // repeated captures have the same result. No backing-memory read here.
    bool advanceQuietClocks(KnownWord control,LogicLevel delayed9) noexcept {
        if(!eventTiming_ || !idleCpuPort(control) || !delayed9.known || delayed9.high)return false;
        mad_={std::uint8_t(cpuAddress_),255};bank_={std::uint8_t((cpuAddress_>>16)&7),7};
        return true;
    }
    void falling(KnownWord control,LogicLevel delayed9,DramBackingView memory) noexcept {
        if(eventTiming_ && idleCpuPort(control) && delayed9.known && !delayed9.high)return;
        const auto bit=[&](unsigned b){return LogicLevel{bool(control.known&(1u<<b)),bool(control.value&(1u<<b))};};
        // IC49 negative-edge JK, J=!UCD12, K=UCD12 -> Q=!UCD12.
        rasQ_=logicNot(bit(12));
        aluAddress.driveClock(logicNot(bit(7)),{});
        channelClock(logicNot(bit(8)));
        readClock(logicNot(delayed9)); // -MPGTE=NAND(-CLK,delayed UCD09)
        settle(control,memory);
    }
    void rising(KnownWord oldControl,Known24 sum,DramBackingView memory) noexcept {
        if(eventTiming_ && idleCpuPort(oldControl)) {
            // No strobe changes. Only IC69's address mux latch is clocked.
            mad_={std::uint8_t(cpuAddress_),255};bank_={std::uint8_t((cpuAddress_>>16)&7),7};
            return;
        }
        // Capture OLD MDO before CAS/RAS change on this edge.
        channelClock({true,true});
        readClock({true,true});
        const bool fromCpu=(oldControl.known&0x8000) && !(oldControl.value&0x8000);
        Known24 source=fromCpu?Known24{cpuAddress_,0x7ffff}:aluAddress.output;
        if(!(oldControl.known&0x8000))source={};
        const LogicLevel highByte{bool(oldControl.known&0x800),bool(oldControl.value&0x800)};
        const KnownByte low{std::uint8_t(source.value),std::uint8_t(source.known)};
        const KnownByte high{std::uint8_t(source.value>>8),std::uint8_t(source.known>>8)};
        mad_=highByte.known?(highByte.high?high:low):mergeDramByte(low,high);
        bank_={std::uint8_t((source.value>>16)&7),std::uint8_t((source.known>>16)&7)};
        casQ_={bool(oldControl.known&0x1000),bool(oldControl.value&0x1000)};
        aluAddress.driveClock({true,true},sum);
        settle(oldControl,memory);

    }
    void settle(KnownWord control,DramBackingView memory) noexcept {
        if(eventTiming_ && (control.known&0xe400)==0xe400 &&
           rasQ_.known && casQ_.known && ras_.known && cas_.known) {
            executeStrobes(control.value,memory);
            return;
        }
        // RAS/CAS, ownership, bank-reset and WE are the only combinational
        // inputs. MAD/data are captured on their original clock/edge events.
        // A stable input network is an idempotent component transition.
        std::uint64_t key=0;
        if(eventTiming_) {
            key=std::uint64_t(control.value&0xe400)|(std::uint64_t(control.known&0xe400)<<16)|
                (std::uint64_t(unsigned(rasQ_.known)|(unsigned(rasQ_.high)<<1))<<32)|
                (std::uint64_t(unsigned(casQ_.known)|(unsigned(casQ_.high)<<1))<<34);
            if(settleValid_ && key==settleKey_)return;
        }
        // +RAS is ACTIVE HIGH before bank NANDs invert it to -RASBn.
        const auto ras=logicNot(logicAnd(rasQ_,logicNot(casQ_)));
        const auto cas=logicAnd(casQ_,{bool(control.known&0x2000),!(control.value&0x2000)});
        const bool rasRise=ras_.known&&!ras_.high&&ras.known&&ras.high;
        const bool casRise=cas_.known&&!cas_.high&&cas.known&&cas.high;
        const bool possibleCasRise=(!cas_.known||!cas_.high)&&(!cas.known||cas.high);
        const bool cpuSelected=(control.known&0x8000) && !(control.value&0x8000);
        const bool sourceKnown=control.known&0x8000;
        // IC99/IC31 asynchronous RASRST: UCD15 & UCD14 high clears the
        // bank-select register. Sample addresses must also have UCD14 low.
        const bool bankEnabled=cpuSelected ||
            (sourceKnown && (control.known&0x4000) && !(control.value&0x4000));
        if(!bankEnabled) {rowValid_=false;casAddressValid_=false;}
        if(rasRise) {
            row_=mad_;rowBank_=bank_;rowCpu_=cpuSelected;
            rowValid_=sourceKnown&&bankEnabled&&(!cpuSelected||active_)&&row_.known==255&&rowBank_.known==7;
        }
        if(possibleCasRise) {
            const LogicLevel we{bool(control.known&0x400),bool(control.value&0x400)};
            mdo_={};mdoCurrent_=false;
            if(casRise && rowValid_ && sourceKnown && rowCpu_==cpuSelected && mad_.known==255) {
                const auto address=(unsigned(rowBank_.value)<<16)|(unsigned(mad_.value)<<8)|row_.value;
                if(!cpuSelected || (active_ && address==cpuAddress_)) {
                    casAddress_=address;casAddressValid_=cpuSelected;
                    if(we.known&&!we.high) {
                        mdo_=memory.read(address);
                        mdoCurrent_=cpuSelected&&cpuRead_;
                        if(cpuSelected)++reads;else ++sampleReads;
                    }
                }
            }
        }
        // Active write disables a valid read result. Recording/ADC bus
        // input is not implemented; never reuse a stale sample as its data.
        if(cas.known&&cas.high&&(control.known&0x400)&&(control.value&0x400)) {
            mdo_={};mdoCurrent_=false;
        }
        // Supplied CPU microcode asserts UCD10 AFTER CAS: late-write mode.
        // Keep the CAS-latched column/address while WE changes. Early-write
        // (WE already active at CAS) reaches this same stable-address path.
        if(cas.known&&cas.high && rowValid_ && casAddressValid_ && active_&&!cpuRead_&&cpuSelected &&
           (control.known&0x400) && (control.value&0x400) && !writeReady) {
            writeAddress=casAddress_;writeData=cpuData_;writeReady=writeData.known==255;++writes;
        }
        if(cas.known&&!cas.high)casAddressValid_=false;
        if(ras.known&&!ras.high){rowValid_=false;casAddressValid_=false;}
        ras_=ras;cas_=cas;
        if(eventTiming_) {
            settleKey_=key;
            // Unknown write data leaves writeReady false: the reference counts
            // each repeated aperture, so that case must remain on the edge path.
            settleValid_=ras.known && cas.known && !(cas.high && rowValid_ && casAddressValid_ && active_ &&
                !cpuRead_ && cpuSelected && (control.known&0x400) && (control.value&0x400) && !writeReady);
        }
    }
#if EII_DIAGNOSTICS
public:
    std::uint8_t mdoForTest() const noexcept {return mdo_.value;}
    bool rowValidForTest() const noexcept {return rowValid_;}
    bool rasForTest() const noexcept {return ras_.high;}
    bool casForTest() const noexcept {return cas_.high;}
    bool rasQForTest() const noexcept {return rasQ_.high;}
    bool casQForTest() const noexcept {return casQ_.high;}
    bool rowCpuForTest() const noexcept {return rowCpu_;}
    Known24 aluOutputForTest() const noexcept {return aluAddress.output;}
    bool activeForTest() const noexcept {return active_;}
    bool casAddressValidForTest() const noexcept {return casAddressValid_;}
    bool mdoCurrentForTest() const noexcept {return mdoCurrent_;}
private:
#endif
    bool idleCpuPort(KnownWord control) const noexcept {
        return (control.known&0xff80)==0xff80 && !(control.value&0xff80) &&
            rasQ_.known&&rasQ_.high && casQ_.known&&!casQ_.high &&
            ras_.known&&!ras_.high && cas_.known&&!cas_.high &&
            readClock_.known&&readClock_.high && channelClock_.known&&channelClock_.high &&
            !aluAddress.mayCapture();
    }
    void executeStrobes(unsigned control,DramBackingView memory) noexcept {
        const unsigned index=((control>>10)&1)|((control>>12)&14)|
            (unsigned(rasQ_.high)<<4)|(unsigned(casQ_.high)<<5)|
            (unsigned(ras_.high)<<6)|(unsigned(cas_.high)<<7);
        const auto op=dramStrobeProgram[index];
        const bool cpu=op&DramStrobeProgram::Cpu;
        if(!(op&DramStrobeProgram::Bank))rowValid_=casAddressValid_=false;
        if(op&DramStrobeProgram::Row) {
            row_=mad_;rowBank_=bank_;rowCpu_=cpu;
            rowValid_=(op&DramStrobeProgram::Bank)&&(!cpu||active_)&&row_.known==255&&rowBank_.known==7;
        }
        if(op&DramStrobeProgram::Column) {
            mdo_={};mdoCurrent_=false;
            if(rowValid_ && rowCpu_==cpu && mad_.known==255) {
                const auto address=(unsigned(rowBank_.value)<<16)|(unsigned(mad_.value)<<8)|row_.value;
                if(!cpu || (active_&&address==cpuAddress_)) {
                    casAddress_=address;casAddressValid_=cpu;
                    if(!(control&0x400)) {
                        mdo_=memory.read(address);mdoCurrent_=cpu&&cpuRead_;
                        if(cpu)++reads;else ++sampleReads;
                    }
                }
            }
        }
        if(op&DramStrobeProgram::Write) {
            mdo_={};mdoCurrent_=false;
            if(rowValid_&&casAddressValid_&&active_&&!cpuRead_&&cpu&&!writeReady) {
                writeAddress=casAddress_;writeData=cpuData_;writeReady=writeData.known==255;++writes;
            }
        }
        if(!(op&DramStrobeProgram::Cas))casAddressValid_=false;
        if(!(op&DramStrobeProgram::Ras))rowValid_=casAddressValid_=false;
        ras_={true,bool(op&DramStrobeProgram::Ras)};cas_={true,bool(op&DramStrobeProgram::Cas)};
        // A later partial-pin transition must not reuse a pre-event cache key.
        settleValid_=false;
    }
    bool eventTiming_=false,settleValid_=false;
    std::uint64_t settleKey_=0;
    bool rowCpu_=true;
    bool active_=false,cpuRead_=true,rowValid_=false,mdoCurrent_=false,casAddressValid_=false;
    std::uint32_t cpuAddress_=0,casAddress_=0;
    KnownByte cpuData_{},mad_{},bank_{},row_{},rowBank_{},mdo_{};
    LogicLevel rasQ_{},casQ_{},ras_{},cas_{},readClock_{},channelClock_{};
    void channelClock(LogicLevel level) noexcept {
        const bool certain=channelClock_.known&&!channelClock_.high&&level.known&&level.high;
        const bool possible=(!channelClock_.known||!channelClock_.high)&&(!level.known||level.high);
        if(certain)channelLatch=mdo_;
        else if(possible)channelLatch=mergeDramByte(channelLatch,mdo_);
        channelClock_=level;
    }
    void readClock(LogicLevel level) noexcept {
        const bool certain=readClock_.known&&!readClock_.high&&level.known&&level.high;
        const bool possible=(!readClock_.known||!readClock_.high)&&(!level.known||level.high);
        if(certain) {
            readLatch=mdo_;++readCaptures;
            if(active_&&cpuRead_&&mdoCurrent_)readReady=readLatch.known==255;
        } else if(possible)readLatch=mergeDramByte(readLatch,mdo_);
        readClock_=level;
    }
};
}

