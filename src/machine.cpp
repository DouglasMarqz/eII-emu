#include "machine.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace eii {
std::vector<std::uint8_t> readFile(const std::filesystem::path& p) {
    std::ifstream f(p,std::ios::binary);
    if(!f) throw std::runtime_error("Cannot open input file: "+p.string());
    return {std::istreambuf_iterator<char>(f),{}};
}
void Lcd::write(bool data,std::uint8_t v) {
    ++writes;
    if(data) {ram[address&127]=v;address=(address+(increment?1:127))&127;}
    else if(v&128) address=v&127;
    else if(v==1) {ram.fill(' ');address=0;}
    else if(v==2) address=0;
    else if((v&0xfc)==4) increment=(v&2)!=0;
    else if((v&0xf8)==8) display=(v&4)!=0;
}
std::uint8_t Lcd::read(bool data) {
    if(!data) return address; // busy timing not implemented in this first probe
    auto result=ram[address&127];address=(address+(increment?1:127))&127;return result;
}
std::string Lcd::line(int row) const {
    std::string s;for(int i=0;i<16;++i) {auto c=ram[(row?64:0)+i];s+=c>=32&&c<127?char(c):'?';}return s;
}
void PioPort::command(std::uint8_t v) {
    if(expectDirection) {mask=v;expectDirection=false;return;}
    if(expectMask) {interruptMask=v;expectMask=false;return;}
    if(!(v&1)) {vector=v;return;}
    switch(v&15) {
    case 15: mode=v>>6;expectDirection=mode==3;enabled=false;ready=false;pending=false;inputFull=false;break;
    case 7: enabled=v&128;expectMask=v&16;control=v;if(expectMask)pending=false;break;
    case 3: enabled=v&128;break;
    }
}
std::uint8_t PioPort::read() {
    if(mode==0) return output;
    if(mode==3) return (input&mask)|(output&~mask);
    ready=true;pending=false;return input;
}
void Node::reset() {pins=z80_init(&cpu);z80ctc_init(&ctc);pio={};pioAcks={};ioActive=false;ioUnknownObserved=false;pioIrqActive=false;}
Machine::Machine(std::uint8_t initialControllerPhase):memory(0x80000),dramKnown(0x80000),channelTimers(initialControllerPhase) {scanner.scanner=true;main.reset();scanner.reset();channelTimers.enableControllerTiming();channelTimers.controllerTiming.enableRegisterWrites();}
void Machine::initializeDramPowerOn(std::uint32_t seed) {
    if(!seed)throw std::invalid_argument("DRAM power-on seed must be nonzero");
    if(ticks)throw std::logic_error("DRAM power-on state must be selected before the first tick");
    // Preserve the original visible-window sequence for existing checkpoints.
    // Then seed physically installed DRAM hidden by CPU-local ROM/SRAM.
    // ROM/SRAM backing remains intact; default unseeded DRAM stays unknown.
    // xorshift32 (13,17,5), low byte per increasing physical address.
    for(std::size_t address=0x2800;address<memory.size();++address) {
        seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
        memory[address]=std::uint8_t(seed);dramKnown[address]=255;
    }
    for(std::size_t address=0;address<dramUnderlay.size();++address) {
        seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
        dramUnderlay[address]=std::uint8_t(seed);dramUnderlayKnown[address]=255;
    }
    // Second 512 KB bank continues the same sequence, so bank-A contents and
    // every existing 512 KB checkpoint stay unchanged.
    if(secondBank_) {
        for(std::size_t address=0x2800;address<otherBank_.size();++address) {
            seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
            otherBank_[address]=std::uint8_t(seed);otherBankKnown_[address]=255;
        }
        for(std::size_t address=0;address<otherUnderlay_.size();++address) {
            seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
            otherUnderlay_[address]=std::uint8_t(seed);otherUnderlayKnown_[address]=255;
        }
    }
}
void Machine::installSecondBank(bool installed) {
    if(ticks)throw std::logic_error("Memory size must be selected before the first tick");
    secondBank_=installed;rtsBank_=false;
    otherBank_.assign(installed?memory.size():0,0);otherBankKnown_.assign(installed?memory.size():0,0);
    otherUnderlay_.fill(0);otherUnderlayKnown_.fill(0);
}
void Machine::updateMemoryBank() noexcept {
    if(!secondBank_)return;
    const bool rts=(diskSio.channels[1].wr[5]&2)!=0; // Z80 SIO WR5 D1: RTS (pin /RTSB low)
    if(rts==rtsBank_)return;
    std::swap_ranges(memory.begin()+0x2800,memory.end(),otherBank_.begin()+0x2800);
    std::swap_ranges(dramKnown.begin()+0x2800,dramKnown.end(),otherBankKnown_.begin()+0x2800);
    std::swap_ranges(dramUnderlay.begin(),dramUnderlay.end(),otherUnderlay_.begin());
    std::swap_ranges(dramUnderlayKnown.begin(),dramUnderlayKnown.end(),otherUnderlayKnown_.begin());
    rtsBank_=rts;++bankSwaps;
}
void Machine::enableCpuDram() noexcept {
    cpuDramEnabled=true;channelTimers.controllerTiming.attachCpuDram(dramBacking());
}
void Machine::load(const std::filesystem::path& mr,const std::filesystem::path& sr,
                   const std::filesystem::path& p135,const std::filesystem::path& p137) {
    const auto put=[](auto& dst,const auto& p) {auto b=readFile(p);if(b.size()!=dst.size()) throw std::runtime_error("Unexpected ROM size: "+p.string());std::copy(b.begin(),b.end(),dst.begin());};
    put(main.rom,mr);put(scanner.rom,sr);
    if(!p135.empty()) put(prom.upper,p135);
    if(!p137.empty()) put(prom.lower,p137);
    if(!p135.empty() || !p137.empty())
        channelTimers.controllerTiming.loadProms(prom,!p135.empty(),!p137.empty());
    if(!p135.empty() && !p137.empty())enableCpuDram();
}
std::uint32_t Machine::physical(std::uint16_t a,bool dma) const {
    unsigned slot=(dma?0:2)|(a>>15);
    return (std::uint32_t(pageReset?0:pages[slot])<<15)|(a&0x7fff);
}
std::uint8_t Machine::readMemory(bool scan,std::uint16_t a) const {
    if(scan) return a&0x8000?scannerRam[a&2047]:scanner.rom[a&8191];
    auto p=physical(a);return p<8192?main.rom[p]:memory[p];
}
void Machine::writeMemory(bool scan,std::uint16_t a,std::uint8_t v) {
    if(scan) {if(a&0x8000) scannerRam[a&2047]=v;}
    else {auto p=physical(a);if(p>=8192) {memory[p]=v;if(p>=0x2800)dramKnown[p]=255;}}
}
void Machine::pioWrite(Node& n,unsigned address,std::uint8_t v) {
    const auto index=(address>>1)&1;
    auto& p=n.pio[index];
    if(address&1) {
        const bool inputReady=n.pio[1].ready;
        p.command(v);
        // Mode 2 uses B's handshake lines even though B data is mode 3.
        if(index==1 && n.pio[0].mode==2) p.ready=inputReady;
        refreshPioIrq(n);
        if(!n.scanner)updateFloppy();
        return;
    }
    p.output=v;
    if(p.mode==0 || (index==0 && p.mode==2)) {p.ready=true;p.pending=false;}
    refreshPioIrq(n);
    if(!n.scanner)updateFloppy();
}
void Machine::pioLinkTick() {
    // Schematic 7-14: IC23 clocks on falling +C2MHz.D (every 2 CPU clocks).
    // Both JK halves have K=Q, hence Q(next)=J & !Q(previous).
    if(ticks&1) return;
    if(main.pio[0].mode!=2 || scanner.pio[0].mode!=2) return;
    const bool toMain=!scanToMainPulse && !main.pio[0].ready && main.pio[1].ready && scanner.pio[0].ready;
    const bool toScan=!mainToScanPulse && main.pio[0].ready && scanner.pio[1].ready;
    const auto transfer=[](Node& from,Node& to,bool oldPulse,bool newPulse,std::uint64_t& count) {
        if(newPulse) to.pio[0].input=from.pio[0].output; // active-low STB loads input
        if(oldPulse && !newPulse) { // rising STB acknowledges both sides
            from.pio[0].ready=false;from.pio[0].pending=true;
            to.pio[0].inputFull=true;to.pio[1].ready=false;to.pio[1].pending=true;
            ++count;refreshPioIrq(from);refreshPioIrq(to);
        }
    };
    transfer(scanner,main,scanToMainPulse,toMain,scanToMainTransfers);
    transfer(main,scanner,mainToScanPulse,toScan,mainToScanTransfers);
    scanToMainPulse=toMain;mainToScanPulse=toScan;
}
std::uint64_t Machine::pioInterrupts(Node& n,std::uint64_t pins) {
    if(runtimeDevices_ && !n.pioIrqActive)return pins;
    // CTC is upstream. A > B within each PIO. RETI is consumed by the
    // highest-priority device currently in service, including nested IRQs.
    if(pins&Z80_RETI) {
        for(auto& p:n.pio) if(p.servicing) {p.servicing=false;pins&=~Z80_RETI;break;}
    }
    for(unsigned i=0;i<2 && (pins&Z80_IEIO);++i) {
        auto& p=n.pio[i];
        if(p.servicing) {pins&=~Z80_IEIO;break;}
        if(!p.enabled || p.expectMask || !p.pending) continue;
        pins&=~Z80_IEIO;
        if((pins&(Z80_M1|Z80_IORQ))==(Z80_M1|Z80_IORQ)) {
            Z80_SET_DATA(pins,p.vector);p.pending=false;p.servicing=true;++n.pioAcks[i];
        } else pins|=Z80_INT;
        break;
    }
    if(runtimeDevices_)refreshPioIrq(n);
    return pins;
}
void Machine::updateFloppy() {
    const auto& b=main.pio[1];
    const auto pins=b.mode==0?b.output:b.mode==3?std::uint8_t((b.output&~b.mask)|b.mask):std::uint8_t(255);
    floppy.drive(pins,diskSio.sideN(),diskSio.writeGateN(),ticks);
    main.pio[1].input=std::uint8_t((main.pio[1].input&~0x21)|(floppy.trackZeroN()?1:0)|(floppy.protectN()?32:0));
    // Sheet 7-15: CN1-8 INDEX -> CTSA (pin18); CN1-34 READY -> DCDA (pin19).
    diskSio.inputs(floppy.indexN(),floppy.readyN());
    // Production runtime only: both drives deselected (INDEX/READY/TRK0/WPRT
    // inactive and time-independent) and an idle read/write path. Any PIO B or
    // SIO write re-enters here and clears the flag before the next boundary.
    floppyQuiet_=runtimeDevices_&&diskRead&&diskRead->quiescent(floppy,diskSio);
}
std::uint8_t Machine::input(Node& n,std::uint8_t p) {
    if constexpr(diagnosticsEnabled) ++n.ins[p];
    if(!n.scanner){updateFloppy();if(hardDisk&&p>=0x3c&&p<=0x3f)return hardDisk->read(p,ticks);if((p&0xf8)==0x38)return diskSio.read(p);}
    if(!n.scanner&&(p&0xf8)==0x20){if(hardDisk||diskDma.floppyIo)return diskDma.status(dmaReadyN());++diskDma.unsupported;return 255;}
    const auto group=n.scanner?(p&0x70):(p&0xf8);
    if(group==(n.scanner?0x50:0x30)) {
        auto& port=n.pio[(p>>1)&1];
        if(p&1) return (n.pio[0].enabled?128:0)|(n.pio[1].enabled?64:0);
        if(!(p&2) && port.mode==2) {
            port.inputFull=false;n.pio[1].ready=true;n.pio[1].pending=false;refreshPioIrq(n);
            return port.input;
        }
        const auto value=port.read();refreshPioIrq(n);return value;
    }
    if(n.scanner) {
        if((p&0x7e)==0x34) {
            const auto value=scannerControls.adc.result;
            if(value.known!=255){++scannerControls.unknownCpuReads;n.ioUnknownObserved=true;}
            // The Z80 core has no unknown-data mask; expose unresolved bits
            // as1 only at this CPU boundary, and report every such read.
            return value.value|std::uint8_t(~value.known);
        }
        if((p&0x7e)==0x38) {
            const auto& b=n.pio[1];
            const auto pb=b.mode==0?b.output:b.mode==3?std::uint8_t((b.output&~b.mask)|(b.input&b.mask)):b.input;
            return scannerControls.columns(pb);
        }
        if((p&0x7e)==0x3e) return lcd.read(p&1);
    } else if((p&0xf8)==0x08) {
        ++sampleReads;return sampleReadValue(n);
    } else if(p<=3) {
        if(p==2)return midiSerial.readMode();
        if(p==3)return midiSerial.readCommand();
        if(p==0 && midiSerial.ready)return midiSerial.readData();
        if(p==1 && midiSerial.midiMode())return midiSerial.status()|(serialReady?2:0);
        if(p==1) return 1|(serialReady?2:0); // TXRDY / RXRDY
        if(p==0 && serialReady) {serialReady=false;serialNext=ticks+4167;return serialByte;}
        return 0;
    }
    ++unmappedIo;return 255;
}
void Machine::output(Node& n,std::uint8_t p,std::uint8_t v) {
    if constexpr(diagnosticsEnabled) ++n.outs[p];
    if(!n.scanner&&hardDisk&&p>=0x3c&&p<=0x3f){hardDisk->write(p,v,ticks);return;}
    if(!n.scanner&&(p&0xf8)==0x38){diskSio.write(p,v);updateFloppy();updateMemoryBank();return;}
    if(!n.scanner&&(p&0xf8)==0x20){diskDma.write(v);return;}
    auto group=n.scanner?(p&0x70):(p&0xf8);
    if(group==(n.scanner?0x50:0x30)) {pioWrite(n,p,v);if(!n.scanner)sampleQuiet_=false; // IMODE may change
        if(!n.scanner&&runtimeAudio_)runtimeAudio_->nativeEvent(channelTimers.halfClocks,{0,analog::NativeEvent::Mode,0,std::uint8_t(channel0AudioEnabled()),255,0});
        return;}
    if(n.scanner) {
        if((p&0x7e)==0x36) return; // ALE/START pulse follows actual bus pins in step().
        if((p&0x7e)==0x3e) {lcd.write(p&1,v);return;}
        // left/right panel LED latches do not affect CPU execution
        if((p&0x4c)==4 || (p&0x4c)==8 || (p&0x4c)==12) return;
    } else {
        if(channelTimers.write(p,v)) return;
        if((p&0xf8)==0x40) {reconstructionControl[p&7].completedWrite(v);
            if(runtimeAudio_)runtimeAudio_->nativeEvent(channelTimers.halfClocks,{0,analog::NativeEvent::Switch,std::uint8_t(p&7),std::uint8_t(v&63),63,0});
            return;}
        // IC86 output 6/7: PCSWMX / PCSWDA; PA2..0 are not decoded.
        if((p&0xf8)==0x70) {controlVoltage.writeMux(v,ticks);nativeControls();return;}
        if((p&0xf8)==0x78) {controlVoltage.writeDac(v,ticks);nativeControls();return;}
        if(registerStaging.write(p,v)) {
            const bool high=(p&0xf8)==0x60;
            channelTimers.controllerTiming.cpuRegisterWrite(registerStaging,high);
            if(high)++commands;
            return;
        }
        if((p&0xf8)==0x68) {pages[p&3]=v&15;pageReset=false;return;}
        if(p==6)midiSerial.writeMode(v);
        if(p==7)midiSerial.writeCommand(v);
        if(p>=4&&p<=7) {if(p==4 && serialTx.size()<65536) serialTx+=char(v);return;}
    }
    ++unmappedIo;
}
// One CPU boundary for one node. The node kind is a compile-time constant so
// each Z80 gets its own straight body (main: DRAM/WAIT/disk; scanner: panel).
template<bool Scanner> void Machine::stepNode(Node& n) {
    constexpr bool scanner=Scanner;
    const bool dmaBus=!scanner&&diskDma.granted;
    bool holdCpu=false;
    if(!scanner && cpuDramEnabled&&!dmaBus) {
        const auto& controller=channelTimers.controllerTiming;
        bool wait=false;
        if(dramBusActive) {
            wait=!controller.observedWait.known||!controller.observedWait.high||
                !controller.cpuDramReady()||!dramServiced;
            if(n.pins&Z80_RD) {
                const auto data=controller.cpuDramReadData();
                wait|=data.known!=255;
                if(data.known==255)Z80_SET_DATA(n.pins,data.value);
            }
        }
        if(wait){n.pins|=Z80_WAIT;++dramWaitClocks;holdCpu=true;}else n.pins&=~Z80_WAIT;
    }
    if(!scanner && !dmaBus && (sampleWaitPin_||(!sampleQuiet_&&sampleReadSelected(n.pins))) && sampleWaitStep(n,holdCpu))holdCpu=true;
    // The pinned core checks _wait BEFORE emitting _mread/_mwrite. Once
    // pins expose a new DRAM request its next internal step would consume
    // data without another WAIT check. Extend the visible bus cycle here;
    // controller, CTC, scanner and source clocks continue during every hold.
    auto pins=dmaBus?diskDma.pins():holdCpu?n.pins:z80_tick(&n.cpu,n.pins);
    auto a=Z80_GET_ADDR(pins);
    if(diagnosticsEnabled && !dmaBus&&!holdCpu && (pins&(Z80_M1|Z80_MREQ|Z80_RD))==(Z80_M1|Z80_MREQ|Z80_RD)) ++n.fetched[a];
    if(pins&Z80_MREQ) {
        const bool controlled=!scanner&&cpuDramEnabled&&physical(a,dmaBus)>=0x2800;
        if(controlled) {
            if(pins&Z80_RD) {auto data=channelTimers.controllerTiming.cpuDramReadData();if(data.known==255)Z80_SET_DATA(pins,data.value);}
        } else if(pins&Z80_RD) {auto v=dmaBus?(physical(a,true)<8192?main.rom[physical(a,true)]:memory[physical(a,true)]):readMemory(scanner,a);Z80_SET_DATA(pins,v);}
        else if(pins&Z80_WR) {
            if(dmaBus){const auto p=physical(a,true);if(p>=8192){memory[p]=Z80_GET_DATA(pins);if(p>=0x2800)dramKnown[p]=255;}}
            else writeMemory(scanner,a,Z80_GET_DATA(pins));
        }
    }
    const auto ctcBase=scanner?0x60:0x28;
    bool ctcSelected=scanner?((a&0x70)==ctcBase):((a&0xf8)==ctcBase);
    bool io=(pins&Z80_IORQ)&&!(pins&Z80_M1);
    // LS374 and S3528 commit on rising write strobes: retain PDO and latch
    // at the first CPU boundary where the write strobe is deasserted.
    const bool stagingSelected=!scanner && io && (pins&Z80_WR) &&
        std::uint8_t(a)>=0x40 && std::uint8_t(a)<0x68;
    if(!scanner && stagingWritePending &&
       (!stagingSelected || (std::uint8_t(a)&0xf8)!=(stagingWritePort&0xf8))) {
        output(n,stagingWritePort,stagingWriteData);
        stagingWritePending=false;
    }
    if(stagingSelected) {
        stagingWritePending=true;stagingWritePort=std::uint8_t(a);stagingWriteData=Z80_GET_DATA(pins);
    }
    const bool firstIo=io && !n.ioActive;
    if(firstIo)n.ioUnknownObserved=false;
    if(scanner) {
        const bool active=io&&bool(pins&(Z80_RD|Z80_WR));
        scannerControls.driveBus(active,std::uint8_t(a),bool(pins&Z80_WR),{Z80_GET_DATA(pins),255});
        if(firstIo && !(a&64) && (a&0x30)!=0x30)++scannerControls.overlappingBusCycles;
    }
    if(io && !ctcSelected) {
        if(pins&Z80_RD) {
            if(!n.ioActive) n.ioValue=input(n,std::uint8_t(a));
            else if(!scanner && (a&0xf8)==0x08) n.ioValue=sampleReadValue(n);
            else if(scanner && (a&0x7e)==0x34) {
                // ADC output is a live enabled latch during /RD, not a byte
                // frozen at the beginning of a multi-clock bus transaction.
                const auto value=scannerControls.adc.bus();
                n.ioValue=value.value|std::uint8_t(~value.known);
                if(value.known!=255&&!n.ioUnknownObserved){++scannerControls.unknownCpuReads;n.ioUnknownObserved=true;}
            }
            Z80_SET_DATA(pins,n.ioValue);
        } else if((pins&Z80_WR)&&!n.ioActive && !stagingSelected) output(n,std::uint8_t(a),Z80_GET_DATA(pins));
        if(diagnosticsEnabled && !n.ioActive&&trace) trace(scanner,pins&Z80_WR,a,Z80_GET_DATA(pins),n.cpu.pc);
    }
    auto ctcPins=pins&Z80_PIN_MASK;
    // Preserve the CTC's own held request, not the previous aggregate INT:
    // PIO requests must not leak upstream into the CTC core's persistent pin.
    ctcPins=(ctcPins&~Z80_INT)|(n.ctc.pins&Z80_INT)|Z80_IEIO;
    if(!scanner) {
        ctcPins=diskSio.daisy(ctcPins);
        ctcPins=diskDma.daisy(ctcPins);
        if(!(ctcPins&Z80_IEIO))ctcPins&=~Z80_INT;
        else {
            // Restore a CTC request suppressed while the upstream SIO was
            // in service. Vendor REQUESTED state otherwise relies on old INT.
            if(runtimeDevices_) {if(n.ctcEvents.requestedBeforeService())ctcPins|=Z80_INT;}
            else for(const auto& ch:n.ctc.chn) {
                if(ch.int_state&Z80CTC_INT_SERVICED)break;
                if(ch.int_state&Z80CTC_INT_REQUESTED){ctcPins|=Z80_INT;break;}
            }
        }
    }
    if(io && ctcSelected) ctcPins|=Z80CTC_CE|std::uint64_t(a&3)*Z80CTC_CS0;
    if(firstIo && ctcSelected) {
        if constexpr(diagnosticsEnabled) {
            if(pins&Z80_RD) ++n.ins[std::uint8_t(a)];
            else ++n.outs[std::uint8_t(a)];
        }
    }
    if(scanner) {
        if(ticks&1) ctcPins|=Z80CTC_CLKTRG2;
        if(scannerControls.adc.eoc.known&&scannerControls.adc.eoc.high)ctcPins|=Z80CTC_CLKTRG3;
        if(scannerSmpteClockHigh) ctcPins|=Z80CTC_CLKTRG1;
    } else {
        // CT1 pin22: traced SERINT on enlarged7-13/16; CT2 pin21 is open.
        // Existing diagnostic byte probe remains separate from host MIDI.
        if(!midiSerial.ready && !serialReady)ctcPins|=Z80CTC_CLKTRG1;
    }
    auto result=runtimeDevices_?n.ctcEvents.tick(n.ctc,ctcPins,ticks):z80ctc_tick(&n.ctc,ctcPins);
    if(firstIo && ctcSelected) {
        if(diagnosticsEnabled && trace) trace(scanner,pins&Z80_WR,a,Z80_GET_DATA(result),n.cpu.pc);
    }
    n.ioActive=io;
    n.pins=pioInterrupts(n,result)&Z80_PIN_MASK;
    if(!scanner&&(diskSio.requestLine()||diskDma.requestLine()))n.pins|=Z80_INT;
    if(!scanner) {
        // 7-12 local ROM/SRAM are direct. DRAM cycles use IC103/WAIT when
        // enabled; without both PROMs the legacy direct probe mode remains.
        const auto address=physical(a,dmaBus); // one page lookup for both uses below
        const bool dram=(n.pins&Z80_MREQ) && address>=0x2800;
        channelTimers.controllerTiming.driveCpuBus({true,!dram},{true,!(n.pins&Z80_RD)});
        if(cpuDramEnabled) {
            const bool active=dram && !(n.pins&Z80_RFSH) && (n.pins&(Z80_RD|Z80_WR));
            if(!active || !dramBusActive || address!=dramBusAddress)dramServiced=false;
            dramBusActive=active;dramBusAddress=address;
            channelTimers.controllerTiming.driveDramCpu(active,address,{Z80_GET_DATA(n.pins),255},bool(n.pins&Z80_RD));
        }
    }
}
void Machine::step(Node& n) {if(n.scanner)stepNode<true>(n);else stepNode<false>(n);}
void Machine::run(std::uint64_t cycles) {
    channelTimers.controllerTiming.requireValidProduction(ticks);
    if(runtimeDevices_ && diskRead)diskRead->enableEventTiming();
    while(cycles--) {
        ++ticks;
        if(!serialReady && ticks>=serialNext && !serialRx.empty()) {
            serialByte=serialRx.front();serialRx.pop_front();serialReady=true;
        }
        midiWire.advance(ticks,midiSerial);
        if(floppyQuiet_){floppySkipped_=true;if(!(ticks&4095))floppy.advance(ticks);} // exact lazy mechanics; 1 ms refresh keeps spin state observable
        else {
            if(floppySkipped_){floppySkipped_=false;if(diskRead)diskRead->skipQuiet(ticks-1);}
            updateFloppy();
            if(diskRead)diskRead->advanceCpuBoundary(ticks,floppy,diskSio);
        }
        if(dmaBus_&&!diskDma.granted) {
            const bool interrupt=diskSio.requestLine()||diskDma.requestLine()||bool(main.pins&Z80_INT);
            main.pins=(cpuResumePins_&~(Z80_INT|Z80_RETI))|(interrupt?Z80_INT:0);
            main.cpu.int_bits=(main.cpu.int_bits&~Z80_INT)|(main.pins&Z80_INT);
            dmaBus_=false;
        }
        // The pinned Z80 has no BUSREQ/BUSACK engine. This explicit adapter
        // grants only with no active CPU memory/I/O cycle or pending strobe.
        // CPU state/pins are saved; scanner/controller/interrupt clocks run.
        if(!dmaBus_&&diskDma.request(dmaReadyN())&&
           !(main.pins&(Z80_MREQ|Z80_IORQ))&&!stagingWritePending) {
            cpuResumePins_=main.pins&~Z80_RETI;diskDma.grant();dmaBus_=true;
        }
        if(hardDisk||diskDma.floppyIo)diskDma.clockReady(dmaReadyN());
        scannerControls.advanceCpuTick(); // source clock settles before coincident CPU bus edges
        stepNode<false>(main);stepNode<true>(scanner);pioLinkTick();
        if(playback_
#ifdef EII_PLAYBACK_TIMING_LAB
            ||playbackTimingObserver_
#endif
        ) channelTimers.advanceCpuTickObserved([this](std::uint64_t edge) {
#ifdef EII_PLAYBACK_TIMING_LAB
            if(playbackTimingObserver_)playbackTimingObserver_(edge);
            if(!playback_)return;
#endif
            std::array<LogicLevel,8> phi1,phi2;std::array<KnownByte,8> program;
            for(unsigned ch=0;ch<8;++ch) {
                phi1[ch]=channelTimers.channels[ch].phi1();phi2[ch]=channelTimers.channels[ch].phi2();
                program[ch]=reconstructionControl[ch].activeCode();
            }
            playback_->advance(edge,playbackAnalogInputs(),phi1,phi2,program);
        });
        else channelTimers.advanceCpuTick();
        channelTimers.controllerTiming.requireValidProduction(ticks);
        // Quiet = IMODE known 0 and the SAR idle: exactly static, skipped. Only
        // a main PIO write can end it (ISMPL presets are replayed from counts).
        // Off the 6-tick grid only a -PCSRIN strobe (IC10b /CLR) can matter.
        if(!sampleQuiet_){if(!(ticks%6))sampleInputLifecycleTick();else if(sampleReadSelected(main.pins))sampleSar.readStrobe(channel0Mode());}
        else if(!(ticks&4095) && sampleInput)sampleInput->idle(ticks);
        if(runtimeAudio_&&runtimeAudio_->nativeAudioEnabled())runtimeAudio_->nativeAdvance(channelTimers.halfClocks);
        else if(runtimeAudio_ && runtimeAudio_->clock()) {
            std::array<analog::KnownSignal,8> frequency;
            for(unsigned ch=0;ch<8;++ch) {
                const auto& cv=controlVoltage.held[ControlVoltageBus::Frequency][ch];
                frequency[ch]={cv.volts,cv.known};

            }
            std::array<KnownByte,8> reconstructionCodes{};
            if(runtimeAudio_->reconstructionEnabled())for(unsigned ch=0;ch<8;++ch)reconstructionCodes[ch]=reconstructionControl[ch].activeCode();
            if(analog::workerAudioMode(runtimeAudio_->mode())) {
                std::array<analog::KnownSignal,8> resonance;
                for(unsigned ch=0;ch<8;++ch) {
                    const auto& q=controlVoltage.held[ControlVoltageBus::Resonance][ch];
                    resonance[ch]={q.volts,q.known};
                }
                runtimeAudio_->sample(playbackAnalogInputs(),frequency,channel0AudioEnabled(),resonance,reconstructionCodes);
            } else runtimeAudio_->sample(playbackAnalogInputs(),frequency,channel0AudioEnabled(),{},reconstructionCodes);
        }
        if(cpuDramEnabled && dramBusActive && !dramServiced) {
            const auto& controller=channelTimers.controllerTiming;
            std::uint32_t address;KnownByte data;
            if(controller.cpuDramWrite(address,data)) {
                memory[address]=data.value;dramKnown[address]=data.known;
                dramServiced=true;if(dmaBus_)++dmaDramWrites;else ++dramWriteCycles;
            } else if((main.pins&Z80_RD) && controller.cpuDramReady() && controller.cpuDramReadData().known==255) {
                dramServiced=true;if(dmaBus_)++dmaDramReads;else ++dramReadCycles;
            }
        }
        if(dmaBus_&&diskDma.granted) {
            const auto& controller=channelTimers.controllerTiming;
            const bool wait=cpuDramEnabled&&dramBusActive&&
                (!dramServiced||!controller.cpuDramReady()||!controller.observedWait.known||!controller.observedWait.high);
            if(wait)++dmaDramWaitClocks;
            const auto data=cpuDramEnabled&&dramBusActive&&(main.pins&Z80_RD)?controller.cpuDramReadData().value:Z80_GET_DATA(main.pins);
            diskDma.tick(wait,data);
        }
    }
    if(runtimeAudio_)runtimeAudio_->flush();
    channelTimers.flushEventTiming();
    channelTimers.controllerTiming.materializeProductionObservation();
    if(runtimeDevices_) {main.ctcEvents.flush(main.ctc);scanner.ctcEvents.flush(scanner.ctc);}
}
void Machine::enableClockedPlayback(analog::ClockedPlaybackOptions options,analog::ClockedPlayback::Sink sink) {
    playback_=std::make_unique<analog::ClockedPlayback>(options,channelTimers.halfClocks,std::move(sink));
}
void Machine::nativeAudio(bool enabled){
    nativeMonitorSent_=false;
    channelTimers.audioSink({});if(!runtimeAudio_)return;
    runtimeAudio_->nativeAudio(enabled,channelTimers.halfClocks);if(!runtimeAudio_->nativeAudioEnabled())return;
    for(unsigned bank=0;bank<3;++bank)for(unsigned ch=0;ch<8;++ch){const auto& v=controlVoltage.held[bank][ch];
        runtimeAudio_->nativeEvent(channelTimers.halfClocks,{0,std::uint8_t(analog::NativeEvent::Amp+bank),std::uint8_t(ch),v.code,std::uint8_t(v.known?255:0),0});}
    for(unsigned ch=0;ch<8;++ch){const auto v=reconstructionControl[ch].activeCode();
        runtimeAudio_->nativeEvent(channelTimers.halfClocks,{0,analog::NativeEvent::Switch,std::uint8_t(ch),v.value,v.known,0});}
    runtimeAudio_->nativeEvent(channelTimers.halfClocks,{0,analog::NativeEvent::Mode,0,std::uint8_t(channel0AudioEnabled()),255,0});
    channelTimers.audioSink([this](auto edge,unsigned ch,KnownByte code,std::uint8_t phase){
        // 7-43/7-44 monitor: with IMODE=1 the channel-0 DAC input is the SAR
        // bus (IC12), i.e. the completed conversion at the deglitch phases.
        if(ch==0&&!sampleQuiet_){const auto mode=channel0Mode();if(mode.known&&mode.high)code=channel0DacBus();}
        runtimeAudio_->nativeEvent(edge,{0,analog::NativeEvent::Pins,std::uint8_t(ch),code.value,code.known,phase});});
}
void Machine::nativeControls(){
    if(!runtimeAudio_||!runtimeAudio_->nativeAudioEnabled()||!controlVoltage.muxKnown)return;
    const unsigned ch=controlVoltage.muxLatch&7;
    for(unsigned bank=0;bank<3;++bank)if(!(controlVoltage.muxLatch&(1u<<(bank+3)))){const auto& v=controlVoltage.held[bank][ch];
        runtimeAudio_->nativeEvent(channelTimers.halfClocks,{0,std::uint8_t(analog::NativeEvent::Amp+bank),std::uint8_t(ch),v.code,std::uint8_t(v.known?255:0),0});}
}
analog::PlaybackInputs Machine::playbackAnalogInputs() const noexcept {
    analog::PlaybackInputs out;
    for(unsigned ch=0;ch<8;++ch) {
        out.codes[ch]=channelTimers.controllerTiming.channelDacCode(ch);
        const auto& cv=controlVoltage.held[ControlVoltageBus::Amplitude][ch];
        out.amplitudeCv[ch]={cv.volts,cv.known};
    }
    // IMODE=1: IC12 drives the DAC (7-43). The host engines receive the bus
    // value the 3202 deglitch samples (latched at channel-0 phi1), not SAR
    // trial codes between deglitch samples.
    {const auto mode=channel0Mode();if(mode.known&&mode.high)out.codes[0]=sampleMonitorCode_;}
    return out;
}
PlaybackSwitchInputs Machine::playbackSwitchInputs() const {
    PlaybackSwitchInputs out;
    for(unsigned ch=0;ch<8;++ch) {
        out.phi1[ch]=channelTimers.channels[ch].phi1();
        out.phi2[ch]=channelTimers.channels[ch].phi2();
        out.reconstructionCode[ch]=reconstructionControl[ch].activeCode();
        out.reconstructionCutoffHz[ch]=reconstructionControl[ch].cutoff();
    }
    return out;
}
std::string Machine::report() const {
    std::ostringstream o;o<<"EII digital prototype; simulated CPU clocks="<<ticks<<"\n";
    for(const auto* n:{&main,&scanner}) {
        o<<(n->scanner?"scanner":"main")<<" PC="<<std::hex<<std::setw(4)<<std::setfill('0')<<n->cpu.pc<<" SP="<<n->cpu.sp<<" BC="<<n->cpu.bc<<" DE="<<n->cpu.de<<" HL="<<n->cpu.hl<<" I="<<unsigned(n->cpu.i)<<std::dec<<" HALT="<<bool(n->pins&Z80_HALT)<<"\n";
        if constexpr(diagnosticsEnabled) {
        std::vector<std::pair<std::uint64_t,unsigned>> hot;
        for(unsigned p=0;p<65536;++p) if(n->fetched[p]) hot.emplace_back(n->fetched[p],p);
        std::sort(hot.rbegin(),hot.rend());o<<"hot PCs:";
        for(unsigned i=0;i<std::min<unsigned>(8,hot.size());++i) o<<" "<<std::hex<<hot[i].second<<std::dec<<":"<<hot[i].first;
        o<<"\nIO:";for(unsigned p=0;p<256;++p) if(n->ins[p]||n->outs[p]) o<<" "<<std::hex<<p<<std::dec<<"(r"<<n->ins[p]<<",w"<<n->outs[p]<<")";o<<"\n";
        }
        o<<"PIO modes="<<unsigned(n->pio[0].mode)<<","<<unsigned(n->pio[1].mode)<<"\n";
        o<<"PIO A/B IRQ ACK="<<n->pioAcks[0]<<","<<n->pioAcks[1]<<"\n";
    }
    o<<"LCD bus writes="<<lcd.writes<<" display="<<lcd.display<<"\n["<<lcd.line(0)<<"]\n["<<lcd.line(1)<<"]\n";
    o<<"PIO transfers main->scanner="<<mainToScanTransfers<<" scanner->main="<<scanToMainTransfers<<"\n";
    const auto& controls=scannerControls;
    o<<"Scanner ADC0809 nominal clocks="<<controls.adc.clocks<<" starts="<<controls.adc.starts
        <<" completed="<<controls.adc.completed<<" aborted="<<controls.adc.aborted
        <<" channel value/mask="<<unsigned(controls.adc.channel.value)<<"/"<<unsigned(controls.adc.channel.known)
        <<" result value/mask="<<unsigned(controls.adc.result.value)<<"/"<<unsigned(controls.adc.result.known)<<" EOC=";
    if(controls.adc.eoc.known)o<<controls.adc.eoc.high;else o<<"unknown";
    o<<" unknown CPU reads="<<controls.unknownCpuReads<<" overlapping decoder cycles="<<controls.overlappingBusCycles
        <<" LED captures="<<controls.ledWrites<<" (nominal64-clock SAR; external parked controls, not measured calibration)\n";
    for(unsigned i=0;i<2;++i) {
        o<<"Floppy "<<i<<" ";
        if(!floppy.drives[i]){o<<"absent\n";continue;}
        const auto& d=*floppy.drives[i];
        o<<"SA465 reference cylinder="<<d.cylinder<<" position valid="<<d.positionValid<<" phaseA="<<d.zeroPhaseA
            <<" selected="<<d.selected()<<" spinning="<<d.spinning()<<" sensor-only disk="<<d.disk
            <<" step/out/in="<<d.steps<<"/"<<d.outSteps<<"/"<<d.inSteps<<" early="<<d.earlySteps
            <<" timing violations="<<d.timingViolations<<" index pulses="<<d.indexPulses
            <<" TRACK00_n="<<d.trackZeroN()<<" INDEX_n="<<d.indexN()<<" READY_n="<<d.readyN()<<"\n";
    }
    o<<"Disk SIO Bisync8x1/modem ext/status transitions="<<diskSio.statusTransitions
        <<" IRQ ACK A/B="<<diskSio.acknowledgements[0]<<"/"<<diskSio.acknowledgements[1]
        <<" unsupported data/status reads="<<diskSio.unsupportedReads<<" writes="<<diskSio.unsupportedWrites
        <<"\n";
    const auto& rx=diskSio.channels[0].rx;
    o<<"SIO-A RX clocks="<<rx.clocks<<" sync="<<rx.matches<<" chars="<<rx.characters<<" FIFO="<<rx.depth<<" overruns="<<rx.overruns<<" unsupported clocks="<<rx.unsupportedClocks<<" CRC="<<rx.crc<<" CRC-known="<<rx.crcKnown<<" RR1="<<unsigned(rx.status)<<" Ready_n="<<diskSio.readyN()<<"\n";
    o<<"Disk read path="<<(diskRead?"enabled nominal image pulses":"detached / sensor-only")<<" DMA transfers="<<diskDma.transfers<<" grants="<<diskDma.grants<<" ACK="<<diskDma.acks<<" enabled="<<diskDma.enabled<<" supported="<<diskDma.supported()<<" unsupported="<<diskDma.unsupported<<" DMA DRAM W/R/WAIT="<<dmaDramWrites<<"/"<<dmaDramReads<<"/"<<dmaDramWaitClocks<<"\n";
    if(diskRead)o<<"Media source edges="<<diskRead->sourceEdges<<" read pulses="<<diskRead->outputPulses<<" separator clocks="<<diskRead->separator.samples<<" (nominal pulse/idle experiment; SIO counters above reset on channel reset)\n";
    o<<"S3528 hardware control (code/known-mask, cutoff Hz):";
    for(unsigned ch=0;ch<8;++ch){const auto code=reconstructionControl[ch].activeCode();const auto fc=reconstructionControl[ch].cutoff();
        o<<" ch"<<ch<<"="<<unsigned(code.value)<<"/"<<unsigned(code.known)<<":";
        if(fc.known)o<<fc.value;else o<<"unknown";}
    o<<'\n';
    o<<"Control DAC writes="<<controlVoltage.dacWrites<<" mux writes="<<controlVoltage.muxWrites
        <<" known ideal hold voltages="<<controlVoltage.knownCount()<<"/24\n";
    for(unsigned channel=0;channel<8;++channel) {
        if(!controlVoltage.held[0][channel].known && !controlVoltage.held[1][channel].known && !controlVoltage.held[2][channel].known) continue;
        o<<"CV channel "<<channel<<" ACV/FCV/QCV:";
        for(unsigned bank=0;bank<3;++bank) {
            const auto& h=controlVoltage.held[bank][channel];
            if(h.known) o<<" "<<h.volts<<"V(code="<<unsigned(h.code)<<")";
            else o<<" unknown";
        }
        o<<"\n";
    }
    o<<"8254 input clocks="<<channelTimers.sourceClocks<<" writes="<<channelTimers.writes
        <<" unsupported commands="<<channelTimers.unimplementedCommands<<"\n";
    for(unsigned i=0;i<9;++i) {
        const auto& c=channelTimers.counters[i];
        o<<"8254 "<<(i==8?"SIOCK":"channel")<<" "<<i<<" mode=";
        if(c.configured)o<<unsigned(c.mode);else o<<"unknown";
        o<<" supported="<<c.supported<<" reload="<<c.reload<<" count="<<c.count<<" OUT=";
        if(c.outputKnown)o<<c.output;else o<<"unknown";
        o<<" rising="<<c.risingEdges<<" falling="<<c.fallingEdges<<" null="<<c.nullCount<<"\n";
    }
    for(unsigned i=0;i<8;++i) {
        const auto& c=channelTimers.channels[i];
        o<<"Channel request "<<i<<" divider known="<<c.dividerKnown()
            <<" clocks="<<c.requestClocks<<" ambiguous="<<c.ambiguousRequestClocks
            <<" last source CLK="<<c.lastRequestClock<<" CHRQ=";
        if(c.request.known)o<<c.request.high;else o<<"unknown (controller drive unknown)";
        o<<"\n";
    }
    const auto& service=channelTimers.serviceBus;
    o<<"Channel service CHSL known mask="<<unsigned(service.selectionKnown)<<" UREQ=";
    if(service.ureq.known)o<<service.ureq.high;else o<<"unknown";
    o<<" (PROM upper/lower loaded="<<channelTimers.controllerTiming.hasUpperProm()
        <<"/"<<channelTimers.controllerTiming.hasLowerProm()<<")\n";
    const auto& timing=channelTimers.controllerTiming;
    o<<"Controller timing rising edges="<<timing.risingEdges<<" half edges="<<channelTimers.halfClocks
        <<" startup trajectories="<<timing.trajectoryCount()<<" CHSL value/mask="
        <<unsigned(timing.channel.value)<<"/"<<unsigned(timing.channel.known)
        <<" UCNT value/mask="<<unsigned(timing.microCount.value)<<"/"<<unsigned(timing.microCount.known)<<"\n";
    o<<"Correlated upper state branches="<<timing.upperBranchCount()<<" (all startup phases retained)\n";
    o<<"PROM clocks="<<timing.promClocks<<" control value/mask="<<timing.control.value<<"/"<<timing.control.known
        <<" request pipeline value/mask="<<unsigned(timing.requestPipeline.value)<<"/"<<unsigned(timing.requestPipeline.known)
        <<" observed WAIT_n=";
    if(timing.observedWait.known)o<<timing.observedWait.high;else o<<"unknown";
    o<<(cpuDramEnabled?" (CPU DRAM/WAIT adapter enabled)\n":" (legacy direct memory; WAIT observation only)\n");
    o<<"CPU DRAM completed reads="<<dramReadCycles<<" writes="<<dramWriteCycles
        <<" WAIT hold clocks="<<dramWaitClocks<<" active="<<dramBusActive
        <<" ready="<<channelTimers.controllerTiming.cpuDramReady()<<"\n";
    const auto sampleBus=timing.channelBusValue();
    o<<"Sample IC40 CHDB value/mask="<<unsigned(sampleBus.value)<<"/"<<unsigned(sampleBus.known)<<" DAC stored codes=";
    for(unsigned i=0;i<8;++i){const auto code=timing.channelDacCode(i);o<<unsigned(code.value)<<"/"<<unsigned(code.known)<<" ";}
    o<<"(digital latches; channel0 MODE/SAR and analog conversion absent)\n";
    o<<"CPU register staging address value/mask="<<unsigned(registerStaging.address.value)<<"/"<<unsigned(registerStaging.address.known)
        <<" data value/mask="<<registerStaging.data.value<<"/"<<registerStaging.data.known<<" writes=";
    for(auto count:registerStaging.writes)o<<count<<" ";
    o<<"(CPU latches; IC92/RFWR and ALU connected)\n";
    const auto writes=timing.certainRegisterWriteRange();
    o<<"Register SRAM certain write apertures per trajectory min/max="<<writes[0]<<"/"<<writes[1]
        <<" fully known cells across trajectories="<<timing.fullyKnownRegisters()<<"/64"
        <<" (scheduled RFD/ALU writeback)\n";
    const auto rfd=timing.dataPipelineValue();const auto flags=timing.delayedFlagValue();
    const auto four=timing.delayedUcd4();
    o<<"RFD pipeline value/mask="<<rfd.value<<"/"<<rfd.known
        <<" IC141 flag value/mask="<<unsigned(flags.value)<<"/"<<unsigned(flags.known)<<" UCD04.1B=";
    if(four.known)o<<four.high;else o<<"unknown";
    o<<" (old RFD bus; raw PROM control4)\n";
    const auto alu=timing.aluOutputValue();
    o<<"ALU output latch value/mask="<<alu.value<<"/"<<alu.known<<" (20-bit sum, carry and flag wiring)\n";
    o<<"SIO CRC model: WR3 D3 start/restart (UM0081 printed250-251/283); a valid image block is accepted, a corrupted one is still rejected.\n";
    if(hardDisk)o<<"HDD: optional functional target / nominal REQ / continuous IO DMA; AE343 3D-reset and 3F-arm behavior unverified; no physical MFM/ECC model.\n";
    o<<"UNIMPLEMENTED: other SIO formats/TX interrupts and general DMA search/burst/read-register modes outside the floppy subset, physical BUSACK/subcycle calibration, sample-recording/SAR DRAM writes, calibrated sample DAC/filter/VCA, PIO mode-0/1 strobes and mode-3 bit interrupts.\n";
    if(!serialTx.empty()) o<<"2651 TX (register-level capture, no serial-bit timing):\n"<<serialTx<<"\n";
    return o.str();
}
}


