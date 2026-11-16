// Sample-input step 0: probe-only I/O trace of the original firmware's Sample
// overlay. Caller-supplied ROMs/disk only; no model change, no injection.
// usage: sample_trace_probe ROMDIR DISK BOOT_TICKS [KEY TICKS]...
//   KEY: S=Sample X=Stop P=Play D=Disk R=Record E=Enter Q=Preset Definition
//   V=Voice Definition 0..9=keypad digit (9 = YES, 7 = NO).
//   Env T0=<tick> lists every non-timer traced I/O from that tick (max 400);
//   env DUMP=1 writes the main-CPU 64K view to dump.bin at the end;
//   env SWAPDISK=<image> mounts another disk in drive 0 right after boot.
// Traced main-CPU I/O: IN 08h..0Fh (SAR readback decode, 7-11), OUT 20h (DMA),
// 32h/33h (PIO B: IMODE=B7), 40h (-CSWF0), 18h..1Bh (8254 IC3).
#include "floppy_hfe.h"
#include "machine.h"
#include <cstdio>
#include <map>
#include <string>
#include <cstdlib>
using B=eii::ScannerButton;
struct Ev{std::uint64_t t;bool w;std::uint16_t a;std::uint8_t d;std::uint16_t pc;};
int main(int argc,char**argv){
  const std::filesystem::path p=argv[1]; auto bytes=eii::readFile(argv[2]);
  if(bytes.size()>=8&&std::equal(bytes.begin(),bytes.begin()+8,"HXCPICFE"))bytes=eii::HfeImage(std::move(bytes)).extractEmuii();
  auto m=std::make_unique<eii::Machine>(0);
  m->load(p/"main.bin",p/"scanner.bin",p/"ic135.bin",p/"ic137.bin");
  m->initializeDramPowerOn(1);m->enableProductionController(true);m->initializeControllerPowerOn(0xe1122026u);
  m->enableNormalRuntime();m->floppy.attach(0,37,400000);m->floppy.drives[0]->insertSensorDisk(true);
  m->diskRead=std::make_unique<eii::DiskReadPath>(87,8);
  m->diskRead->mount(0,std::make_shared<const eii::EmuiiImage>(std::move(bytes)),eii::EmuiiGap::Hxc2026);
  std::vector<Ev> ev;
  m->trace=[&](bool s,bool w,std::uint16_t a,std::uint8_t d,std::uint16_t pc){if(s)return;unsigned q=a&255;
    if((!w&&q>=8&&q<16)||(w&&(q==0x20||q==0x32||q==0x33||q==0x40||(q>=0x18&&q<=0x1b))))ev.push_back({m->ticks,w,std::uint16_t(q),d,pc});};
  auto lcd=[&](const char*t){std::printf("%s t=%llu lcd=[%s|%s] pc=%04x ev=%zu dma=%llu/%llu\n",t,(unsigned long long)m->ticks,m->lcd.line(0).c_str(),m->lcd.line(1).c_str(),m->main.cpu.pc,ev.size(),(unsigned long long)m->diskDma.writes,(unsigned long long)m->diskDma.transfers);};
  m->run(std::stoull(argv[3]));lcd("BOOT");
  if(const char* swap=getenv("SWAPDISK")){ // insert another disk after boot (e.g. the OS disk)
    auto b2=eii::readFile(swap);if(b2.size()>=8&&std::equal(b2.begin(),b2.begin()+8,"HXCPICFE"))b2=eii::HfeImage(std::move(b2)).extractEmuii();
    m->diskRead->mount(0,std::make_shared<const eii::EmuiiImage>(std::move(b2)),eii::EmuiiGap::Hxc2026);m->run(4000000);lcd("SWAP");}
  auto press=[&](B b,const char*n,std::uint64_t after){m->scannerControls.setButton(b,true);m->run(800000);m->scannerControls.setButton(b,false);
    for(std::uint64_t i=0;i<after;i+=4000000){m->run(4000000);} std::string s=std::string("KEY ")+n;lcd(s.c_str());};
  for(int i=4;i+1<argc;i+=2){std::string k=argv[i];B b=k=="S"?B::Sample:k=="E"?B::Enter:k=="Y"?B::Digit9:B(unsigned(B::Digit0)+0);
    if(k.size()==1&&k[0]>='0'&&k[0]<='9'){static const B d[10]={B::Digit0,B::Digit1,B::Digit2,B::Digit3,B::Digit4,B::Digit5,B::Digit6,B::Digit7,B::Digit8,B::Digit9};b=d[k[0]-'0'];}
    if(k=="S")b=B::Sample; if(k=="X")b=B::Stop; if(k=="P")b=B::Play; if(k=="D")b=B::Disk; if(k=="R")b=B::Record; if(k=="Q")b=B::PresetDefinition; if(k=="V")b=B::VoiceDefinition;
    press(b,k.c_str(),std::stoull(argv[i+1]));}
  if(getenv("DUMP")){FILE*f=fopen("dump.bin","wb");for(unsigned a=0;a<65536;++a)fputc(m->readMemory(false,std::uint16_t(a)),f);fclose(f);}
  std::map<std::string,unsigned> seen; std::uint64_t t0=getenv("T0")?std::stoull(getenv("T0")):~0ull; unsigned np=0;
  for(auto&e:ev)if(e.t>=t0&&!(e.w&&e.a>=0x18&&e.a<=0x1b)&&np++<400)std::printf("ALL %llu %s %02X=%02X pc=%04X\n",(unsigned long long)e.t,e.w?"OUT":"IN ",e.a,e.d,e.pc);
  for(auto&e:ev){char b[96];std::snprintf(b,96,"%s %02X=%02X pc=%04X",e.w?"OUT":"IN ",e.a,e.d,e.pc);if(seen[b]++<1&&seen.size()<400)std::printf("%llu %s\n",(unsigned long long)e.t,b);}
  std::printf("distinct=%zu total=%zu\n",seen.size(),ev.size());
  for(auto&[k,v]:seen)if(v>50)std::printf("  x%u %s\n",v,k.c_str());
}
