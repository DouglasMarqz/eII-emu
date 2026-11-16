#include "machine.h"
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void step(eii::Sa465Drive& d,std::uint64_t t,bool outward=true) {
    d.drive(false,false,true,outward,true,true,t-8);
    d.drive(false,false,false,outward,true,true,t-4);
    d.drive(false,false,true,outward,true,true,t);
}
int main(){try{
    eii::Sa465Drive d(3,400000);
    require(!d.spinning()&&d.trackZeroN()&&d.indexN()&&d.readyN(),"unselected drive releases all open-collector status lines");
    d.insertSensorDisk();d.drive(false,false,true,true,true,true,400000);
    step(d,400020);require(d.cylinder==2&&d.steps==1&&d.trackZeroN(),"head moves on STEP trailing edge, not falling");
    step(d,412020);step(d,424020);require(d.cylinder==0&&!d.trackZeroN()&&!d.dataSettled(),"logical TRACK00 and15ms data-settling guard are distinct");
    d.advance(436020);require(!d.trackZeroN(),"recalibration reaches settled physical cylinder0");
    step(d,440020);d.advance(452020);require(d.cylinder==0&&d.trackZeroN(),"extra outward step at stop changes phase and deasserts TRACK00");
    step(d,456020);d.advance(468020);require(!d.trackZeroN(),"second extra outward step restores phaseA and TRACK00");
    step(d,480020,false);d.advance(492020);require(d.cylinder==1&&d.trackZeroN(),"direction low moves inward");
    const auto accepted=d.steps;d.drive(false,false,false,false,true,true,500000);d.drive(false,false,true,false,true,true,500001);
    require(d.steps==accepted&&d.timingViolations==1,"sub-microsecond pulse rejected with diagnostic");
    eii::Sa465Drive early(37,400000);step(early,100);require(early.cylinder==37&&early.earlySteps==1,"explicit100ms power-up guard does not silently home head");
    for(unsigned start=0;start<80;++start) {
        eii::Sa465Drive home(start,123456);home.drive(false,false,true,true,true,true,400000);
        std::uint64_t time=400020;
        while(home.trackZeroN() && home.steps<81){step(home,time);time+=12000;}
        require(home.cylinder==0&&!home.trackZeroN()&&home.outSteps==start,"every explicit initial cylinder0..79 homes with exactly its distance");
    }
    // Spindle/READY from two actual modeled index crossings; selection
    // cannot reset phase or restart motor when common MOTOR remains active.
    eii::Sa465Drive rot(17,400000);rot.insertSensorDisk();rot.drive(false,false,true,true,true,true,400000);
    rot.advance(2399999);require(rot.readyN()&&rot.indexPulses==0,"no READY or index pulses before500ms spin-up");
    rot.advance(2400000);require(rot.indexPulses==1&&!rot.indexN()&&rot.readyN(),"first steady index at chosen rotor phase remains not ready");
    rot.advance(2408000);require(rot.indexN(),"2ms nominal index pulse terminates");
    rot.advance(3200000);require(rot.indexPulses==2&&!rot.readyN(),"second200ms-spaced index establishes READY");
    rot.advance(4000000);
    const auto phase=rot.phase();rot.drive(true,false,true,true,true,true,4000000);
    require(rot.readyN()&&rot.indexN()&&rot.phase()==phase,"deselection releases shared status without resetting rotor");
    rot.drive(false,false,true,true,true,true,4000000);require(!rot.readyN(),"reselecting running ready drive preserves state");
    rot.drive(true,true,true,true,true,true,4000000);rot.advance(15999999);require(rot.spinning(),"motor remains powered during3second stop delay");
    rot.advance(16000000);require(!rot.spinning(),"motor stops only after full electronic delay");
    rot.drive(false,false,true,true,true,true,16000000);require(rot.readyN(),"fresh motor start must reacquire index history");
    rot.eject();require(!rot.indexN()&&rot.readyN()&&rot.protectN(),"selected empty drive has documented constant-active INDEX, no READY or write protect");
    // Board port mapping: B4 selects0, B3 selects1, shared MOTOR=AND.
    eii::FloppyBus bus;bus.attach(0,2,400000);bus.attach(1,7,200000);
    bus.drives[0]->insertSensorDisk();bus.drives[1]->insertSensorDisk(false);
    bus.drive(0x0e,true,true,400000);
    require(bus.drives[0]->selected()&&!bus.drives[1]->selected()&&bus.drives[0]->spinning()&&bus.drives[1]->spinning(),"select0 isB4 low; motor line runs BOTH installed spindles");
    bus.drive(0x16,false,true,400100);require(!bus.drives[0]->selected()&&bus.drives[1]->selected()&&!bus.drives[1]->sideN(),"select1 isB3 low, independent side pin retained");
    // SIO register pointer and active-low input status, frozen until command.
    eii::DiskSio sio;
    sio.write(0x3b,2);sio.write(0x3b,0xe0);sio.write(0x3b,1);sio.write(0x3b,4);
    sio.write(0x39,1);sio.write(0x39,1);sio.inputs(false,false);
    const auto captured=sio.read(0x39);require((captured&0x2a)==0x2a&&sio.requestLine(),"CTS/DCD active-low pins assert RR0 status and external interrupt");
    sio.inputs(true,true);require((sio.read(0x3d)&0x28)==0x28,"short modem pulse is latched across aliased status read");
    auto pins=sio.daisy(Z80_IEIO|Z80_M1|Z80_IORQ);require(Z80_GET_DATA(pins)==0xea&&!(pins&Z80_IEIO)&&sio.acknowledgements[0]==1,"A external vectorEA wins acknowledge and blocks lower devices");
    sio.write(0x39,0x10);require((sio.read(0x39)&0x28)==0&&!sio.requestLine(),"Reset Ext/Status exposes current levels, clears pending");
    pins=sio.daisy(Z80_IEIO|Z80_RETI);require(!(pins&Z80_RETI)&&(pins&Z80_IEIO),"SIO RETI consumes only its own service");
    sio.write(0x39,5);sio.write(0x39,0x82);require(!sio.sideN()&&!sio.writeGateN(),"WR5 DTR and RTS drive physical side/write-gate levels");
    sio.write(0x39,0x18);require(sio.sideN()&&sio.writeGateN(),"channel reset deasserts output modem lines");
    require(!(sio.read(0x39)&1),"no fake received character exists without real bitstream");
    // Real Z80 OUT/IN homing loop after a clocked power-up wait, not an
    // injected controller result. Head starts37 and firmware polls TRACK00.
    auto m=std::make_unique<eii::Machine>();m->scanner.rom[0]=0x76;m->floppy.attach(0,37,400000);m->floppy.drives[0]->insertSensorDisk();
    std::vector<std::uint8_t> code={0xf3,0x31,0xf0,0x27,0x3e,0xcf,0xd3,0x33,0x3e,0x21,0xd3,0x33,
        0x3e,0x1e,0xd3,0x32,0x01,0x00,0x50}; // BC20480 wait
    const auto wait=code.size();code.insert(code.end(),{0x0b,0x78,0xb1,0x20,std::uint8_t(int(wait)-int(code.size()+5))});
    code.insert(code.end(),{0x3e,0x0e,0xd3,0x32});
    const auto loop=code.size();code.insert(code.end(),{0xdb,0x32,0xe6,1,0x28,0});const auto branch=code.size()-1;
    code.insert(code.end(),{0x3e,0x0a,0xd3,0x32,0x3e,0x0e,0xd3,0x32,0x01,0x00,0x02});
    const auto delay=code.size();code.insert(code.end(),{0x0b,0x78,0xb1,0x20,std::uint8_t(int(delay)-int(code.size()+5)),0xc3,std::uint8_t(loop),0});
    const auto done=code.size();code[branch]=std::uint8_t(done-(branch+1));code.insert(code.end(),{0x3e,0x55,0x32,0x00,0x21,0x76});
    std::copy(code.begin(),code.end(),m->main.rom.begin());m->run(1300000);
    require(m->memory[0x2100]==0x55&&m->floppy.drives[0]->cylinder==0&&m->floppy.drives[0]->outSteps==37,"real Z80 reaches TRACK00 from37 without host head reset");
    require(!m->floppy.drives[0]->timingViolations&&m->main.pio[1].mask==0x21,"real OUT pulse timing and PIO input/output direction honored");
    // Real IM2 arbitration with three simultaneous known requests. SIO is
    // physically upstream of DMA(passive here), CTC and PIO. CTC REQUESTED
    // must survive suppression while SIO is serviced, without INT leakage.
    auto irq=std::make_unique<eii::Machine>();irq->scanner.rom[0]=0x76;
    const std::uint8_t interruptProgram[]={0xf3,0x31,0xf0,0x27,0x3e,0x20,0xed,0x47,0xed,0x5e,0xfb,0x76,0xc3,0x0b,0};
    std::copy(std::begin(interruptProgram),std::end(interruptProgram),irq->main.rom.begin());
    for(unsigned source=0;source<3;++source) {
        const unsigned handler=0x100+source*0x40;
        std::vector<std::uint8_t> isr={0xf5,0x3a,0,0x21,0x3c,0x32,0,0x21,0x32,std::uint8_t(0x10+source),0x21};
        if(!source)isr.insert(isr.end(),{0x3e,0x10,0xd3,0x39});
        isr.insert(isr.end(),{0xf1,0xfb,0xed,0x4d});
        std::copy(isr.begin(),isr.end(),irq->main.rom.begin()+handler);
        const unsigned vector=source==0?0xea:source==1?0xe8:0xe4;
        irq->writeMemory(false,0x2000+vector,std::uint8_t(handler));irq->writeMemory(false,0x2001+vector,std::uint8_t(handler>>8));
    }
    irq->run(150);
    irq->diskSio.write(0x3b,2);irq->diskSio.write(0x3b,0xe0);irq->diskSio.write(0x3b,1);irq->diskSio.write(0x3b,4);
    irq->diskSio.write(0x39,1);irq->diskSio.write(0x39,1);
    irq->diskSio.channels[0].pending=true;irq->diskSio.channels[0].frozen=true;
    irq->main.ctc.chn[0].int_vector=0xe8;irq->main.ctc.chn[0].int_state=Z80CTC_INT_REQUESTED;irq->main.ctc.pins|=Z80_INT;
    auto& pio=irq->main.pio[0];pio.enabled=true;pio.pending=true;pio.vector=0xe4;
    irq->run(3000);
    require(irq->memory[0x2110]==1&&irq->memory[0x2111]==2&&irq->memory[0x2112]==3,"real IM2 order SIO before suppressed CTC before PIO");
    require(irq->diskSio.acknowledgements[0]==1&&irq->main.pioAcks[0]==1&&!(irq->main.pins&Z80_INT),"all three RETIs clear own service/request without CTC INT contamination");
    std::cout<<"PASS: SA465 reference trailing STEP/direction/phaseA stop, power-up guard, spin-up/two-index READY/coast, two-drive PIO wiring; SIO modem latch/vector/reset/RTS-DTR outputs; all80 initial cylinders, real Z80 homes from37 and IM2 SIO>CTC>PIO ACK/RETI; no data bitstream or OS boot\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
