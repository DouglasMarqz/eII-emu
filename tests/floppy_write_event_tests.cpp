#include "disk_read_path.h"
#include "reference/disk_read_path_rw_initial.h"
#include <iostream>
#include <stdexcept>
int main()try {
 eii::DiskReadPath actual(87,8);eii::DiskReadPathRwInitial reference(87,8);
 auto a=std::make_shared<eii::EmuiiImage>(std::vector<std::uint8_t>(eii::EmuiiImage::imageBytes));
 auto b=std::make_shared<eii::EmuiiImage>(std::vector<std::uint8_t>(eii::EmuiiImage::imageBytes));
 actual.mountWritable(0,a,eii::EmuiiGap::Hxc2026);reference.mountWritable(0,b,eii::EmuiiGap::Hxc2026);
 eii::FloppyBus bus;eii::DiskSio x,y;
 auto reg=[&](unsigned n,unsigned value){for(auto* s:{&x,&y}){s->write(0x39,n);s->write(0x39,value);}};
 reg(4,0x10);reg(5,0x64);reg(6,0xfa);reg(7,0x96);reg(1,1);
 unsigned random=0x12345678;
 for(unsigned tick=1;tick<=300000;++tick){
  random^=random<<13;random^=random>>17;random^=random<<5;
  if(random%113==0)reg(5,(random&128)?0x6d:0x64);
  if(random%29==0){x.write(0x38,random>>8);y.write(0x38,random>>8);}
  if(random%181==0){x.write(0x39,0xc0);y.write(0x39,0xc0);}
  if(random%73==0){x.write(0x39,0x10);y.write(0x39,0x10);}
  actual.advanceCpuBoundary(tick,bus,x);reference.advanceCpuBoundary(tick,bus,y);
  const auto& p=x.channels[0];const auto& q=y.channels[0];
  if(p.live!=q.live||p.latched!=q.latched||p.pending!=q.pending||p.frozen!=q.frozen||
     p.txEmpty!=q.txEmpty||p.tx.line!=q.tx.line||p.tx.empty!=q.tx.empty||p.tx.underrun!=q.tx.underrun||
     p.tx.crc!=q.tx.crc||p.tx.characters!=q.tx.characters||p.tx.crcFrames!=q.tx.crcFrames||p.tx.overwrites!=q.tx.overwrites||
     x.requestLine()!=y.requestLine()||x.readyN()!=y.readyN())throw std::runtime_error("TX functional state differs from every-edge oracle");
 }
 if(x.channels[0].tx.clocks>=y.channels[0].tx.clocks)throw std::runtime_error("idle clock work was not removed");
 std::cout<<"PASS300000 random enable/disable/data/CRC/status boundaries vs frozen write-clock oracle; exact TX line/CRC/Ready/IRQ states; disabled idle clocks skipped\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
