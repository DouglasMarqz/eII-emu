#include "machine.h"
#include "floppy_hfe.h"
#include <iostream>
#include <iomanip>
#include <fstream>
static std::uint64_t hash(const auto& data) {
    std::uint64_t h=14695981039346656037ull;for(auto b:data){h^=b;h*=1099511628211ull;}return h;
}
int main(int argc,char** argv)try {
    if(argc<3 || argc>4)throw std::invalid_argument("usage: midi_play_probe ROM-folder original-bank-image [PCM-output.wav]");
    const std::filesystem::path folder=argv[1];
    auto m=std::make_unique<eii::Machine>(0);
    m->load(folder/"main.bin",folder/"scanner.bin",folder/"ic135.bin",folder/"ic137.bin");
    m->initializeDramPowerOn(1);m->enableNormalRuntime();
    auto bytes=eii::readFile(argv[2]);
    if(bytes.size()>=8&&std::equal(bytes.begin(),bytes.begin()+8,"HXCPICFE"))bytes=eii::HfeImage(std::move(bytes)).extractEmuii();
    m->floppy.attach(0,37,400000);m->floppy.drives[0]->insertSensorDisk(true);
    m->diskRead=std::make_unique<eii::DiskReadPath>(87,8);
    m->diskRead->mount(0,std::make_shared<const eii::EmuiiImage>(std::move(bytes)),eii::EmuiiGap::Hxc2026);
    double peak=0,energy=0;std::uint64_t frames=0,nonzero=0;bool playing=false;
    std::vector<eii::host::StereoFrame> capture;
    std::array<double,8> maxAcv{};
    m->enableRuntimeAudio([&](auto block){frames+=block.size();for(auto f:block){peak=std::max(peak,double(std::abs(f.left)));
        if(playing){energy+=double(f.left)*f.left;if(f.left!=0)++nonzero;}}
        if(playing && argc==4)capture.insert(capture.end(),block.begin(),block.end());
        if(playing)for(unsigned ch=0;ch<8;++ch)maxAcv[ch]=std::max(maxAcv[ch],m->controlVoltage.held[0][ch].volts);
    });
    m->run(200000000);
    if(!m->channelTimers.controllerTiming.voiceTransactions)
        throw std::runtime_error("normal controller did not execute voice slots; invalid playback setup");
    std::cout<<"BOOT_BEGIN\n"<<m->report()<<"BOOT_FNV memory="<<hash(m->memory)<<" known="<<hash(m->dramKnown)<<" scanner="<<hash(m->scannerRam)<<"\nBOOT_END\n";
    std::cout<<"SCN2651 MR1/MR2/CR="<<std::hex<<unsigned(m->midiSerial.mode[0])<<"/"<<unsigned(m->midiSerial.mode[1])<<"/"<<unsigned(m->midiSerial.command)<<std::dec<<"\n";
    if(!m->midiSerial.enabled())throw std::runtime_error("firmware did not enable31250baud MIDI receiver");
    std::cout<<"PIOB mode/output/mask="<<unsigned(m->main.pio[1].mode)<<"/"<<unsigned(m->main.pio[1].output)<<"/"<<unsigned(m->main.pio[1].mask)<<" playback0="<<m->channel0PlaybackEnabled()<<"\n";
    const auto commands=m->commands;peak=0;frames=0;playing=true;
    for(unsigned note:{60,64}) {
        const std::array<std::uint8_t,3> message{0x90,std::uint8_t(note),100};
        m->receiveMidi(message);m->run(2000000);
        std::cout<<"NOTE_ON "<<note<<" UARTreceived="<<m->midiSerial.received<<" FWreads="<<m->midiSerial.reads
                 <<" commands="<<m->commands-commands<<" peak="<<std::setprecision(12)<<peak<<"\n";
    }
    std::cout<<"MAX_ACV ";for(auto v:maxAcv)std::cout<<v<<" ";std::cout<<"\n";
    const double notePeak=peak;
    for(unsigned note:{60,64}) {
        const std::array<std::uint8_t,3> message{0x80,std::uint8_t(note),0};m->receiveMidi(message);
    }
    m->run(4000000);
    std::cout<<"NOTE_OFF UARTreceived="<<m->midiSerial.received<<" FWreads="<<m->midiSerial.reads
             <<" overruns="<<m->midiSerial.overruns<<" rejected="<<m->midiSerial.rejected<<" frames="<<frames<<"\n";
    std::cout<<"PCM_STATS peak="<<peak<<" rms="<<std::sqrt(energy/frames)<<" nonzero="<<nonzero<<" frames="<<frames<<"\n";
    std::cout<<"FINAL_FNV memory="<<hash(m->memory)<<" known="<<hash(m->dramKnown)<<" scanner="<<hash(m->scannerRam)<<"\n";
    if(argc==4) {
        std::ofstream out(argv[3],std::ios::binary);
        auto u16=[&](unsigned n){out.put(char(n));out.put(char(n>>8));};
        auto u32=[&](unsigned n){u16(n);u16(n>>16);};
        const unsigned bytes=unsigned(capture.size()*4);
        out.write("RIFF",4);u32(36+bytes);out.write("WAVEfmt ",8);u32(16);
        u16(1);u16(2);u32(48000);u32(48000*4);u16(4);u16(16);
        out.write("data",4);u32(bytes);
        for(auto f:capture){u16(unsigned(std::lround(std::clamp(f.left,-1.f,1.f)*32767)));u16(unsigned(std::lround(std::clamp(f.right,-1.f,1.f)*32767)));}
        out.close();if(!out)throw std::runtime_error("PCM WAV export failed");
        std::cout<<"PCM_WAV frames="<<capture.size()<<" rate=48000 channels=2 format=PCM16; no normalization\n";
    }
    if(m->midiSerial.reads!=12||m->midiSerial.overruns||m->midiSerial.rejected||m->commands==commands||notePeak<=1e-9)
        throw std::runtime_error("MIDI firmware/PCM path did not meet playback checks");
    std::cout<<"PASS: original OS/bank + pacedMIDI ->SCN2651 ->CTC1 ->real firmware voice allocation ->DRAM/DAC/CV ->basicVCF/VCA ->nonzeroPCM; no note allocation bypass\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
