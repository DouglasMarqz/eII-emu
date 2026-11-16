// Panel button latency probe (docs/ROM_SETS.md, slow first press). Real ROMs
// and a read-only bank, production controller, no GUI/DSP. For each button:
// press for 80 ms, then measure LED latch time, LCD change time and floppy
// read activity (read pulses) in the following 4 s; then the same button again.
// usage: panel_latency_probe ROMDIR BANK [main.bin] [scanner.bin] [fast|stock] [seq]
#include "floppy_hfe.h"
#include "machine.h"
#include "vst3/scanner_panel_response.h"
#include <cstdio>
#include <iostream>
#include <vector>

using eii::ScannerButton;
static const char* name(ScannerButton b){
    switch(b){case ScannerButton::VoiceDefinition:return "VoiceDefinition";case ScannerButton::PresetDefinition:return "PresetDefinition";
    case ScannerButton::Sample:return "Sample";case ScannerButton::Disk:return "Disk";case ScannerButton::Special:return "Special";
    case ScannerButton::VcaLfo:return "VcaLfo";case ScannerButton::Filter:return "Filter";case ScannerButton::RealTimeControl:return "RealTimeControl";
    case ScannerButton::Transpose:return "Transpose";case ScannerButton::Tune:return "Tune";case ScannerButton::Select:return "Select";default:return "other";}
}
int main(int argc,char** argv)try{
    if(argc<3){std::cerr<<"usage: panel_latency_probe ROMDIR BANK [main] [scanner] [fast]\n";return 2;}
    const auto folder=std::filesystem::u8path(argv[1]);
    const std::string mainName=argc>3?argv[3]:"main.bin",scannerName=argc>4?argv[4]:"scanner.bin";const bool fast=argc>5&&std::string(argv[5])=="fast";
    auto m=std::make_unique<eii::Machine>(0);
    m->channelTimers.enableControllerTiming();m->channelTimers.controllerTiming.enableRegisterWrites();
    m->load(folder/mainName,folder/scannerName,folder/"ic135.bin",folder/"ic137.bin");
    if(fast)eii::vst::applyFastScannerPanel(m->scanner.rom,true);
    m->initializeDramPowerOn(1);m->enableProductionController();
    m->initializeControllerPowerOn(0xe1122026u);m->enableNormalRuntime();
    auto bytes=eii::readFile(std::filesystem::u8path(argv[2]));
    if(bytes.size()>=8&&std::equal(bytes.begin(),bytes.begin()+8,"HXCPICFE"))bytes=eii::HfeImage(std::move(bytes)).extractEmuii();
    m->floppy.attach(0,37,400000);m->floppy.drives[0]->insertSensorDisk(true);
    m->diskRead=std::make_unique<eii::DiskReadPath>(87,8);
    m->diskRead->mount(0,std::make_shared<const eii::EmuiiImage>(std::move(bytes)),eii::EmuiiGap::Hxc2026);
    constexpr std::uint64_t ms=4000;
    std::uint64_t t=0;auto run=[&](std::uint64_t ticks){m->run(ticks);t+=ticks;};
    for(unsigned i=0;i<250&&!m->lcd.line(0).starts_with("P");++i)run(1000*ms);
    run(1000*ms);
    std::printf("BOOT t=%.3f s main=%s scanner=%s fast=%d lcd0='%s'\n",double(t)/4e6,mainName.c_str(),scannerName.c_str(),int(fast),m->lcd.line(0).c_str());
    const ScannerButton buttons[]={ScannerButton::VoiceDefinition,ScannerButton::PresetDefinition,ScannerButton::Sample,ScannerButton::Disk,
        ScannerButton::Special,ScannerButton::VcaLfo,ScannerButton::Filter,ScannerButton::RealTimeControl,ScannerButton::Transpose,ScannerButton::Tune};
    // "seq": enter/leave/re-enter, then another module, then back (overlay reuse).
    const bool seq=argc>6&&std::string(argv[6])=="seq";
    const ScannerButton sequence[]={ScannerButton::VoiceDefinition,ScannerButton::VoiceDefinition,ScannerButton::VoiceDefinition,ScannerButton::VoiceDefinition,
        ScannerButton::PresetDefinition,ScannerButton::PresetDefinition,ScannerButton::VoiceDefinition,ScannerButton::VoiceDefinition,
        ScannerButton::VcaLfo,ScannerButton::VcaLfo,ScannerButton::Filter,ScannerButton::Filter,ScannerButton::VoiceDefinition};
    std::vector<std::pair<ScannerButton,unsigned>> presses;
    if(seq){unsigned n=0;for(auto b:sequence)presses.push_back({b,++n});}else for(auto b:buttons)for(unsigned press=1;press<=2;++press)presses.push_back({b,press});
    for(auto [b,press]:presses){
        const auto led0=eii::ScannerControls::ledState(m->scannerControls.leds,b);const std::string l0=m->lcd.line(0)+"|"+m->lcd.line(1);
        const auto pulses0=m->diskRead->outputPulses;const auto writes0=m->scannerControls.ledWrites;
        double tLed=-1,tLcd=-1,firstDisk=-1,lastDisk=-1;std::uint64_t lastPulses=pulses0;
        m->scannerControls.setButton(b,true);
        for(std::uint64_t k=1;k<=4000;++k){
            if(k==80)m->scannerControls.setButton(b,false);
            run(ms);
            const auto led=eii::ScannerControls::ledState(m->scannerControls.leds,b);
            if(tLed<0&&led.known&&(!led0.known||led.high!=led0.high))tLed=double(k);
            if(tLcd<0&&m->lcd.line(0)+"|"+m->lcd.line(1)!=l0)tLcd=double(k);
            const auto p=m->diskRead->outputPulses;if(p!=lastPulses){if(firstDisk<0)firstDisk=double(k);lastDisk=double(k);lastPulses=p;}
        }
        std::printf("PRESS %-16s #%u led_ms=%.0f lcd_ms=%.0f disk_pulses=%llu disk_ms=[%.0f..%.0f] ledwrites=%llu lcd='%s|%s'\n",name(b),press,tLed,tLcd,
            (unsigned long long)(m->diskRead->outputPulses-pulses0),firstDisk,lastDisk,(unsigned long long)(m->scannerControls.ledWrites-writes0),m->lcd.line(0).c_str(),m->lcd.line(1).c_str());
    }
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
