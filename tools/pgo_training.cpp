// PGO training workload for the Windows release core (not a validation tool).
// Same concrete startup as the GUI: original firmware boots the supplied bank,
// then idle, eight held MIDI notes and release run in the selected audio mode.
// usage: pgo_training ROM-folder bank-image MODE(0..3) NATIVE(0/1) S3528(0/1)
#define WITH_MIDI 1
#include "floppy_hfe.h"
#include "machine.h"
#include <chrono>
#include <iostream>
static int trainingMain(int argc, char **argv) try {
  if (argc != 6)
    throw std::invalid_argument("usage: pgo_training ROM-folder bank-image MODE NATIVE S3528");
  const auto p = std::filesystem::u8path(argv[1]);
  const int mode = std::atoi(argv[3]);
  auto m = std::make_unique<eii::Machine>(0);
  m->load(p / "main.bin", p / "scanner.bin", p / "ic135.bin", p / "ic137.bin");
  m->initializeDramPowerOn(1);
  m->enableProductionController(true);
  m->initializeControllerPowerOn(0xe1122026u);
  auto bytes = eii::readFile(std::filesystem::u8path(argv[2]));
  if (bytes.size() >= 8 && std::equal(bytes.begin(), bytes.begin() + 8, "HXCPICFE"))
    bytes = eii::HfeImage(std::move(bytes)).extractEmuii();
  m->floppy.attach(0, 37, 400000);
  m->floppy.drives[0]->insertSensorDisk(false);
  m->diskRead = std::make_unique<eii::DiskReadPath>(87, 8);
  m->diskRead->mountWritable(0, std::make_shared<eii::EmuiiImage>(std::move(bytes)), eii::EmuiiGap::Hxc2026);
  m->diskDma.floppyIo=true;
  m->enableNormalRuntime();
  std::uint64_t frames = 0;
  m->enableRuntimeAudio([&](auto block) { frames += block.size(); }, eii::analog::RuntimeAudioMode(mode));
  m->reconstruction(std::atoi(argv[5]) != 0);
  m->nativeAudio(std::atoi(argv[4]) != 0);
  const auto stage = [&](const char *label, std::uint64_t ticks) {
    const auto start = std::chrono::steady_clock::now();
    m->run(ticks);
    std::cout << label << " wall=" << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << std::endl;
  };
  stage("boot", 200000000);
  if(!m->lcd.line(0).starts_with("P"))
    throw std::runtime_error("Training bank did not boot: choose a known bootable bank, not an empty/formatting disk.");
  stage("idle", 8000000);
  for (unsigned n = 48; n < 56; ++n) { const std::array<std::uint8_t, 3> on{0x90, std::uint8_t(n), 100}; m->receiveMidi(on); }
  stage("notes", 16000000);
  for (unsigned n = 48; n < 56; ++n) { const std::array<std::uint8_t, 3> off{0x80, std::uint8_t(n), 0}; m->receiveMidi(off); }
  stage("release", 8000000);
  std::cout << "frames=" << frames << std::endl;
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}


#ifdef _WIN32
#include <windows.h>
// PowerShell passes UTF-16 paths. Keep accented ROM/bank folders intact.
int wmain(int argc,wchar_t** argv){
  std::vector<std::string> args;args.reserve(argc);
  for(int i=0;i<argc;++i){const int n=WideCharToMultiByte(CP_UTF8,0,argv[i],-1,nullptr,0,nullptr,nullptr);if(n<=0)return 2;
    std::string value(n,'\0');WideCharToMultiByte(CP_UTF8,0,argv[i],-1,value.data(),n,nullptr,nullptr);value.pop_back();args.push_back(std::move(value));}
  std::vector<char*> pointers;for(auto& a:args)pointers.push_back(a.data());return trainingMain(argc,pointers.data());
}
#else
int main(int argc,char** argv){return trainingMain(argc,argv);}
#endif