namespace eii {
// 7-43 SAR/logic per CPU tick. While IMODE is known low and the register is
// idle the model is exactly static and skipped; IC10a's ISMPL presets during
// that time are replayed from channel 0's pulse counter when tracking resumes.
[[gnu::noinline,gnu::cold]] void Machine::sampleInputLifecycleTick() {
    const auto imode=channel0Mode();
    // A live host source is drained (about every 1 ms) while not sampling.
    if(sampleInput && !(imode.known&&imode.high) && !(ticks%6144))sampleInput->idle(ticks);
    if(!(imode.known&&imode.high)&&sampleAnalogActive_) {
        sampleAnalogActive_=false; // C27 discharges (tau 0.1 s); settled value returned to the ADC pin
        scannerControls.adc.setInput(unsigned(ScannerAnalogInput::Envelope),{0,true});
    }
    if(!(imode.known&&imode.high))publishSamplingMonitor(imode);
    if(sampleSar.quiescent(imode)){sampleTracking_=false;sampleQuiet_=true;return;}
    // Evaluation grid: every 6 CPU ticks = both C333K edges (t%12 = 0, 6).
    // +ISMPL, phi1 and phi2 each last 16 source clocks (6.4 ticks), so every
    // pulse is seen; 144 ticks/sample is a multiple of 6, so the hold and
    // phase instants keep a fixed relation to the channel-0 divider.
    // (The caller only calls on the grid; off-grid read strobes go to
    // SampleSar::readStrobe, the only asynchronous input shorter than 6 ticks.)
    auto& ch0=channelTimers.channels[0];
    LogicLevel ismpl{};
    if(imode.known) {
        // Sync the lazily timed dividers only when channel 0's phase outputs
        // can have changed (16-clock divider states, or any timer event).
        if(!sampleTracking_||channelTimers.runtimeEvents!=sampleFlushEvents_||channelTimers.sourceClocks>=sampleFlushAt_) {
            channelTimers.flushEventTiming();
            const auto d=ch0.clocksUntilSamplingEvent();
            sampleFlushAt_=d==UINT64_MAX?UINT64_MAX:channelTimers.sourceClocks+d;sampleFlushEvents_=channelTimers.runtimeEvents;
        }
        if(!sampleTracking_) {
            // IC10a presets while skipped: every channel-0 request edge (second
            // count 4) is followed 16 source clocks later by +ISMPL (count 5)
            // because the 8254 period (>=128 clocks used by OS3.1: 360) lets
            // the divider reach count 8. A request edge since the last tracked
            // tick therefore implies a preset (at most 1.6 us early if the
            // edge is that recent). Unresolved request history -> unknown.
            if(ch0.ambiguousRequestClocks!=sampleAmbiguousSeen_)sampleSar.presetHistory({});
            else if(ch0.requestClocks!=sampleIsmplSeen_)sampleSar.presetHistory({true,true});
            sampleTracking_=true;
        }
        ismpl=ch0.samplingPulse();
        {const auto p1=ch0.phi1();if(!p1.known)sampleMonitorCode_={};else if(p1.high)sampleMonitorCode_=channel0DacBus();}
        sampleIsmplSeen_=ch0.requestClocks;sampleAmbiguousSeen_=ch0.ambiguousRequestClocks;
    } else sampleTracking_=false;
    LogicLevel comparator{};
    if(sampleComparatorExternal)comparator=sampleComparator;
    else if(imode.known&&imode.high) {
        const auto& acv=controlVoltage.held[ControlVoltageBus::Amplitude][0];
        // Quiet start (documented assumption): the analog model begins at the
        // first tick with a fully known DAC bus and ENCODE after IMODE=1.
        if(!sampleAnalogActive_ && channel0DacBus().known==255 && sampleSar.encode(imode).known)
            {sampleAnalog.reset({acv.volts,acv.known},ticks);sampleAnalogActive_=true;}
        if(sampleAnalogActive_) {
        analog::SampleInputAnalog::Pins pins{scannerPioBPin(7),scannerPioBPin(6),ismpl,ch0.phi1(),ch0.phi2(),
            reconstructionControl[0].activeCode(),{acv.volts,acv.known}};
        comparator=sampleAnalog.tick(pins,sampleInput.get(),ticks,channel0IdacVolts(sampleAnalog.referenceCv()));
        // +IENV.V -> R26 1k -> scanner ADC0809 IN1 (7-8).
        if(ticks%48==0){const auto e=sampleAnalog.envelope(); /* every 12 us */ scannerControls.adc.setInput(unsigned(ScannerAnalogInput::Envelope),{std::min(e.value,5.0),e.known});}
        }
    }
    const SampleSar::Inputs in{imode,ismpl,{true,!sampleReadSelected(main.pins)},comparator};
    sampleSar.cpuTick(ticks,in);
    if(imode.known&&imode.high)publishSamplingMonitor(imode);
}
void Machine::publishSamplingMonitor(LogicLevel mode){
    if(!runtimeAudio_||!runtimeAudio_->nativeAudioEnabled())return;
    const bool active=mode.known&&mode.high;
    const auto value=active&&sampleAnalogActive_?sampleAnalog.iint():analog::KnownSignal{};
    if(nativeMonitorSent_&&active==nativeMonitorActive_&&value.known==nativeMonitorValue_.known&&(!value.known||value.value==nativeMonitorValue_.value))return;
    analog::NativeEvent e{0,analog::NativeEvent::MonitorIint,0,std::uint8_t(active),std::uint8_t(value.known?255:0),0};e.analogBits=std::bit_cast<std::uint32_t>(float(value.value));
    runtimeAudio_->nativeEvent(channelTimers.halfClocks,e);
    nativeMonitorSent_=true;nativeMonitorActive_=active;nativeMonitorValue_=value;
}
[[gnu::noinline,gnu::cold]] std::uint8_t Machine::sampleReadValue(Node& n) {
    const auto q=sampleSar.register_(); // IC13 74HCT244 -> PD7..0
    if(q.known!=255 && !n.ioUnknownObserved){++sampleUnknownReads;n.ioUnknownObserved=true;}
    return q.value|std::uint8_t(~q.known); // unresolved bits exposed as1 at the Z80 boundary
}
// 7-13: -INPRDY (IC22 7407) is wired-OR with the existing DMA ready source.
bool Machine::dmaReadyN() {
    const bool hdReadyN=!hardDisk||!hardDisk->dmaArmed||!hardDisk->dataRequest(ticks);
    if(sampleQuiet_)return diskSio.readyN()&&hdReadyN; // IC10b held clear by IMODE=0
    const auto r=sampleSar.readyN();
    if(!r.known){if(diskDma.enabled)++sampleReadyUnknown;return diskSio.readyN()&&hdReadyN;}
    return diskSio.readyN()&&hdReadyN&&r.high;
}
}
namespace eii {
// -IWAIT -> -PWAIT (7-10 IC84) for a main-CPU read of 08h..0Fh before EOC.
[[gnu::noinline,gnu::cold]] bool Machine::sampleWaitStep(Node& n,bool dramHold) {
    bool sampleWait=false;
    if(sampleReadSelected(n.pins)) {
        const auto w=sampleSar.waitN(channel0Mode(),{true,false});
        if(!w.known)++sampleWaitUnknown; // unresolved: not held, counted
        sampleWait=w.known&&!w.high;
    }
    if(sampleWait){n.pins|=Z80_WAIT;++sampleWaitClocks;sampleWaitPin_=true;return true;}
    if(sampleWaitPin_){sampleWaitPin_=false;if(!dramHold)n.pins&=~Z80_WAIT;}
    return false;
}
}

