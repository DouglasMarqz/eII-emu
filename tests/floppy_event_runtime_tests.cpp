#include "floppy_mechanics.h"
#include "reference/floppy_mechanics_036.h"
#include "disk_read_path.h"
#include <cassert>
#include <random>
#include <iostream>
#include <memory>

int main() {
    std::mt19937 rng(0xf10ee026);
    for(unsigned trial=0;trial<16;++trial) {
        const auto cylinder=rng()%80,phase=rng()%800000;
        eii::Sa465Drive event(cylinder,phase);eii::ReferenceSa465Drive reference(cylinder,phase);
        bool disk=true;event.insertSensorDisk(false);reference.insertSensorDisk(false);
        std::uint64_t now=0;
        for(unsigned step=0;step<10000;++step) {
            now+=step%5==0?rng()%3000000:rng()%12;
            if(step%113==0) {
                disk=!disk;if(disk){event.insertSensorDisk(true);reference.insertSensorDisk(true);}else{event.eject();reference.eject();}
            }
            const bool sel=rng()%8==0,motor=rng()%20==0,stp=rng()%2,dir=rng()%2,side=rng()%2,write=rng()%8!=0;
            event.drive(sel,motor,stp,dir,side,write,now);reference.drive(sel,motor,stp,dir,side,write,now);
            assert(event.cylinder==reference.cylinder && event.positionValid==reference.positionValid && event.phase()==reference.phase());
            assert(event.indexPulses==reference.indexPulses && event.steps==reference.steps && event.timingViolations==reference.timingViolations);
            assert(event.trackZeroN()==reference.trackZeroN() && event.indexN()==reference.indexN() && event.readyN()==reference.readyN());
            assert(event.readGateAllowed()==reference.readGateAllowed() && event.spinning()==reference.spinning());
        }
    }
    // Same independently formatted media and real separator/SIO, including
    // gate/side changes and all pulse widths. Event mode must preserve every
    // received bit, separator expiry, FIFO/CRC state and aggregate counters.
    std::vector<std::uint8_t> data(573440);for(auto& b:data)b=std::uint8_t(rng());
    auto image=std::make_shared<const eii::EmuiiImage>(std::move(data));
    for(unsigned width:{1u,4u,5u,8u,59u,60u,63u}) {
        eii::FloppyBus bus;bus.attach(0,0,12345);bus.drives[0]->insertSensorDisk(true);
        eii::DiskReadPath reference(87,width),events(87,width);events.enableEventTiming();
        reference.mount(0,image,eii::EmuiiGap::Hxc2026);events.mount(0,image,eii::EmuiiGap::Hxc2026);
        eii::DiskSio a,b;
        const auto configure=[](eii::DiskSio& sio) {
            const auto reg=[&](unsigned n,unsigned v){sio.write(0x39,n);sio.write(0x39,v);};
            reg(4,0x10);reg(5,0x64);reg(6,0xfa);reg(7,0x96);
            sio.write(0x39,0x43);sio.write(0x39,0xd9);reg(1,0xe0);
        };configure(a);configure(b);
        for(std::uint64_t tick=1;tick<=3800000;++tick) {
            const bool side=tick<3501234;
            const auto pins=std::uint8_t(tick>=3350123 && tick<3350987?255:14);
            bus.drive(pins,side,true,tick);
            reference.advanceCpuBoundary(tick,bus,a);events.advanceCpuBoundary(tick,bus,b);
            assert(reference.sourceEdges==events.sourceEdges && reference.outputPulses==events.outputPulses && reference.selectedEdges==events.selectedEdges);
            assert(reference.separator.rxClock()==events.separator.rxClock() && reference.separator.rxData()==events.separator.rxData());
            assert(reference.separator.samples==events.separator.samples && reference.separator.ignoredRetriggers==events.separator.ignoredRetriggers);
            assert(a.channels[0].rx.clocks==b.channels[0].rx.clocks && a.channels[0].rx.characters==b.channels[0].rx.characters);
            assert(a.channels[0].rx.depth==b.channels[0].rx.depth && a.channels[0].rx.crc==b.channels[0].rx.crc && a.channels[0].rx.status==b.channels[0].rx.status);
            if(a.channels[0].rx.depth)assert(a.read(0x38)==b.read(0x38));
        }
    }
    std::cout<<"PASS:160000 randomized mechanism steps vs frozen oracle; seven pulse widths, gate/side changes, exact pulse/separator/SIO clocks against per-edge media path\n";
}
