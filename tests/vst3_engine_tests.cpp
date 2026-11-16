// Portable checks for the VST3 transport pieces that need no ROMs: state text,
// sinc rate conversion, indexed frame ring and the host-clock engine without a
// machine (silence padding, offline anchoring, error path, teardown).
#include <memory>
#include "vst3/plugin_engine.h"
#include "vst3/post_output_gain.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace eii;
static int failures=0;
static void check(bool ok,const char* what){std::cout<<(ok?"PASS ":"FAIL ")<<what<<"\n";if(!ok)++failures;}

int main(){
    {auto heldEngine=std::make_unique<vst::PluginEngine>();auto& e=*heldEngine; // heap: 1MB Windows stack
     bool accepted=true;for(unsigned i=0;i<8192;++i)accepted&=e.queueVoltage(1000000,i%6,double(i%128)*5/127);
     check(accepted&&!e.queueVoltage(1000000,0,5),"panel handoff reports bounded queue admission/failure without blocking");}
    // A healthy DSP queue must remain pipelined; fragmented MIDI blocks must
    // still reserve enough slots for the complete next run, without overflow.
    check(vst::dspRunFits(3,24000),"DSP admission permits overlap with three queued blocks");
    check(!vst::dspRunFits(4,24000),"DSP admission prevents a full-quantum overflow");
    check(!vst::dspRunFits(7,1),"DSP admission accounts for tiny partial blocks");
    check(vst::dspRunFits(0,24000),"DSP admission permits an empty-queue run");
    {
        std::promise<void> started,release;auto done=release.get_future().share();
        std::atomic<bool> first{true};
        auto heldWorker=std::make_unique<analog::ChipAudioWorker>([&](auto){if(first.exchange(false)){started.set_value();done.wait();}});auto& worker=*heldWorker;
        analog::ChipAudioWorker::Input input{};worker.push(input);worker.flush();
        const bool entered=started.get_future().wait_for(std::chrono::seconds(5))==std::future_status::ready;
        check(entered,"DSP worker reached controlled in-flight block");
        if(entered){
            for(int i=0;i<3;++i){worker.push(input);worker.flush();}
            auto st=worker.stats();
            check(st.queuedBlocks==3&&st.queuedFrames==3&&vst::dspRunFits(st.queuedBlocks,24000),"fragmented queue admits safe pipelined work");
            worker.push(input);worker.flush();st=worker.stats();
            check(st.queuedBlocks==4&&!vst::dspRunFits(st.queuedBlocks,24000)&&st.dropped==0,"block-based backpressure stops before overflow");
        }
        release.set_value();
    }
    {
        vst::Signal signal;check(!signal.wait(0),"signal starts unsignaled");
        signal.set();check(signal.wait(2)&&!signal.wait(0),"signal wakes once and auto-resets");
        check(!signal.wait(2),"signal timeout returns without a wake");
    }
    // --- state ---
    vst::PluginSettings s;s.romFolder=vst::textPath("C:/ROMs/Emulátor II");s.floppy=vst::textPath("Z:/EII/disk/18 Grand Piano #2.hfe");
    s.audioMode=analog::RuntimeAudioMode::NonlinearChip192;s.nativeAudio=true;s.controls={1,2,3,4,2.5,0.125};s.postOutputDb=9;s.sampleInputTrimDb=12;s.volume=0.75;s.recallProgram=0xc005;s.marginMs=40;s.editorWidth=1700;s.editorHeight=659;
    s.calibration.values[0]=s.calibration.values[0]*0.5+analog::filterParameters[0].lo*0.5;
    s.calibration[analog::FilterParam::MixGain]=4;s.legacyCalibration[analog::FilterParam::MixGain]=4;
    const auto text=vst::serializeSettings(s);const auto back=vst::parseSettings(text);
    check(back==s,"settings serialize/parse round trip (UTF-8 paths, calibration)");
    check(vst::serializeSettings(back)==text,"serialization is canonical");
    bool threw=false;try{vst::parseSettings("garbage");}catch(...){threw=true;}check(threw,"foreign state rejected");

    check(vst::parseSettings("EIIVST3 1\n").sampleInputTrimDb==0,"old project state defaults to unity input calibration");
    check(vst::parseSettings("EIIVST3 1\nsampleinputtrimdb=100\n").sampleInputTrimDb==24,"input calibration state is bounded");
    for(auto mode:{analog::RuntimeAudioMode::LegacyQ,analog::RuntimeAudioMode::NonlinearChip,analog::RuntimeAudioMode::NonlinearChip192}){
        auto profile=back;profile.audioMode=mode;
        check(vst::activeCalibration(profile)[analog::FilterParam::MixGain]==4,"4x host master trim survives state restore for selected filter model");
    }
    {
        vst::PostOutputGain output;std::array<float,4096> l{},r{};l.fill(.5f);r.fill(-.25f);
        output.process(l.data(),r.data(),4096,0,48000);check(l.back()==.5f&&r.back()==-.25f,"final output unity preserves signal exactly");
        output.process(l.data(),r.data(),4096,12,48000);
        check(std::abs(l.back()/.5f-std::pow(10.,12./20))<.001&&l.back()>1&&r.back()<-.9f,"final output +12dB reaches expected gain with float headroom and stereo polarity");
        check(l.front()>.5f&&l.front()<l.back(),"final output changes smoothly rather than stepping");
        check(vst::parseSettings("EIIVST3 1\n").postOutputDb==0,"old projects retain unity final output");
    }
    // --- resampler: 48 kHz -> 44.1 kHz ---
    {
        vst::SincResampler r(44100);double err=0,ref=0,peak23=0;
        for(int f:{1000,18000,23000}){
            auto x=[&](std::int64_t i){return float(std::sin(2*3.14159265358979*f*double(i)/48000));};
            double maxOut=0;
            for(int j=200;j<4600;++j){
                const std::uint64_t num=std::uint64_t(j)*48000u;const auto ip=std::int64_t(num/44100);const double frac=double(num%44100)/44100;
                const float y=r.interpolate(frac,[&](int k){return x(ip-vst::SincResampler::K+1+k);});
                const double ideal=std::sin(2*3.14159265358979*f*double(j)/44100);
                if(f==1000){err+=(y-ideal)*(y-ideal);ref+=ideal*ideal;}maxOut=std::max(maxOut,double(std::abs(y)));
            }
            if(f==18000)check(std::abs(20*std::log10(maxOut))<0.1,"18 kHz passband within 0.1 dB");
            if(f==23000)peak23=maxOut;
        }
        check(10*std::log10(err/ref)<-90,"1 kHz conversion error below -90 dB");
        check(20*std::log10(peak23)<-60,"23 kHz (above 22.05 kHz Nyquist) attenuated by > 60 dB");
    }

    // --- frame ring: indexed reads, late frames discarded, no slip ---
    {
        vst::FrameRing ring;host::StereoFrame f;
        check(!ring.read(0,f)&&f.left==0,"missing frame reads as silence");
        std::array<host::StereoFrame,4> a{{{1,1},{2,2},{3,3},{4,4}}};ring.push(a);
        check(ring.read(3,f)&&f.left==4,"indexed read after a skip");
        std::array<host::StereoFrame,2> b{{{5,5},{6,6}}};ring.push(b);
        check(ring.read(5,f)&&f.left==6&&ring.written()==6,"stream index stays aligned");
    }

    {
        auto heldHistory=std::make_unique<vst::SamplingTrace>();auto& history=*heldHistory;vst::SamplingTrace::Row row{};
        history.push(row);check(!history.recording()&&!history.allocated(),"trace defaults off without history allocation");
        history.start();
        for(unsigned i=0;i<vst::SamplingTrace::capacity+7;++i){row[0]=i;history.push(row);}
        std::istringstream csv(history.csv());std::string line;std::getline(csv,line);unsigned rows=0;double first=-1,last=-1;bool width=true;
        while(std::getline(csv,line)){if(!rows)first=std::stod(line);last=std::stod(line);width&=std::count(line.begin(),line.end(),',')==int(vst::SamplingTrace::columns-1);++rows;}
        history.stop();const auto frozen=history.csv();history.push(row);check(history.csv()==frozen,"stopped trace does not collect rows");
        history.start();check(history.csv().find("\n")==history.csv().size()-1,"new trace clears previous history");
        check(rows==vst::SamplingTrace::capacity&&first==7&&last==vst::SamplingTrace::capacity+6&&width,"sampling trace wraps chronologically with complete fixed-width rows");
    }
    // --- engine without a machine ---
    {
        auto heldEngine=std::make_unique<vst::PluginEngine>();auto& e=*heldEngine;e.setupProcessing(48000,512);
        check(e.latencySamples()==512+1440,"48 kHz latency = block + 30 ms default margin");
        e.setupProcessing(44100,441);
        check(e.latencySamples()==441+1323+31,"44.1 kHz latency = block + 30 ms + kernel");
        e.setMarginMs(10);check(e.latencySamples()==441+441+31,"10 ms margin lowers the reported latency");e.setMarginMs(30);
        e.setupProcessing(48000,256);e.activate();
        std::vector<float> l(256),r(256);bool silent=true;
        for(int b=0;b<200;++b){e.render(l.data(),r.data(),256,true);for(float v:l)silent&=v==0;}
        auto st=e.status();
        check(silent&&e.underruns()==0,"offline render without machine: silent, no late frames");
        vst::PluginSettings bad;bad.romFolder="/nonexistent-eii-roms";e.boot(bad,false);
        for(int b=0;b<20;++b)e.render(l.data(),r.data(),256,true);
        for(int i=0;i<100&&e.status().panel.error.empty();++i)std::this_thread::sleep_for(std::chrono::milliseconds(5));
        st=e.status();
        check(!st.panel.error.empty()&&!st.panel.loaded,"missing ROM reported, machine not loaded");
        std::uint8_t m[3]{0x90,60,100};e.queueMidi(10,m,3);
        for(int b=0;b<20;++b)e.render(l.data(),r.data(),256,true);
        check(e.underruns()==0,"engine keeps pace after error");
        e.activate();for(int b=0;b<20;++b)e.render(l.data(),r.data(),256,true);
        check(e.status().anchored,"re-activation re-anchors the stream");
        check(!e.samplingTraceRecording(),"engine trace defaults off");
        e.activate();e.render(l.data(),r.data(),256,false);
        std::this_thread::sleep_for(std::chrono::milliseconds(25));e.render(l.data(),r.data(),256,false);
        for(int i=0;i<200&&e.status().callbacks!=2;++i)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        check(e.status().callbackMaxGapMs==0,"detailed callback timing stays inactive while trace is off");
        auto empty=e.samplingTrace().get();check(std::count(empty.begin(),empty.end(),'\n')==1,"disabled engine keeps no trace rows");
        auto beginTrace=e.startSamplingTrace();beginTrace.get();check(e.samplingTraceRecording(),"explicit trace start enables collection");
        e.activate();e.render(l.data(),r.data(),256,false);
        std::this_thread::sleep_for(std::chrono::milliseconds(25));e.render(l.data(),r.data(),256,false);
        for(int i=0;i<200&&(e.status().callbacks!=2||e.status().callbackMaxGapMs<20);++i)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        check(e.status().callbacks==2&&e.status().callbackMaxGapMs>=20,"diagnostics distinguish a host callback gap from owner work");
        auto trace=e.samplingTrace();check(trace.wait_for(std::chrono::seconds(2))==std::future_status::ready,"sampling diagnostic export answers asynchronously");
        const auto csv=trace.get();check(!e.samplingTraceRecording()&&std::count(csv.begin(),csv.end(),'\n')>1,"export stops active trace with recorded rows");
        const auto frozen=e.samplingTrace().get();check(frozen==csv,"disabled engine does not append history after stop");
        check(csv.find("input_missing")!=std::string::npos&&csv.find("native_overflows")!=std::string::npos&&csv.find("/nonexistent")==std::string::npos,"sampling export includes diagnostic counters without ROM paths");
    }
    std::cout<<(failures?"vst3 engine tests FAILED\n":"vst3 engine tests passed\n");
    return failures?1:0;
}
