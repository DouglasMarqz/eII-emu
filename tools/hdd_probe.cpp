#include "machine.h"
#include <iostream>
int main(int argc,char**argv)try{
 if(argc<3){std::cerr<<"Usage: hdd_probe ROMDIR FLOPPY|none [ticks] [input-hdd] [output-hdd] [save]\n";return 2;}
 auto m=std::make_unique<eii::Machine>(0);std::filesystem::path r=argv[1];
 m->load(r/"main23.bin",r/"scanner.bin",r/"ic135.bin",r/"ic137.bin");m->initializeDramPowerOn(1);m->enableProductionController();m->initializeControllerPowerOn(0xe1122026u);m->enableNormalRuntime();
 const bool reopen=argc>4&&std::string(argv[4])!="-";
 m->installHardDisk(reopen?eii::readFile(argv[4]):std::vector<std::uint8_t>(613*4*18*512));
 if(std::string(argv[2])!="none"){
 m->floppy.attach(0,37,400000);m->floppy.drives[0]->insertSensorDisk(true);m->diskRead=std::make_unique<eii::DiskReadPath>(87,8);
 m->diskRead->mount(0,std::make_shared<const eii::EmuiiImage>(eii::readFile(argv[2])),eii::EmuiiGap::Hxc2026);}
 using B=eii::ScannerButton;
 std::vector<std::pair<unsigned,B>> actions={{20000000,B::Special},{28000000,B::Digit2},{32000000,B::Digit2},{38000000,B::Digit9},{44000000,B::Enter},{81000000,B::Digit7},{88000000,B::Disk},{94000000,B::Digit2},{100000000,B::Digit0},{104000000,B::Digit2},{108000000,B::Enter}};
 if(reopen){actions={{20000000,B::Disk},{24000000,(argc>6?B::Digit7:B::Digit2)},{28000000,B::Digit0},{32000000,B::Digit2},{36000000,B::Enter},{42000000,B::Digit9},{48000000,B::Enter}};}
 if(argc>6&&std::string(argv[6])=="boot")actions.clear();
 std::string last;std::size_t commands=0;const unsigned limit=argc>3?std::stoul(argv[3]):200000000;
 for(unsigned t=0;t<limit;t+=100000){for(auto [at,b]:actions){if(t==at)m->scannerControls.setButton(b,true);if(t==at+800000)m->scannerControls.setButton(b,false);}
 m->run(100000);auto lcd=m->lcd.line(0)+" / "+m->lcd.line(1);if(lcd!=last){std::cout<<m->ticks<<" LCD "<<lcd<<" PC "<<std::hex<<m->main.cpu.pc<<std::dec<<std::endl;last=lcd;}
 while(commands<m->hardDisk->commands.size()){auto&c=m->hardDisk->commands[commands++];if(commands<100){std::cout<<"HD CDB ";for(auto b:c)std::cout<<std::hex<<unsigned(b)<<' ';std::cout<<std::dec<<std::endl;if(c[0]==0x15){std::cout<<"MODE blockSize "<<m->hardDisk->blockSize<<" data ";for(auto b:m->hardDisk->lastMode)std::cout<<std::hex<<unsigned(b)<<' ';std::cout<<std::dec<<std::endl;}}}
 if(t%10000000==0)std::cout<<"PROGRESS "<<m->ticks<<" DMA "<<m->diskDma.transfers<<" HD cmds "<<commands<<" phase "<<m->hardDisk->phase<<" ioInvalid "<<m->hardDisk->invalidAccess<<std::endl;
 }
 if(argc>5)m->hardDisk->save(argv[5]);
 std::cout<<m->report()<<"HD bytes R/W "<<m->hardDisk->bytesRead<<'/'<<m->hardDisk->bytesWritten<<" invalid "<<m->hardDisk->invalidAccess<<std::endl;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}
