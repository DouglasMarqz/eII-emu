// DAC timing and full PCM audit: firmware-derived inputs replayed OFF/repeat/ON at96/192k.
// Aggregate counters only; requires caller-supplied private media outsidegit.
// Does not modify emulator source, reset firmware, write media or export audio.
#include "machine.h"
#include "floppy_hfe.h"
#include "host_thread_cpu.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <memory>
#include <vector>
#include <bit>
static std::uint64_t hashInput(std::uint64_t h,const eii::analog::NonlinearVoiceBank::Input& in){
    auto add=[&](std::uint64_t n){for(unsigned i=0;i<8;++i){h^=std::uint8_t(n>>(i*8));h*=1099511628211ull;}};
    add(in.channel0Playback);
    for(auto c:in.reconstructionCode){add(c.value);add(c.known);}
    for(unsigned ch=0;ch<8;++ch){add(in.playback.codes[ch].value);add(in.playback.codes[ch].known);
        for(auto v:{in.playback.amplitudeCv[ch],in.frequency[ch],in.resonance[ch]}){add(v.known);add(std::bit_cast<std::uint64_t>(v.value));}}
    return h;
}
int main(int argc,char** argv)try {
    if(argc!=4)throw std::runtime_error("usage: playback_fidelity_probe ROM-folder bank.hfe calibration.txt");
    std::ifstream profileFile(argv[3]);if(!profileFile)throw std::runtime_error("profile open failed");
    const auto profile=eii::analog::FilterCalibration::parse({std::istreambuf_iterator<char>(profileFile),{}});
    const auto image=eii::HfeImage(eii::readFile(argv[2])).extractEmuii();
    auto ptr=std::make_unique<eii::Machine>(0);auto& m=*ptr;std::filesystem::path rom=argv[1];
    m.channelTimers.enableControllerTiming();m.channelTimers.controllerTiming.enableRegisterWrites();
    m.load(rom/"main.bin",rom/"scanner.bin",rom/"ic135.bin",rom/"ic137.bin");
    m.initializeDramPowerOn(1);m.enableProductionController();m.initializeControllerPowerOn(0xe1122026u);m.enableNormalRuntime();
    m.floppy.attach(0,37,400000);m.floppy.drives[0]->insertSensorDisk(true);m.diskRead=std::make_unique<eii::DiskReadPath>(87,8);
    m.diskRead->mount(0,std::make_shared<const eii::EmuiiImage>(image),eii::EmuiiGap::Hxc2026);
    while(m.ticks<240000000){m.run(1000000);if(m.ticks%40000000==0)std::cout<<"BOOT_PROGRESS tick="<<m.ticks<<" LCD=["<<m.lcd.line(0)<<"] DMA="<<m.diskDma.transfers<<std::endl;
        if(m.diskDma.transfers>=500000&&m.lcd.line(0).starts_with("P01")&&!m.floppy.drives[0]->selected())break;}
    if(!m.lcd.line(0).starts_with("P01"))throw std::runtime_error("bank failed to reach preset screen");
    m.run(4000000);
    std::cout<<"BOOT_OK tick="<<m.ticks<<" LCD=["<<m.lcd.line(0)<<"] DMA="<<m.diskDma.transfers<<std::endl;
    constexpr unsigned rate=48000,seconds=8;
    std::vector<eii::analog::NonlinearVoiceBank::Input> tape;tape.reserve(rate*seconds);
    const auto origin=m.ticks;std::uint64_t hash=14695981039346656037ull;
    for(unsigned frame=0;frame<rate*seconds;++frame){
        if(frame==rate)m.scannerControls.setButton(eii::ScannerButton::Play,true);
        if(frame==rate+rate/5)m.scannerControls.setButton(eii::ScannerButton::Play,false);
        const auto target=origin+(std::uint64_t(frame+1)*4000000+rate-1)/rate;m.run(target-m.ticks);
        eii::analog::NonlinearVoiceBank::Input in;in.playback=m.playbackAnalogInputs();in.channel0Playback=m.channel0PlaybackEnabled();
        for(unsigned ch=0;ch<8;++ch){auto f=m.controlVoltage.held[eii::ControlVoltageBus::Frequency][ch];auto q=m.controlVoltage.held[eii::ControlVoltageBus::Resonance][ch];in.frequency[ch]={f.volts,f.known};in.resonance[ch]={q.volts,q.known};}
        for(unsigned ch=0;ch<8;++ch){in.reconstructionCode[ch]=m.reconstructionControl[ch].activeCode();if(in.reconstructionCode[ch].known!=63)throw std::runtime_error("unknown firmware S3528 code");}
        hash=hashInput(hash,in);tape.push_back(in);
        if((frame+1)%rate==0)std::cout<<"CAPTURE second="<<(frame+1)/rate<<" LCD=["<<m.lcd.line(0)<<"]["<<m.lcd.line(1)<<"]"<<std::endl;
    }
    std::cout<<"PINS frames="<<tape.size()<<" hash="<<std::hex<<hash<<std::dec<<" invalid="<<m.channelTimers.controllerTiming.productionEngineInvalid()<<std::endl;
    // End-to-end renderer transport: identical tape, actual worker and bounded
    // stereo PCM queue. Wait only in this offline harness, never in firmware.
    for(unsigned factor:{2u,4u}){
        std::vector<eii::host::StereoFrame> reference;
        for(unsigned pass=0;pass<3;++pass){
            const bool enabled=pass==2;
            auto queue=std::make_shared<eii::host::AudioQueue>(1056);
            eii::analog::RuntimeAudioEngine engine([queue](auto frames){queue->push(frames);},
                factor==2?eii::analog::RuntimeAudioMode::NonlinearChip:eii::analog::RuntimeAudioMode::NonlinearChip192);
            engine.calibrate(profile);engine.reconstruction(enabled);
            std::vector<eii::host::StereoFrame> pcm;pcm.reserve(tape.size());
            for(unsigned begin=0;begin<tape.size();begin+=96){
                const unsigned count=std::min(96u,unsigned(tape.size())-begin);
                for(unsigned n=begin;n<begin+count;++n){const auto& in=tape[n];
                    engine.sample(in.playback,in.frequency,in.channel0Playback,in.resonance,in.reconstructionCode);}
                engine.flush();const auto timeout=std::chrono::steady_clock::now()+std::chrono::seconds(10);
                while(queue->stats().queued<count){
                    if(engine.chipStats().failures||std::chrono::steady_clock::now()>timeout)throw std::runtime_error("PCM worker failure/timeout");
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
                std::array<eii::host::StereoFrame,96> frames{};queue->pop(std::span(frames.data(),count));
                pcm.insert(pcm.end(),frames.begin(),frames.begin()+count);
            }
            std::uint64_t changed=0,hashPcm=14695981039346656037ull;double error=0,energy=0,peakError=0;
            for(unsigned n=0;n<pcm.size();++n){const auto a=pcm[n];
                if(!std::isfinite(a.left)||a.left!=a.right)throw std::runtime_error("invalid stereo PCM");
                const auto bits=std::bit_cast<std::uint32_t>(a.left);for(unsigned b=0;b<4;++b){hashPcm^=std::uint8_t(bits>>(b*8));hashPcm*=1099511628211ull;}
                if(pass){changed+=bits!=std::bit_cast<std::uint32_t>(reference[n].left);
                    const double d=double(a.left)-reference[n].left;error+=d*d;peakError=std::max(peakError,std::abs(d));}
                energy+=double(a.left)*a.left;
            }
            if(!pass)reference=pcm;
            if((pass==1&&changed)||(enabled&&!changed)||energy==0)throw std::runtime_error("PCM A/B/determinism failed");
            const auto q=queue->stats();const auto dsp=engine.chipStats();
            if(q.missing||q.dropped||dsp.dropped||dsp.failures)throw std::runtime_error("PCM transport lost data");
            std::cout<<std::setprecision(12)<<"PCM factor="<<factor<<" pass="<<pass<<" S3528="<<enabled<<" frames="<<pcm.size()<<" changedBitsFrames="<<changed
                <<" hash="<<std::hex<<hashPcm<<std::dec<<" rms="<<std::sqrt(energy/pcm.size())<<" rmsDifference="<<std::sqrt(error/pcm.size())
                <<" peakDifference="<<peakError<<" fallback="<<dsp.fallbacks<<" held="<<dsp.held<<" missing="<<q.missing<<" dropped="<<q.dropped<<std::endl;
        }
    }
    // Timing audit only: the normal GUI never installs this observer. Compare
    // exact50ns held-code areas to the current CPU-boundary48k endpoint pickup.
    for(bool highChord:{false,true}){
    if(highChord){
        m.scannerControls.setButton(eii::ScannerButton::Stop,true);m.run(800000);
        m.scannerControls.setButton(eii::ScannerButton::Stop,false);m.run(800000);
        const std::array<std::uint8_t,3> off{0xb0,123,0};m.receiveMidi(off);m.run(400000);
        for(unsigned note=84;note<92;++note){const std::array<std::uint8_t,3> message{0x90,std::uint8_t(note),100};m.receiveMidi(message);}
        m.run(80000);
        std::cout<<"CHORD UARTreceived="<<m.midiSerial.received<<" read="<<m.midiSerial.reads<<" overruns="<<m.midiSerial.overruns<<std::endl;
    }
    struct ChannelAudit{
        eii::KnownByte previous{};eii::LogicLevel ack{},p1{},p2{};
        std::uint64_t changes=0,latches=0,phaseChanges=0,unknown=0,nonzero=0,frames=0,differentAreaFrames=0;
        std::uint64_t changesInFrame=0,maxChanges=0,periodMin=UINT64_MAX,periodMax=0,lastLatch=0;
        std::uint64_t p1FromLatchMin=UINT64_MAX,p1FromLatchMax=0,p2FromP1Min=UINT64_MAX,p2FromP1Max=0,lastP1=0;
        std::uint64_t latchDuringPhase=0,phaseOverlap=0,phaseUnknown=0;
        std::int64_t area=0;bool frameKnown=true;
    };
    std::array<ChannelAudit,8> audit{};std::uint64_t intervals=0,frameIntervals=0;
    for(unsigned ch=0;ch<8;++ch){auto& a=audit[ch];a.previous=m.channelTimers.controllerTiming.channelDacCode(ch);
        a.ack=m.channelTimers.serviceBus.acknowledge[ch];a.p1=m.channelTimers.channels[ch].phi1();a.p2=m.channelTimers.channels[ch].phi2();}
    m.playbackTimingObserver([&](std::uint64_t edge){
        ++intervals;++frameIntervals;
        for(unsigned ch=0;ch<8;++ch){auto& a=audit[ch];const auto code=m.channelTimers.controllerTiming.channelDacCode(ch);
            const auto ack=m.channelTimers.serviceBus.acknowledge[ch],p1=m.channelTimers.channels[ch].phi1(),p2=m.channelTimers.channels[ch].phi2();
            if(a.p1.known&&!a.p1.high&&p1.known&&p1.high){
                if(a.lastLatch){a.p1FromLatchMin=std::min(a.p1FromLatchMin,edge-a.lastLatch);a.p1FromLatchMax=std::max(a.p1FromLatchMax,edge-a.lastLatch);}a.lastP1=edge;}
            if(a.p2.known&&!a.p2.high&&p2.known&&p2.high&&a.lastP1){a.p2FromP1Min=std::min(a.p2FromP1Min,edge-a.lastP1);a.p2FromP1Max=std::max(a.p2FromP1Max,edge-a.lastP1);}
            if(code!=a.previous){++a.changes;++a.changesInFrame;}
            if(a.ack.known&&!a.ack.high&&ack.known&&ack.high){++a.latches;
                a.latchDuringPhase+=(p1.known&&p1.high)||(p2.known&&p2.high);
                if(a.lastLatch){a.periodMin=std::min(a.periodMin,edge-a.lastLatch);a.periodMax=std::max(a.periodMax,edge-a.lastLatch);}a.lastLatch=edge;}
            a.phaseOverlap+=p1.known&&p2.known&&p1.high&&p2.high;a.phaseUnknown+=!p1.known||!p2.known;
            a.phaseChanges+=(a.p1!=p1)+(a.p2!=p2);
            if(code.known==255){const int units=eii::analog::Am6072::signedUnits(code.value);a.area+=units;a.nonzero+=units!=0;}
            else{++a.unknown;a.frameKnown=false;}
            a.previous=code;a.ack=ack;a.p1=p1;a.p2=p2;
        }
    });
    const auto timingOrigin=m.ticks;
    for(unsigned frame=0;frame<4800;++frame){
        const auto target=timingOrigin+(std::uint64_t(frame+1)*4000000+47999)/48000;m.run(target-m.ticks);
        for(unsigned ch=0;ch<8;++ch){auto& a=audit[ch];const auto code=m.channelTimers.controllerTiming.channelDacCode(ch);
            if(a.frameKnown&&code.known==255){++a.frames;a.differentAreaFrames+=a.area!=std::int64_t(eii::analog::Am6072::signedUnits(code.value))*std::int64_t(frameIntervals);}
            a.maxChanges=std::max(a.maxChanges,a.changesInFrame);a.changesInFrame=0;a.area=0;a.frameKnown=true;
        }frameIntervals=0;
    }
    m.playbackTimingObserver({});
    for(unsigned ch=0;ch<8;++ch){const auto& a=audit[ch];std::cout<<"TIMING chord="<<highChord<<" ch="<<ch<<" reload="<<m.channelTimers.counters[ch].reload<<" intervals="<<intervals<<" knownFrames="<<a.frames<<" areaDiffFrames="<<a.differentAreaFrames
        <<" codeChanges="<<a.changes<<" maxChangesPer48k="<<a.maxChanges<<" ACKlatches="<<a.latches<<" latchMin50ns="<<a.periodMin<<" latchMax50ns="<<a.periodMax
        <<" phaseChanges="<<a.phaseChanges<<" unknown="<<a.unknown<<" nonzero="<<a.nonzero
        <<" latchDuringPhase="<<a.latchDuringPhase<<" phaseOverlap="<<a.phaseOverlap<<" phaseUnknown="<<a.phaseUnknown
        <<" latchToPhi1Min50ns="<<a.p1FromLatchMin<<" latchToPhi1Max50ns="<<a.p1FromLatchMax
        <<" phi1ToPhi2Min50ns="<<a.p2FromP1Min<<" phi1ToPhi2Max50ns="<<a.p2FromP1Max<<std::endl;}
    if(intervals!=2000000||m.channelTimers.controllerTiming.productionEngineInvalid())throw std::runtime_error("invalid timing capture");
    }
    return 0;
}catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<std::endl;return 1;}
