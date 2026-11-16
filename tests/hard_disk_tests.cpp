#include "hard_disk.h"
#include "disk_dma.h"
#include <iostream>
using eii::HardDisk;
static void check(bool v,const char*s){if(!v)throw std::runtime_error(s);}
int main(){std::uint64_t t=0;HardDisk d(std::vector<std::uint8_t>(8*512,0xa5));
 auto start=[&](std::vector<std::uint8_t> c){d.write(0x3c,1,t+=4);d.write(0x3e,0,t+=4);d.write(0x3e,255,t+=4);for(auto b:c)d.write(0x3c,b,t+=4);};
 auto finish=[&]{auto status=d.read(0x3c,t+=4);check(d.phase==HardDisk::Message,"status then message");check(d.read(0x3c,t+=4)==0&&d.phase==HardDisk::Free,"message releases bus");return status;};
 start({0x2a,0,0,0,0,2,0,0,1,0});check(d.phase==HardDisk::DataOut,"write phase");for(unsigned i=0;i<512;++i)d.write(0x3c,std::uint8_t(i*17+3),t+=4);check(finish()==0,"write status");
 start({0x28,0,0,0,0,2,0,0,1,0});for(unsigned i=0;i<512;++i)check(d.read(0x3c,t+=4)==std::uint8_t(i*17+3),"roundtrip");check(finish()==0,"read status");
 auto old=d.image;start({0x2a,0,0,0,0,8,0,0,1,0});check(finish()==2&&d.image==old,"out of range safe");start({3,0,0,0,0,0});check(d.read(0x3c,t+=4)==0x21,"sense illegal address");for(unsigned i=0;i<3;++i)d.read(0x3c,t+=4);check(finish()==0,"sense complete");check(!d.invalidAccess,"valid sequence");
 start({0x15,0,0,0,22,0});
 for(auto b:std::vector<unsigned>{0,0,0,8,0,0,0,0,0,0,2,0,1,2,100,4,0,0,0,0,0,2})d.write(0x3c,b,t+=4);
 check(finish()==0&&d.cylinders==612&&d.heads==4&&d.blockSize==512,"mode geometry");
 start({4,0x1c,0,0,2,0});
 for(auto b:std::vector<unsigned>{0,0,0,8,0,0,1,0,0,0,0,100})d.write(0x3c,b,t+=4);
 check(finish()==0&&d.defectList.size()==12&&d.image.front()==0x6c&&d.image.back()==0x6c,"format defect list and fill");
 old=d.image;start({4,0x1c,0,0,2,0});for(auto b:std::vector<unsigned>{0,0,0,8,0,0,0,0,0,0,0,100})d.write(0x3c,b,t+=4);
 check(finish()==2&&d.image==old,"invalid defect leaves disk intact");
 d.writable=false;start({0x2a,0,0,0,0,0,0,0,1,0});check(finish()==2&&d.image==old,"write protect");
 start({3,0,0,0,0,0});check(d.read(0x3c,t+=4)==0x27,"write protect sense");for(unsigned i=0;i<3;++i)d.read(0x3c,t+=4);check(finish()==0,"protect sense complete");
 unsigned persisted=0;d.writable=true;
 d.persistImage=[&](std::span<const std::uint8_t> bytes){check(bytes.size()==d.image.size(),"full host persistence image");++persisted;};
 start({0x2a,0,0,0,0,1,0,0,1,0});for(unsigned i=0;i<512;++i)d.write(0x3c,0x42,t+=4);
 check(finish()==0&&persisted==1&&!d.dirty,"direct HDD write flushes and clears dirty only on success");
 d.persistImage=[](std::span<const std::uint8_t>){throw std::runtime_error("injected host failure");};
 bool failed=false;start({0x2a,0,0,0,0,1,0,0,1,0});try{for(unsigned i=0;i<512;++i)d.write(0x3c,0x43,t+=4);}catch(const std::runtime_error&){failed=true;}
 check(failed&&d.dirty&&d.phase==HardDisk::Status&&finish()==2,"host failure propagates and retains unsaved HDD state");
 eii::DiskDma dma;dma.hdIo=true;for(auto v:{0xc3,0x79,0x00,0x88,0x01,0x00,0x54,0x00,0x68,0x00,0x80,0xb5,0x3c,0x00,0x92,0xcf,0xbf,0x87})dma.write(v);
 check(dma.supported()&&dma.request(false),"continuous IO enabled by HD option");dma.grant();dma.clockReady(true);auto pins=dma.pins();for(unsigned i=0;i<100;++i)dma.tick(false,99);check(dma.granted&&dma.transfers==0&&dma.pins()==pins&&!(pins&(Z80_IORQ|Z80_MREQ)),"not-ready holds bus without active IO");dma.clockReady(false);check(!dma.transfers,"ready resumes at source boundary");
 std::cout<<"PASS HD target roundtrip/error and DMA READY boundary\n";
}
