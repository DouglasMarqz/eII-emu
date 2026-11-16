#include "machine.h"
#include "floppy_hfe.h"
#include <iostream>
int main(int argc,char**argv)try{
 if(argc<5)return 2;auto m=std::make_unique<eii::Machine>(0);std::filesystem::path r=argv[1];m->load(r/"main23.bin",r/"scanner.bin",r/"ic135.bin",r/"ic137.bin");m->initializeDramPowerOn(1);m->enableProductionController();m->initializeControllerPowerOn(0xe1122026u);m->enableNormalRuntime();if(std::string(argv[2])!="none")m->installHardDisk(eii::readFile(argv[2]));
 const bool hd=std::string(argv[3])=="none";
 if(!hd){auto bytes=eii::readFile(argv[3]);if(bytes.size()>8&&std::equal(bytes.begin(),bytes.begin()+8,"HXCPICFE"))bytes=eii::HfeImage(std::move(bytes)).extractEmuii();m->floppy.attach(0,37,400000);m->floppy.drives[0]->insertSensorDisk(true);m->diskRead=std::make_unique<eii::DiskReadPath>(87,8);m->diskRead->mount(0,std::make_shared<const eii::EmuiiImage>(std::move(bytes)),eii::EmuiiGap::Hxc2026);}
 std::string last;bool ready=false;for(unsigned t=0;t<(hd?40000000u:450000000u);t+=100000){m->run(100000);auto lcd=m->lcd.line(0)+" / "+m->lcd.line(1);if(lcd!=last){std::cout<<m->ticks<<" LCD "<<lcd<<std::endl;last=lcd;}if(m->ticks>30000000&&m->lcd.line(0).starts_with("P01 NULL PRESET")&&m->lcd.line(1)=="                "){ready=true;break;}}
 if(!ready)throw std::runtime_error("saved floppy did not reload NULL PRESET");std::ofstream data(std::string(argv[4])+".ram",std::ios::binary);data.write(reinterpret_cast<const char*>(m->memory.data()),m->memory.size());std::ofstream known(std::string(argv[4])+".known",std::ios::binary);known.write(reinterpret_cast<const char*>(m->dramKnown.data()),m->dramKnown.size());std::cout<<"PASS saved image cold boot; DMA "<<m->diskDma.transfers<<std::endl;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}
