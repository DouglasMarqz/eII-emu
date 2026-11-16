#include "machine.h"
#include <iostream>
#include <stdexcept>
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main()try {
    eii::MidiSerial2651 receiver;
    receiver.writeMode(0x4e);receiver.writeMode(0);receiver.writeCommand(0x24);
    require(receiver.enabled(),"firmware8N1/external500k/16/RxEN");
    eii::MidiWire wire;const std::array<std::uint8_t,3> on{0x90,60,100};
    require(wire.enqueue(on,17)&&wire.queuedBytes()==3,"queue note packet and expose pending byte count");
    wire.advance(1296,receiver);require(!receiver.ready,"no early character delivery");
    wire.advance(1297,receiver);require(receiver.ready&&receiver.readData()==0x90,"first byte after320us");
    wire.advance(2577,receiver);require(receiver.readData()==60,"data byte order");
    wire.advance(3857,receiver);require(receiver.readData()==100&&wire.queuedBytes()==0,"velocity byte order and drained queue count");
    receiver.receive(1);receiver.receive(2);require((receiver.status()&16)&&receiver.readData()==2,"overrun holds latest character/status");
    receiver.writeCommand(0x34);require(!(receiver.status()&16),"error reset");
    receiver.receive(3);receiver.writeCommand(0);require(!receiver.ready,"disable clearsRXRDY");
    receiver.receive(4);require(receiver.rejected==1,"disabled input not forced through");
    receiver.readCommand();receiver.writeMode(0xca);receiver.writeMode(0x3e);receiver.writeCommand(0x27);
    require(!receiver.enabled(),"diagnostic9600/7N2 not fabricated asMIDI");
    receiver.readCommand();receiver.writeMode(0x4e);receiver.writeMode(0);receiver.writeCommand(0x24);
    for(unsigned s=0x80;s<0xf0;++s)require(eii::midiMessageLength(s)==(((s&0xf0)==0xc0||(s&0xf0)==0xd0)?2:3),"allchannel shortmessage lengths");
    require(eii::midiMessageLength(0xf8)==1&&!eii::midiMessageLength(0xf0)&&!eii::midiMessageLength(0x40),"realtime/SysEx/running-data guards");
    std::array<std::uint8_t,4097> flood{};require(!wire.enqueue(flood,4000)&&wire.dropped==1,"bounded wire rejects complete packet on overflow");
    // Actual Z80 programs the UART andCTC1/IM2; ISR reads the physical RHR.
    auto m=std::make_unique<eii::Machine>();m->scanner.rom[0]=0x76;
    const unsigned char boot[]={0xf3,0x31,0xff,0x27,0x3e,0x20,0xed,0x47,0xed,0x5e,
      0xdb,3,0x3e,0x4e,0xd3,6,0xaf,0xd3,6,0x3e,0x24,0xd3,7,
      0x3e,0xe0,0xd3,0x28,0x3e,0xc7,0xd3,0x29,0x3e,1,0xd3,0x29,0xfb,0x76,0x18,0xfd};
    const unsigned char isr[]={0xf5,0xdb,0,0x32,0,0x21,0xf1,0xfb,0xed,0x4d};
    std::copy(std::begin(boot),std::end(boot),m->main.rom.begin());
    std::copy(std::begin(isr),std::end(isr),m->main.rom.begin()+0x100);
    m->memory[0x20e2]=0;m->memory[0x20e3]=1;
    require(!m->channel0PlaybackEnabled(),"unknown/input MODE does not enable channel0");
    m->main.pio[1].mode=3;m->main.pio[1].mask=0x7f;m->main.pio[1].output=0;
    require(m->channel0PlaybackEnabled(),"known low PIOB7 MODE enables playback0");
    m->main.pio[1].output=128;require(!m->channel0PlaybackEnabled(),"sampling MODE suppresses playback0");
    m->main.pio[1].mode=1;
    m->run(1000);const std::array<std::uint8_t,1> byte{0x90};m->receiveMidi(byte);m->run(3000);
    require(m->midiSerial.received==1&&m->midiSerial.reads==1&&m->memory[0x2100]==0x90,
            "CTC1/IM2 interrupt executes original Z80IN fromSCN2651 data register");
    std::cout<<"PASS: SCN2651 configuration/status/overrun/reset;31250baud pacing/bounded wire;allshortmessages;realZ80 UART->CTC1->IM2->IN\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
