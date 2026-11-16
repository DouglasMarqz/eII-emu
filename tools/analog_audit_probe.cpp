// S3528/SSM2045 audit laboratory (2026-10-29). Private ROM/bank paths are caller supplied.
// Captures ONE firmware run (reference 48k inputs + native event frames), then replays
// every route OFF/ON offline. Writes raw float32 PCM and a code log next to the given
// prefix (keep it outside git). Build with -DEII_PLAYBACK_TIMING_LAB=1.
#include "machine.h"
#include "floppy_hfe.h"
#include "host_audio_float.h"
#include <bit>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

using namespace eii;
static void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
struct StoredFrame{analog::NativePins initial;std::uint64_t start;bool valid;std::vector<analog::NativeEvent> events;};
static std::uint64_t fnv(const std::vector<float>& v){std::uint64_t h=14695981039346656037ull;for(float f:v){auto u=std::bit_cast<std::uint32_t>(f);for(unsigned i=0;i<4;++i){h^=std::uint8_t(u>>(8*i));h*=1099511628211ull;}}return h;}
int main(int argc,char** argv)try{
    require(argc==4,"usage: analog_audit_probe ROM-folder bank.hfe out-prefix");
    const std::string prefix=argv[3];
    const auto image=HfeImage(readFile(argv[2])).extractEmuii();
    auto machine=std::make_unique<Machine>(0);auto& m=*machine;
    const std::filesystem::path rom=argv[1];
    m.channelTimers.enableControllerTiming();m.channelTimers.controllerTiming.enableRegisterWrites();
    m.load(rom/"main.bin",rom/"scanner.bin",rom/"ic135.bin",rom/"ic137.bin");
    m.initializeDramPowerOn(1);m.enableProductionController();m.initializeControllerPowerOn(0xe1122026u);m.enableNormalRuntime();
    m.floppy.attach(0,37,400000);m.floppy.drives[0]->insertSensorDisk(true);
    m.diskRead=std::make_unique<DiskReadPath>(87,8);
    m.diskRead->mount(0,std::make_shared<const EmuiiImage>(image),EmuiiGap::Hxc2026);
    while(m.ticks<240000000){m.run(1000000);
        if(m.diskDma.transfers>=500000&&m.lcd.line(0).starts_with("P01")&&!m.floppy.drives[0]->selected())break;}
    require(m.lcd.line(0).starts_with("P01"),"firmware bank boot failed");m.run(4000000);
    std::cout<<"BOOT_OK tick="<<m.ticks<<" LCD=["<<m.lcd.line(0)<<"]["<<m.lcd.line(1)<<"]"<<std::endl;
    const analog::FilterCalibration nominal{};
    m.enableRuntimeAudio([](auto){},analog::RuntimeAudioMode::NonlinearChip);
    m.calibrateAudio(nominal);m.reconstruction(true);m.nativeAudio(true);
    std::vector<StoredFrame> tape;tape.reserve(400000);
    m.nativeFrameObserver([&](const analog::NativeFrame& f){tape.push_back({f.initial,f.start,f.valid,{f.events.begin(),f.events.begin()+f.count}});});
    std::vector<analog::PlaybackVoiceBank::Input> ref;ref.reserve(400000);
    std::ofstream log(prefix+"_codes.txt");
    const std::array<unsigned,7> notes={36,48,60,72,84,96,108};
    constexpr unsigned rate=48000,noteFrames=rate*7/10,gapFrames=rate*3/10;
    const unsigned total=unsigned(notes.size())*(noteFrames+gapFrames)+rate/2;
    const auto origin=m.ticks;
    for(unsigned n=0;n<total;++n){
        const unsigned slot=n/(noteFrames+gapFrames),phase=n%(noteFrames+gapFrames);
        if(slot<notes.size()){
            if(phase==0){const std::array<std::uint8_t,3> on={0x90,std::uint8_t(notes[slot]),110};require(m.receiveMidi(on),"MIDI on");}
            if(phase==noteFrames){const std::array<std::uint8_t,3> off={0x80,std::uint8_t(notes[slot]),0};require(m.receiveMidi(off),"MIDI off");}
        }
        const auto target=origin+(std::uint64_t(n+1)*4000000)/rate;m.run(target-m.ticks);
        analog::PlaybackVoiceBank::Input in;in.playback=m.playbackAnalogInputs();in.channel0Playback=m.channel0AudioEnabled();
        for(unsigned ch=0;ch<8;++ch){const auto& f=m.controlVoltage.held[1][ch];const auto& q=m.controlVoltage.held[2][ch];
            in.frequency[ch]={f.volts,f.known};in.resonance[ch]={q.volts,q.known};in.reconstructionCode[ch]=m.reconstructionControl[ch].activeCode();}
        ref.push_back(in);
        if(slot<notes.size()&&(phase==rate/10||phase==noteFrames-1)){
            log<<"note="<<notes[slot]<<" t="<<(phase==rate/10?"100ms":"end")<<" LCD=["<<m.lcd.line(0)<<"]\n";
            for(unsigned ch=0;ch<8;++ch){const auto c=m.reconstructionControl[ch].activeCode();const auto fc=m.reconstructionControl[ch].cutoff();
                const auto& a=m.controlVoltage.held[0][ch];const auto& f=m.controlVoltage.held[1][ch];const auto& q=m.controlVoltage.held[2][ch];
                log<<"  ch"<<ch<<" code="<<unsigned(c.value)<<"/"<<unsigned(c.known)<<" fc="<<(fc.known?fc.value:-1)<<" writes="<<m.reconstructionControl[ch].writes
                   <<std::setprecision(4)<<" ACV="<<a.volts<<" FCV="<<f.volts<<" QCV="<<q.volts<<"\n";}
        }
    }
    m.nativeFrameObserver({});
    std::cout<<"CAPTURE frames="<<ref.size()<<" native="<<tape.size()<<" overflow="<<m.nativeAudioOverflows()<<" order="<<m.nativeAudioOrderingErrors()
             <<" invalid="<<m.channelTimers.controllerTiming.productionEngineInvalid()<<" midi="<<m.midiSerial.received<<"/"<<m.midiSerial.overruns<<std::endl;
    require(!m.nativeAudioOverflows()&&!m.nativeAudioOrderingErrors(),"native capture fault");
    const host::AudioFloatScope floats;
    auto write=[&](const std::string& name,const std::vector<float>& pcm){std::ofstream o(prefix+"_"+name+".f32",std::ios::binary);o.write(reinterpret_cast<const char*>(pcm.data()),std::streamsize(pcm.size()*4));};
    auto report=[&](const std::string& name,const std::vector<float>& off,const std::vector<float>& on){
        double po=0,pn=0,pd=0,pk=0,pkOn=0;unsigned changed=0,clipOn=0;
        for(unsigned i=0;i<off.size();++i){po+=double(off[i])*off[i];pn+=double(on[i])*on[i];const double d=double(on[i])-off[i];pd+=d*d;changed+=off[i]!=on[i];
            pk=std::max(pk,double(std::abs(off[i])));pkOn=std::max(pkOn,double(std::abs(on[i])));clipOn+=std::abs(on[i])>=1;}
        double pi=0;for(unsigned i=0;i<off.size();++i){const double d=double(on[i])+off[i];pi+=d*d;}
        std::cout<<std::setprecision(6)<<"ROUTE "<<name<<" frames="<<off.size()<<" rmsOff="<<std::sqrt(po/off.size())<<" rmsOn="<<std::sqrt(pn/off.size())
            <<" peakOff="<<pk<<" peakOn="<<pkOn<<" clipOn="<<clipOn<<" diffRel_dB="<<10*std::log10(std::max(1e-30,pd/po))<<" invertedDiffRel_dB="<<10*std::log10(std::max(1e-30,pi/po))
            <<" changed="<<changed<<" fnvOff="<<std::hex<<fnv(off)<<" fnvOn="<<fnv(on)<<std::dec<<std::endl;
        write(name+"_off",off);write(name+"_on",on);};
    { // Legacy BasicVoice (48k, worker-free), as RuntimeAudioEngine::sample.
        std::array<std::vector<float>,2> pcm;
        for(unsigned pass=0;pass<2;++pass){std::vector<analog::BasicPlaybackVoice> v(8,analog::BasicPlaybackVoice(48000));
            for(const auto& in:ref){double s=0;for(unsigned ch=0;ch<8;++ch){if(ch==0&&!in.channel0Playback)continue;
                    s+=v[ch].process(in.playback.codes[ch],in.playback.amplitudeCv[ch],in.frequency[ch],in.reconstructionCode[ch],pass==1);}
                pcm[pass].push_back(float(std::clamp(s,-1.0,1.0)));}}
        report("legacy48",pcm[0],pcm[1]);
    }
    { // deglitch output Y statistics on the reference route (what drives R138)
        double ymax=0;size_t over=0,count=0;
        for(const auto& in:ref)for(unsigned ch=0;ch<8;++ch){if(in.playback.codes[ch].known!=255||!in.playback.amplitudeCv[ch].known)continue;
            const double y=std::abs(-2490*analog::Am6072::signedDecodeCurrent(in.playback.codes[ch].value,in.playback.amplitudeCv[ch].value/9400)*(-10000.0/200));
            ymax=std::max(ymax,y);over+=y>13.5;++count;}
        std::cout<<"DEGLITCH_Y reference-route max |Y|="<<ymax<<" V, samples beyond the 13.5 V TL084 swing="<<over<<"/"<<count<<std::endl;
    }
    struct Route{const char* name;unsigned factor;bool legacyQ,eiii,native,m2=false;};
    for(const Route r:{Route{"ssm96",2,false,false,false},Route{"ssm192",4,false,false,false},Route{"legacyq96",2,true,false,false},Route{"eiii96",2,false,true,false},
                       Route{"ssm96native",2,false,false,true},Route{"ssm192native",4,false,false,true},Route{"legacyq96native",2,true,false,true},Route{"eiii96native",2,false,true,true},
                       Route{"ssm2m96",2,false,false,false,true},Route{"ssm2m192",4,false,false,false,true},Route{"ssm2m96native",2,false,false,true,true},Route{"ssm2m192native",4,false,false,true,true}}){
        std::array<std::vector<float>,2> pcm;
        for(unsigned pass=0;pass<2;++pass){auto bank=std::make_unique<analog::PlaybackVoiceBank>(r.factor,r.legacyQ,r.eiii,r.m2);bank->calibrate(nominal);
            const unsigned count=r.native?unsigned(std::min(tape.size(),ref.size())):unsigned(ref.size());pcm[pass].reserve(count);
            for(unsigned n=0;n<count;++n){analog::PlaybackVoiceBank::Input in;
                if(r.native){const auto& t=tape[n];in.native.enabled=true;in.native.initial=t.initial;in.native.start=t.start;in.native.valid=t.valid;
                    in.native.count=std::uint16_t(t.events.size());std::copy(t.events.begin(),t.events.end(),in.native.events.begin());}
                else in=ref[n];
                in.reconstructionEnabled=pass==1;const float y=bank->process(in);require(std::isfinite(y),"nonfinite");pcm[pass].push_back(y);}
            const auto s=bank->solverStats();std::cout<<"  "<<r.name<<" pass="<<pass<<" solverFallbacks="<<s.fallbacks<<" held="<<s.held<<std::endl;}
        report(r.name,pcm[0],pcm[1]);
    }
    return 0;
}catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<std::endl;return 1;}
