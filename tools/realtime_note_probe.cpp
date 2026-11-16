#define WITH_MIDI 1
#include "floppy_hfe.h"
#include "machine.h"
#include <chrono>
#include <bit>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
static std::uint64_t hash(const auto &data) {
  std::uint64_t h = 14695981039346656037ull;
  for (auto b : data) {
    h ^= b;
    h *= 1099511628211ull;
  }
  return h;
}
std::unique_ptr<eii::Machine> machine;
int main(int argc, char **argv) try {
  if (argc < 3 || argc > 4)
    throw std::invalid_argument("usage: realtime_note_probe ROM-folder "
                                "original-bank-image [spans|edges|lab|compare]");
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
  const bool compare=argc==4 && std::string(argv[3])=="compare";
  const bool lab=argc==4 && std::string(argv[3])=="lab";
  if (argc == 4 && !lab && !compare) {
    const std::string mode = argv[3];
    if (mode != "spans" && mode != "edges")
      throw std::invalid_argument("expected spans or edges");
    m->channelTimers.enableControllerSpans(mode == "spans");
  }
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
  unsigned failures=0;
  for(unsigned index=0;;++index) {
    unsigned scenario=index,gap=20000;
    if(compare){if(index>=2)break;scenario=6+index;}
    else if(lab) {std::cout << "READY scenario gap_ticks" << std::endl;if(!(std::cin>>scenario>>gap))break;}
    else if(index>=5)break;
    if(scenario>9 || gap<4000 || gap>1000000)throw std::invalid_argument("scenario/gap outside lab range");
    const auto pid=fork();if(pid<0)throw std::runtime_error("fork failed");
    if(!pid) {
      double energy=0,peak=0;std::uint64_t frames=0,pcmHash=14695981039346656037ull;
      if(scenario==6 || scenario==7)m->channelTimers.controllerTiming.enableBlockPublication(scenario==7);
      if(scenario!=0)m->enableRuntimeAudio([&](auto block){for(auto f:block){energy+=double(f.left)*f.left;peak=std::max(peak,double(std::abs(f.left)));++frames;const auto bits=std::bit_cast<std::uint32_t>(f.left);for(unsigned i=0;i<4;++i){pcmHash^=(bits>>(i*8))&255;pcmHash*=1099511628211ull;}}});
      const auto measure=[&](const char* label,std::uint64_t n) {
        energy=peak=0;frames=0;const auto start=std::chrono::steady_clock::now();const auto cpu=std::clock();
        m->run(n);
        std::cout << "scenario=" << scenario << " " << label << " ticks=" << n << " wall="
                  << std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()
                  << " cpu=" << double(std::clock()-cpu)/CLOCKS_PER_SEC << " pcmRMS=" << (frames?std::sqrt(energy/frames):0) << " peak=" << peak
                  << " ACV=";
        for(auto cv:m->controlVoltage.held[eii::ControlVoltageBus::Amplitude])std::cout << cv.volts << ',';
        std::cout << " uart=" << m->midiSerial.received << '/' << m->midiSerial.reads << '/' << m->midiSerial.overruns << std::endl;
      };
      measure("idle",4000000);
      if(scenario==4) {
        auto press=[&](eii::ScannerButton b){m->scannerControls.setButton(b,true);m->run(800000);m->scannerControls.setButton(b,false);m->run(800000);std::cout << "BUTTON [" << m->lcd.line(0) << "][" << m->lcd.line(1) << "]" << std::endl;};
        press(eii::ScannerButton::PresetDefinition);press(eii::ScannerButton::Digit3);press(eii::ScannerButton::Digit0);
        for(unsigned i=0;i<=40;++i){m->scannerControls.setVoltage(eii::ScannerAnalogInput::SliderA,double(i)/8);m->run(400000);std::cout << "MIDI_MENU " << i << " [" << m->lcd.line(0) << "][" << m->lcd.line(1) << "]" << std::endl;}
        _exit(0);
      }
      const auto start=std::chrono::steady_clock::now();const auto cpu=std::clock();
      for(unsigned i=0;i<160;++i) {
        const std::uint8_t note=std::uint8_t(48+i%12);
        const std::array<std::uint8_t,3> on{0x90,note,100},off{std::uint8_t((scenario==2 || scenario>=6)?0x90:0x80),note,0};
        m->receiveMidi(on);m->run(gap);m->receiveMidi(off);m->run(gap);
      }
      std::cout << "scenario=" << scenario << " dense ticks=6400000 wall=" << std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count() << " cpu=" << double(std::clock()-cpu)/CLOCKS_PER_SEC << std::endl;
      measure("release1",4000000);measure("release5",16000000);
      if(scenario==3) {const std::array<std::uint8_t,3> mod{0xb0,1,127},bend{0xe0,127,127};m->receiveMidi(mod);m->receiveMidi(bend);measure("controllers",4000000);}
      std::cout << "RESULT scenario=" << scenario << " pcmHash=" << pcmHash << " mem=" << hash(m->memory) << " scan=" << hash(m->scannerRam) << std::endl;
      _exit(0);
    }
    int status=0;waitpid(pid,&status,0);if(!WIFEXITED(status)||WEXITSTATUS(status))++failures;
  }
  std::cout << "FAILURES=" << failures << std::endl;return failures?1:0;
} catch(const std::exception& e){std::cerr << e.what() << std::endl;return 1;}
