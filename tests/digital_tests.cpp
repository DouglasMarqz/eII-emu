#include "machine.h"
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
static void require(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
int main() {
    try {
        auto concrete=std::make_unique<eii::Machine>(0);
        require(concrete->channelTimers.controllerTiming.trajectoryCount()==1,"constructor selects one controller phase");
        concrete->run(1);
        require(concrete->channelTimers.controllerTiming.microCount.value!=0,"selected controller phase leaves reset and clocks");
        concrete.reset();
        auto m=std::make_unique<eii::Machine>();
        m->main.rom[0]=0x5a;m->main.rom[8191]=0xa5;
        require(m->readMemory(false,0)==0x5a && m->readMemory(false,0x8000)==0x5a,"reset page mirror");
        m->writeMemory(false,0,0);require(m->readMemory(false,0)==0x5a,"ROM is immutable");
        m->writeMemory(false,0x2000,0x31);require(m->readMemory(false,0xa000)==0x31,"SRAM reset mirror");
        m->output(m->main,0x68,15);m->output(m->main,0x69,14);
        m->output(m->main,0x6a,2);m->output(m->main,0x6b,3);
        require(m->physical(0)==0x10000 && m->physical(0x8000)==0x18000,"CPU banks");
        require(m->physical(0,true)==0x78000 && m->physical(0x8000,true)==0x70000,"independent DMA banks");
        m->writeMemory(false,0x1234,0x77);require(m->memory[0x11234]==0x77,"banked write");
        m->scanner.rom[1]=0x34;require(m->readMemory(true,0x6001)==0x34,"scanner ROM mirrors");
        m->writeMemory(true,0x8000,0x12);require(m->readMemory(true,0xf800)==0x12,"scanner RAM mirrors");
        eii::PromInputs in;in.counter=7;in.channel3=true;in.ureq=true;in.preCarry=true;in.loop=true;in.decrement=true;in.backwards=true;
        m->prom.latched=0xc000;require(m->prom.upperAddress(in)==511,"IC135 feedback pins");
        require(m->prom.lowerAddress(in)==511,"IC137 address pins");
        m->prom.upper[511]=0x40;m->prom.lower[511]=0x32;
        require(m->prom.clock(in)==0x4032 && m->prom.upperAddress(in)==255,"PROM clock uses old feedback");
        auto run=std::make_unique<eii::Machine>();
        // Synthetic scanner program, written for this test, not firmware bytes.
        const std::uint8_t code[]={0x3e,0x01,0xd3,0xbe,0x3e,'T',0xd3,0xbf,0x3e,'E',0xd3,0xbf,0x3e,'S',0xd3,0xbf,0x3e,'T',0xd3,0xbf,0x76};
        std::copy(std::begin(code),std::end(code),run->scanner.rom.begin());run->run(1000);
        require(run->lcd.line(0).substr(0,4)=="TEST" && run->lcd.writes==5,"real CPU instructions drive LCD bus");
        auto serial=std::make_unique<eii::Machine>();
        const std::uint8_t receive[]={0xdb,0,0xd3,4,0x76};
        std::copy(std::begin(receive),std::end(receive),serial->main.rom.begin());
        serial->serialRx={'Q','R'};serial->run(1000);
        require(serial->serialTx=="Q" && !serial->serialReady,"serial read consumes one byte");
        serial->run(5000);require(serial->serialReady && serial->serialByte=='R',"serial byte pacing");
        auto timer=std::make_unique<eii::Machine>();
        // Real OUT/IN cycles configure IM2 and a periodic timer while DI.
        // Leave IRQ pending for many instructions before EI; ISR must repeat
        // after RETI. A one-clock INT pulse is lost during the DI interval.
        const std::uint8_t timed[]={
            0xf3,0x31,0xf0,0x27,0x3e,0x20,0xed,0x47,0xed,0x5e,
            0x3e,0xe0,0xd3,0x28,0x3e,0x87,0xd3,0x28,
            0x3e,0x40,0xd3,0x28,0xdb,0x28,0x32,0x02,0x21,
            0x06,0xff,0x10,0xfe,0xfb,0x76,0xc3,0x20,0x00};
        std::copy(std::begin(timed),std::end(timed),timer->main.rom.begin());
        const std::uint8_t isr[]={0xf5,0x3a,0x00,0x21,0x3c,0x32,0x00,0x21,0xf1,0xfb,0xed,0x4d};
        std::copy(std::begin(isr),std::end(isr),timer->main.rom.begin()+0x100);
        timer->writeMemory(false,0x20e0,0);timer->writeMemory(false,0x20e1,1);
        timer->run(2000);
        require(timer->main.ctc.chn[0].constant==0x40 && timer->main.ctc.chn[0].control==0x81,"CTC control/constant written once per CPU OUT");
        require(!eii::diagnosticsEnabled || (timer->main.outs[0x28]==3 && timer->main.ins[0x28]==1),"CTC bus access accounting");
        require(timer->readMemory(false,0x2102)>0 && timer->readMemory(false,0x2102)<=0x40,"CTC read held through CPU IN");
        require((timer->main.ctc.chn[0].int_state&Z80CTC_INT_REQUESTED) && (timer->main.pins&Z80_INT),"CTC IRQ held while CPU interrupts disabled");
        timer->run(18000);
        require(timer->readMemory(false,0x2100)>=10,"CTC IM2 IRQ acknowledgement and RETI repeat");
        auto link=std::make_unique<eii::Machine>();
        const auto setupPio=[&](eii::Node& n,std::uint8_t base) {
            link->output(n,base+3,0xcf);link->output(n,base+3,0xff);
            link->output(n,base+1,0x8f);
            link->input(n,base); // prime mode-2 input ready
        };
        setupPio(link->main,0x30);setupPio(link->scanner,0xdc);
        require(!link->main.pio[0].ready && link->main.pio[1].ready,"mode-2 ARDY output-empty and BRDY input-empty roles");
        link->output(link->main,0x30,0x5a);
        require(link->scanner.pio[0].input==255,"PIO write does not instantly cross-copy the bus");
        link->run(2);
        require(link->mainToScanPulse && !link->mainToScanTransfers && !link->scanner.pio[1].pending,"IC23 falling strobe captures before rising-edge IRQ");
        link->run(2);
        require(link->mainToScanTransfers==1 && link->scanner.pio[0].inputFull && !link->scanner.pio[1].ready,"IC23 rising strobe completes one transfer");
        require(link->main.pio[0].pending && link->scanner.pio[1].pending && !link->scanner.pio[0].pending,"mode-2 output ACK uses A and input IRQ uses B even when disabled");
        link->output(link->main,0x30,0xa5);link->run(8);
        require(link->mainToScanTransfers==1 && link->main.pio[0].ready,"unread receiver applies backpressure");
        require(link->input(link->scanner,0xdc)==0x5a,"first byte retained under backpressure");
        link->run(4);
        require(link->mainToScanTransfers==2 && link->input(link->scanner,0xdc)==0xa5,"next byte transfers after receiver read");
        // Both endpoints have data: the schematic grants main's direction
        // first, then scanner's once main ARDY drops and main BRDY is high.
        link->output(link->main,0x30,0x31);link->output(link->scanner,0xdc,0x72);
        link->run(4);
        require(link->mainToScanTransfers==3 && link->scanToMainTransfers==0,"main wins simultaneous bidirectional requests");
        link->run(4);
        require(link->scanToMainTransfers==1 && link->input(link->main,0x30)==0x72,"reverse transfer uses main BRDY and scanner ARDY");
        require(link->input(link->scanner,0xdc)==0x31,"simultaneous main byte retained");
        // Independent IM2 vectors for PIO A and B, pending while disabled,
        // A's priority, and successive RETI release are exercised by a CPU.
        auto irq=std::make_unique<eii::Machine>();
        const std::uint8_t pioCpu[]={0xf3,0x31,0xf0,0x27,0x3e,0x20,0xed,0x47,0xed,0x5e,0xfb,0x76,0xc3,0x0b,0x00};
        std::copy(std::begin(pioCpu),std::end(pioCpu),irq->main.rom.begin());
        const std::uint8_t pioIsrA[]={0xf5,0x3e,0xa1,0x32,0x00,0x22,0xf1,0xfb,0xed,0x4d};
        const std::uint8_t pioIsrB[]={0xf5,0x3e,0xb2,0x32,0x01,0x22,0xf1,0xfb,0xed,0x4d};
        std::copy(std::begin(pioIsrA),std::end(pioIsrA),irq->main.rom.begin()+0x100);
        std::copy(std::begin(pioIsrB),std::end(pioIsrB),irq->main.rom.begin()+0x120);
        irq->writeMemory(false,0x20e0,0);irq->writeMemory(false,0x20e1,1);
        irq->writeMemory(false,0x20e2,0x20);irq->writeMemory(false,0x20e3,1);
        irq->output(irq->main,0x31,0xe0);irq->output(irq->main,0x33,0xe2);
        irq->main.pio[0].pending=true;irq->main.pio[1].pending=true;
        irq->run(200);
        require(!irq->main.pioAcks[0] && !irq->main.pioAcks[1],"disabled PIO requests do not reach CPU");
        irq->output(irq->main,0x31,0x83);irq->output(irq->main,0x33,0x83);
        irq->run(40);
        require(irq->main.pioAcks[0]==1 && irq->main.pioAcks[1]==0,"PIO A priority blocks B until RETI");
        irq->run(1000);
        require(irq->main.pioAcks[0]==1 && irq->main.pioAcks[1]==1 && irq->readMemory(false,0x2200)==0xa1 && irq->readMemory(false,0x2201)==0xb2,"PIO A/B vectors and RETI release");
        require(!(irq->main.pins&Z80_INT) && !irq->main.pio[0].servicing && !irq->main.pio[1].servicing,"PIO IRQ does not contaminate CTC held INT after acknowledgement");
        // A simultaneous upstream CTC request must win, and its RETI must
        // release the downstream PIO without losing either source.
        std::copy(std::begin(pioIsrB),std::end(pioIsrB),irq->main.rom.begin()+0x140);
        irq->writeMemory(false,0x20e4,0x40);irq->writeMemory(false,0x20e5,1);
        irq->main.ctc.chn[0].int_vector=0xe4;
        irq->main.ctc.chn[0].int_state=Z80CTC_INT_REQUESTED;
        irq->main.ctc.pins|=Z80_INT;irq->main.pio[0].pending=true;
        irq->run(50);
        require((!eii::diagnosticsEnabled || irq->main.fetched[0x140]>0) && irq->main.pioAcks[0]==1,"CTC has priority over simultaneous PIO IRQ");
        irq->run(1000);
        require(irq->main.pioAcks[0]==2 && !irq->main.pio[0].servicing && !(irq->main.pins&Z80_INT),"CTC RETI releases PIO and both IRQ sources clear independently");
        irq->output(irq->main,0x33,0xb7);irq->output(irq->main,0x33,0xfe);
        require(irq->main.pio[1].interruptMask==0xfe && irq->main.pio[1].control==0xb7,"PIO interrupt mask is distinct from interrupt control and I/O direction");
        std::cout<<"PASS: memory/PROM/LCD/serial, CTC held IRQ/IM2/RETI, clocked mode-2 PIO transfer/backpressure/arbitration and A/B IRQ vectors/priority/RETI\n";
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}

