#define WITH_MIDI 1
#ifndef EII_SOUND_PATH_LAB
#error Build with -DEII_SOUND_PATH_LAB; this observer is never enabled in Release.
#endif
#include "floppy_hfe.h"
#include "machine.h"
#include <chrono>
#include <bit>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <sys/wait.h>
#include <unistd.h>
std::unique_ptr<eii::Machine> machine;
static bool recording=false;
static std::array<std::uint64_t,0x80000> sampleReads{};
void eii::production::observeSampleRead(std::uint32_t address,std::uint8_t) {
  if(recording)++sampleReads[address];
}
int main(int argc, char **argv) try {
  if(argc<3 || argc>4 || (argc==4 && std::string(argv[3])!="--stress"))throw std::invalid_argument("usage: sound_path_probe ROM-folder original-bank-image [--stress]");
  const bool stress=argc==4;
  const std::filesystem::path folder = argv[1];
  machine = std::make_unique<eii::Machine>();
  auto& m=machine;
  // Match the GUI concrete startup and use caller-supplied media only.
  m->channelTimers.controllerTiming = eii::ControllerTiming(0);
  m->channelTimers.enableControllerTiming();
  m->channelTimers.controllerTiming.enableRegisterWrites();
  m->load(folder / "main.bin", folder / "scanner.bin", folder / "ic135.bin",
          folder / "ic137.bin");
  m->initializeDramPowerOn(1);
  m->enableProductionController();
  m->initializeControllerPowerOn(0xe1122026u);
  m->enableNormalRuntime();
  auto bytes = eii::readFile(argv[2]);
  if (bytes.size() >= 8 &&
      std::equal(bytes.begin(), bytes.begin() + 8, "HXCPICFE"))
    bytes = eii::HfeImage(std::move(bytes)).extractEmuii();
  m->floppy.attach(0, 37, 400000);
  m->floppy.drives[0]->insertSensorDisk(true);
  m->diskRead = std::make_unique<eii::DiskReadPath>(87, 8);
  m->diskRead->mount(0,
                     std::make_shared<const eii::EmuiiImage>(std::move(bytes)),
                     eii::EmuiiGap::Hxc2026);

  while(m->ticks<200000000) {
    m->run(1000000);
    if(m->diskDma.transfers>=520552 && m->lcd.line(0).find("P01")!=std::string::npos &&
       !m->floppy.drives[0]->selected())break;
  }
  m->run(4000000);
  std::cout << "BOOT tick=" << m->ticks << " LCD=[" << m->lcd.line(0) << "]" << std::endl;

  // Native sound-path lab. Sample live held DAC/CV observations at 48kHz;
  // no firmware changes, no source-clock observer added to the release engine.
  for(unsigned preset=0;preset<2;++preset) {
    auto pid=fork(); if(pid<0)throw std::runtime_error("fork");
    if(!pid) {
      m->run(8000000);
      auto press=[&](eii::ScannerButton b){m->scannerControls.setButton(b,true);m->run(800000);m->scannerControls.setButton(b,false);m->run(800000);};
      press(eii::ScannerButton::Digit0);press(preset?eii::ScannerButton::Digit2:eii::ScannerButton::Digit1);
      m->run(4000000);
      const std::string expected=preset?"P02":"P01";
      if(m->lcd.line(0).find(expected)!=0)throw std::runtime_error("panel did not select requested preset");
      std::cerr<<"PRESET "<<preset+1<<" ["<<m->lcd.line(0)<<"]\n";
      auto sw=m->playbackSwitchInputs();
      for(unsigned ch=0;ch<8;++ch)std::cerr<<"S3528 "<<ch<<" "<<unsigned(sw.reconstructionCode[ch].value)<<" "<<sw.reconstructionCutoffHz[ch].value<<"\n";
      std::array<eii::analog::BasicPlaybackVoice,8> voices{
        eii::analog::BasicPlaybackVoice(48000),eii::analog::BasicPlaybackVoice(48000),
        eii::analog::BasicPlaybackVoice(48000),eii::analog::BasicPlaybackVoice(48000),
        eii::analog::BasicPlaybackVoice(48000),eii::analog::BasicPlaybackVoice(48000),
        eii::analog::BasicPlaybackVoice(48000),eii::analog::BasicPlaybackVoice(48000)};
      std::ofstream out("build/050/preset"+std::to_string(preset+1)+".csv");
      out<<"frame";for(unsigned ch=0;ch<8;++ch)out<<",code"<<ch<<",acv"<<ch<<",fcv"<<ch<<",voice"<<ch;out<<",sum\n";
      const std::array<std::uint8_t,3> on{0x90,60,100};m->receiveMidi(on);
      recording=true;const auto start=m->ticks;double peak=0,energy=0;unsigned clipped=0;
      for(unsigned i=0;i<48000;++i){
        m->run(start+(std::uint64_t(i+1)*4000000)/48000-m->ticks);
        const auto input=m->playbackAnalogInputs();double sum=0;
        out<<i;
        for(unsigned ch=0;ch<8;++ch) {
          auto cv=m->controlVoltage.held[eii::ControlVoltageBus::Frequency][ch];
          double v=voices[ch].process(input.codes[ch],input.amplitudeCv[ch],{cv.volts,cv.known});
          if(ch==0&&!m->channel0PlaybackEnabled())v=0;
          sum+=v;out<<','<<unsigned(input.codes[ch].value)<<','<<input.amplitudeCv[ch].value<<','<<cv.volts<<','<<v;
        }
        out<<','<<sum<<'\n';peak=std::max(peak,std::abs(sum));energy+=sum*sum;clipped+=std::abs(sum)>1;
      }
      recording=false;
      std::ofstream reads("build/050/reads"+std::to_string(preset+1)+".csv");
      reads<<"address,reads,code\n";
      for(unsigned a=0;a<sampleReads.size();++a)if(sampleReads[a])reads<<a<<','<<sampleReads[a]<<','<<unsigned(a<0x2800?m->dramUnderlay[a]:m->memory[a])<<'\n';
      reads.close();
      out.close();std::cerr<<"AUDIO preset="<<preset+1<<" peak="<<peak<<" RMS="<<std::sqrt(energy/48000)<<" clipped="<<clipped<<"\n";
      std::ofstream mem("build/050/loaded-dram.bin",std::ios::binary);mem.write(reinterpret_cast<const char*>(m->memory.data()),m->memory.size());mem.close();
      if(stress) {
        const std::array<std::uint8_t,3> release{0x80,60,0};m->receiveMidi(release);m->run(4000000);
        const auto received=m->midiSerial.received,read=m->midiSerial.reads,overruns=m->midiSerial.overruns;
        sampleReads.fill(0);recording=true;
        for(unsigned i=0;i<96;++i) {
          const std::uint8_t note=48+i%24;
          const std::array<std::uint8_t,3> on{0x90,note,100},off{0x80,note,0};
          m->receiveMidi(on);m->run(100000);m->receiveMidi(off);m->run(100000);
        }
        m->run(8000000);recording=false;
        std::uint64_t low=0;for(unsigned a=0;a<0x2800;++a)low+=sampleReads[a];
        std::cerr<<"STRESS preset="<<preset+1<<" pairs=96 gap=100000 invalid="<<m->channelTimers.controllerTiming.productionEngineInvalid()
          <<" UART="<<m->midiSerial.received-received<<'/'<<m->midiSerial.reads-read<<'/'<<m->midiSerial.overruns-overruns<<" lowDRAM="<<low<<std::endl;
      }
      _exit(0);
    }
    int status=0;waitpid(pid,&status,0);if(status)throw std::runtime_error("child failed");
  }
  return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
