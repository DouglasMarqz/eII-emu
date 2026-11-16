// Sample input step 7: offline WAV -> modeled +INPUT.V -> original OS3.1
// Sample overlay (real firmware, panel keys only) -> SAR/-IWAIT/INI -> DRAM ->
// playback. Caller-supplied ROMs, OS disk and WAV; nothing is injected into
// memory or CPU state. The WAV only drives the 7-42 input pin voltage.
// usage: sample_wav_probe ROMDIR OSDISK WAV OUTPREFIX [reference|production|both]
//        [volts_full_scale=1.0] [record_seconds=2] [midi_note=60] [legacy|native]
//        [arm|force] [gain_slider_volts=-1: press 3 (GAIN SET) and move slider A]
//        [offline | live:<speed>]  live: replay a realistic capture timeline
//        through LiveSampleInput (10 ms packets, latency jitter, late bursts),
//        the machine at <speed> x real time with slow periods and stalls,
//        paced by input availability exactly as the GUI worker does.
// Analysis: THD+N/harmonics of the monitor, the DPCM-decoded DRAM and the
// playback, and phase continuity in 10 ms blocks (a splice, skip or repeat
// of captured input shows as a phase jump).
// Writes OUTPREFIX_<engine>_play.wav (48 kHz BasicVoice output) and prints
// LCD, DRAM change/hash, DPCM-decoded tone and playback tone estimates.
#include "floppy_hfe.h"
#include "machine.h"
#include "sample_input_live.h"
#include <functional>
#include <memory>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
using B=eii::ScannerButton;
static std::vector<float> readWav(const std::string& path,double& rate){
    auto bytes=eii::readFile(path);
    if(bytes.size()<44||std::memcmp(bytes.data(),"RIFF",4)||std::memcmp(bytes.data()+8,"WAVE",4))throw std::runtime_error("not a RIFF/WAVE file");
    unsigned channels=0,bits=0,format=0;std::size_t pos=12;std::vector<float> out;
    auto u16=[&](std::size_t p){return unsigned(bytes[p]|(bytes[p+1]<<8));};
    auto u32=[&](std::size_t p){return std::uint32_t(u16(p)|(u16(p+2)<<16));};
    while(pos+8<=bytes.size()){
        const auto size=u32(pos+4);const auto body=pos+8;
        if(!std::memcmp(bytes.data()+pos,"fmt ",4)){format=u16(body);channels=u16(body+2);rate=u32(body+4);bits=u16(body+14);}
        else if(!std::memcmp(bytes.data()+pos,"data",4)){
            if(format!=1||(bits!=16&&bits!=24)||!channels)throw std::runtime_error("WAV must be PCM 16/24-bit");
            const unsigned step=bits/8*channels;
            for(std::size_t p=body;p+step<=body+size&&p+step<=bytes.size();p+=step){
                const int v=bits==16?int(std::int16_t(u16(p))):(int(std::int32_t((bytes[p]<<8)|(bytes[p+1]<<16)|(bytes[p+2]<<24))>>8));
                out.push_back(float(v/(bits==16?32768.0:8388608.0))); // first channel
            }
        }
        pos=body+size+(size&1);
    }
    if(out.empty())throw std::runtime_error("WAV has no samples");
    return out;
}
static void writeWav(const std::string& path,const std::vector<float>& s,unsigned rate){
    std::ofstream f(path,std::ios::binary);auto w32=[&](std::uint32_t v){f.write(reinterpret_cast<const char*>(&v),4);};auto w16=[&](std::uint16_t v){f.write(reinterpret_cast<const char*>(&v),2);};
    f.write("RIFF",4);w32(36+std::uint32_t(s.size()*2));f.write("WAVEfmt ",8);w32(16);w16(1);w16(1);w32(rate);w32(rate*2);w16(2);w16(16);f.write("data",4);w32(std::uint32_t(s.size()*2));
    for(float x:s){const auto v=std::int16_t(std::lround(std::clamp(x,-1.0f,1.0f)*32767));w16(std::uint16_t(v));}
}
// Dominant frequency by zero crossings of the mean-removed signal.
static double toneHz(const std::vector<double>& y,double rate){
    if(y.size()<4)return 0;
    double m=0;for(double v:y)m+=v;m/=double(y.size());
    std::size_t first=0,last=0,n=0;for(std::size_t i=1;i<y.size();++i)if((y[i-1]-m<0)&&(y[i]-m>=0)){if(!n)first=i;last=i;++n;}
    return n>1?(n-1)*rate/double(last-first):0;
}
// Least-squares sine fit (refined frequency) -> THD+N, harmonics, noise.
static constexpr double pi=3.14159265358979323846;
struct Fit{double f=0,amp=0,thdn=0,noise=0;double h[6]{};};
static double project(const std::vector<double>& y,double w,double& a,double& b){
    double ss=0,cc=0,sc=0,ys=0,yc=0;for(std::size_t i=0;i<y.size();++i){const double s=std::sin(w*double(i)),c=std::cos(w*double(i));ss+=s*s;cc+=c*c;sc+=s*c;ys+=y[i]*s;yc+=y[i]*c;}
    const double d=ss*cc-sc*sc;a=(ys*cc-yc*sc)/d;b=(yc*ss-ys*sc)/d;return a*a+b*b;
}
static Fit sineFit(std::vector<double> y,double rate){
    Fit r;if(y.size()<64)return r;double m=0;for(double v:y)m+=v;m/=double(y.size());for(double& v:y)v-=m;
    // coarse scan +/-10 % around the zero-crossing estimate, then golden section
    const double z=toneHz(y,rate);double a,b,best=0,bf=z;
    for(int k=-100;k<=100;++k){const double f=z*(1+0.001*k);if(f<=0)continue;const double e=project(y,2*pi*f/rate,a,b);if(e>best){best=e;bf=f;}}
    double lo=bf-z*0.001,hi=bf+z*0.001;
    for(int k=0;k<60;++k){const double m1=lo+(hi-lo)*0.382,m2=lo+(hi-lo)*0.618;if(project(y,2*pi*m1/rate,a,b)>project(y,2*pi*m2/rate,a,b))hi=m2;else lo=m1;}
    r.f=(lo+hi)/2;std::vector<double> res=y;
    auto remove=[&](double f,double& amp){project(res,2*pi*f/rate,a,b);amp=std::sqrt(a*a+b*b);for(std::size_t i=0;i<res.size();++i)res[i]-=a*std::sin(2*pi*f/rate*double(i))+b*std::cos(2*pi*f/rate*double(i));};
    remove(r.f,r.amp);double e=0;for(double v:res)e+=v*v;
    r.thdn=10*std::log10(e/double(res.size())/(r.amp*r.amp/2));
    for(int k=2;k<=5;++k){double amp=0;if(k*r.f<rate/2)remove(k*r.f,amp);r.h[k]=amp>0?20*std::log10(amp/r.amp):-999;}
    e=0;for(double v:res)e+=v*v;r.noise=10*std::log10(e/double(res.size())/(r.amp*r.amp/2));
    return r;
}
static void printFit(const char* name,const char* what,const std::vector<double>& y,double rate){
    const auto f=sineFit(y,rate);
    std::printf("[%s] %s: %.2f Hz amp %.4f THD+N %.1f dB (H2 %.1f H3 %.1f H4 %.1f H5 %.1f dBc; noise w/o H2..5 %.1f dB)\n",name,what,f.f,f.amp,f.thdn,f.h[2],f.h[3],f.h[4],f.h[5],f.noise);
}
// Phase continuity in 10 ms blocks at the fitted frequency.
static void continuity(const char* name,const char* what,const std::vector<double>& y,double rate){
    const auto f=sineFit(y,rate);if(f.amp<=0){std::printf("[%s] %s: no tone\n",name,what);return;}
    const std::size_t n=std::size_t(rate*0.01);double prev=0,maxJump=0;std::size_t jumps=0,blocks=0,weak=0;
    for(std::size_t s0=0;s0+n<=y.size();s0+=n){
        double ss=0,sc=0,cc=0,ys=0,yc=0,mean=0;for(std::size_t i=0;i<n;++i)mean+=y[s0+i];mean/=double(n);
        for(std::size_t i=0;i<n;++i){const double w=2*pi*f.f/rate*double(s0+i),s=std::sin(w),c=std::cos(w),v=y[s0+i]-mean;ss+=s*s;cc+=c*c;sc+=s*c;ys+=v*s;yc+=v*c;}
        const double d=ss*cc-sc*sc,a=(ys*cc-yc*sc)/d,b=(yc*ss-ys*sc)/d,amp=std::sqrt(a*a+b*b),ph=std::atan2(b,a);
        if(amp<0.5*f.amp){++weak;blocks=0;continue;}
        if(blocks++){double dp=ph-prev;while(dp>pi)dp-=2*pi;while(dp<-pi)dp+=2*pi;maxJump=std::max(maxJump,std::fabs(dp));
            if(std::fabs(dp)>0.25){if(++jumps<=8)std::printf("[%s] %s: phase jump %.2f rad at sample %zu (%.3f s)\n",name,what,dp,s0,double(s0)/rate);}}
        prev=ph;
    }
    std::printf("[%s] %s continuity: %zu x 10 ms blocks, phase jumps >0.25 rad: %zu, max |dphase| %.3f rad, weak blocks %zu\n",name,what,y.size()/n,jumps,maxJump,weak);
}
// Simulated host: capture packets and machine speed on one deterministic clock.
static std::int64_t g_simNs=0;
static std::int64_t simClock(){return g_simNs;}
struct LiveSim{
    std::shared_ptr<eii::analog::LiveSampleInput> in;const std::vector<float>* wav=nullptr;double speed=1;
    std::size_t src=0,packet=0;std::int64_t start=0,lastDelivery=0;std::uint32_t rng=0x1234567u;std::uint64_t t0=0,maxBuffered=0,quanta=0;
    double rnd(){rng=rng*1664525u+1013904223u;return double(rng>>8)/16777216.0;}
    void deliver(){
        for(;;){
            // packet k is captured at k*10 ms; delivered 10..14 ms later, every
            // 97th one 45 ms late (the following ones queue behind it).
            std::int64_t d=start+std::int64_t(packet)*10'000'000+10'000'000+std::int64_t(rnd()*4e6);
            if(packet%97==96)d+=45'000'000;
            d=std::max(d,lastDelivery);if(d>g_simNs)return;lastDelivery=d;
            std::array<float,480> b{};for(auto& v:b){v=src<wav->size()?(*wav)[src]:0.0f;++src;}
            in->push(b);++packet;
        }
    }
    // machine capacity: `speed`, halved for 300 ms of every 2 s, plus a 45 ms host stall every 621 quanta
    double speedAt(std::int64_t t) const {const auto ms=((t-start)/1'000'000)%2000;return ms>=1600&&ms<1900?speed*0.5:speed;}
    void run(eii::Machine& m,std::uint64_t n){
        while(n){
            deliver();maxBuffered=std::max(maxBuffered,in->buffered());
            if(!m.sampleInputReady(20000)){g_simNs+=1'000'000;continue;}
            const std::int64_t emu=std::int64_t(m.ticks-t0)*250;
            if(emu>g_simNs-start+20'000'000){g_simNs=start+emu-20'000'000;continue;}   // pacer sleeps
            const auto q=std::min<std::uint64_t>(20000,n);m.run(q);n-=q;++quanta;
            g_simNs+=std::int64_t(double(q)*250/speedAt(g_simNs));
            if(quanta%621==0)g_simNs+=45'000'000;
        }
    }
};
static std::uint64_t fnv(const std::vector<std::uint8_t>& d){std::uint64_t h=14695981039346656037ull;for(auto b:d){h^=b;h*=1099511628211ull;}return h;}
struct Result{std::uint64_t mem=0,known=0,audio=0;std::size_t changed=0;};
struct Options{double volts=1.0,seconds=2.0,gainSlider=-1,liveSpeed=0;unsigned note=60;bool native=false,arm=false;};
static Result runEngine(const std::filesystem::path& roms,const std::vector<std::uint8_t>& disk,const std::vector<float>& wav,double wavRate,
                        bool production,const std::string& prefix,const Options& o){
    const double volts=o.volts,seconds=o.seconds;const unsigned note=o.note;
    const char* name=production?"production":"reference";
    auto m=std::make_unique<eii::Machine>(0);
    m->load(roms/"main.bin",roms/"scanner.bin",roms/"ic135.bin",roms/"ic137.bin");
    m->initializeDramPowerOn(1);m->enableProductionController(production);m->initializeControllerPowerOn(0xe1122026u);
    m->enableNormalRuntime();m->floppy.attach(0,37,400000);m->floppy.drives[0]->insertSensorDisk(true);
    m->diskRead=std::make_unique<eii::DiskReadPath>(87,8);
    m->diskRead->mount(0,std::make_shared<const eii::EmuiiImage>(disk),eii::EmuiiGap::Hxc2026);
    std::vector<float> audio;bool capture=false;std::uint64_t ah=14695981039346656037ull;
    m->enableRuntimeAudio([&](auto fs){for(auto f:fs){std::uint32_t b;std::memcpy(&b,&f.left,4);ah=(ah^b)*1099511628211ull;if(capture)audio.push_back(f.left);}},
        o.native?eii::analog::RuntimeAudioMode::NonlinearChip:eii::analog::RuntimeAudioMode::BasicVoice);
    if(o.native){m->reconstruction(true);m->nativeAudio(true);}
    LiveSim live;bool liveOn=false;
    auto adv=[&](std::uint64_t t){if(liveOn)live.run(*m,t);else m->run(t);};
    auto lcd=[&](const char* tag){std::printf("[%s] %-12s t=%llu lcd=[%s|%s] reads=%llu conv=%llu waits=%llu unkReads=%llu\n",name,tag,(unsigned long long)m->ticks,
        m->lcd.line(0).c_str(),m->lcd.line(1).c_str(),(unsigned long long)m->sampleReads,(unsigned long long)m->sampleSar.conversions,(unsigned long long)m->sampleWaitClocks,(unsigned long long)m->sampleUnknownReads);};
    auto press=[&](B b,std::uint64_t after){m->scannerControls.setButton(b,true);adv(800000);m->scannerControls.setButton(b,false);adv(after);};
    adv(200000000);lcd("boot");
    press(B::Sample,30000000);lcd("sample");
    auto pgain=[&](const char* tag){const auto a=m->scannerPioBPin(7),b=m->scannerPioBPin(6);
        std::printf("[%s] %s: -PGAIN1=%s +PGAIN2=%s lcd=[%s|%s]\n",name,tag,a.known?(a.high?"1":"0"):"?",b.known?(b.high?"1":"0"):"?",m->lcd.line(0).c_str(),m->lcd.line(1).c_str());};
    if(o.gainSlider>=0){press(B::Digit3,8000000);pgain("GAIN SET");
        m->scannerControls.setVoltage(eii::ScannerAnalogInput::SliderA,o.gainSlider);adv(8000000);pgain("slider A");
        press(B::Digit3,8000000);pgain("GAIN SET again");}
    if(o.liveSpeed>0){
        if(wavRate!=48000)throw std::runtime_error("live mode needs a 48 kHz WAV");
        live.in=std::make_shared<eii::analog::LiveSampleInput>();live.in->clockNs=&simClock;live.in->voltsFullScale=float(volts);
        live.wav=&wav;live.speed=o.liveSpeed;g_simNs=1'000'000'000;live.start=g_simNs;live.t0=m->ticks;
        m->sampleInput=live.in;liveOn=true;
    } else {
        auto sig=std::make_shared<eii::analog::SampleInputSignal>();sig->samples=wav;sig->rate=wavRate;sig->voltsFullScale=volts;sig->startTick=m->ticks;
        m->sampleInput=sig;}
    const auto before=m->memory;const auto beforeKnown=m->dramKnown;
    capture=true;
    if(o.arm){
        // ARM: the OS3.1 overlay waits in its pre-trigger INI loop (6E1F/6E36)
        // until the scanner reports the threshold (PIO A FCh); the record loop
        // is 6E58..6EAA. Report when the main CPU first executes it.
        const auto armTick=m->ticks;std::uint64_t trig=0;
        m->scannerControls.setButton(B::Digit7,true);adv(800000);m->scannerControls.setButton(B::Digit7,false);
        while(m->ticks-armTick<std::uint64_t(seconds*4e6)){adv(20000);const auto pc=m->main.cpu.pc;if(!trig&&pc>=0x6e58&&pc<0x6eab)trig=m->ticks;}
        if(trig)std::printf("[%s] threshold trigger %.3f s after ARM (input tone onset in the burst WAV: 1.000 s)\n",name,double(trig-armTick)/4e6);
        else std::printf("[%s] no threshold trigger\n",name);
        lcd("arm+rec");}
    else {press(B::Digit9,std::uint64_t(seconds*4e6));lcd("force+rec");}
    {   // Stored-data rate: SAR reads (CPU INI into DRAM) over 1.000 s of the
        // ongoing recording. Must emerge from timer 360 / -IWAIT: 27777.8 Hz.
        const auto r0=m->sampleReads;adv(4000000);
        std::printf("[%s] stored-data rate %llu samples/s (10 MHz / 360 = 27777.8)\n",name,(unsigned long long)(m->sampleReads-r0));}
    capture=false;
    {   // Step 6: channel-0 monitor while sampling (IMODE=1), as wired on 7-44.
        std::vector<double> a(audio.begin(),audio.end());double peak=0;for(double v:a)peak=std::max(peak,std::fabs(v));
        std::printf("[%s] monitor during recording: %zu frames, peak %.4f, tone %.1f Hz\n",name,a.size(),peak,
            a.size()>8?toneHz(std::vector<double>(a.begin()+a.size()/4,a.end()),48000):0.0);
        if(a.size()>9600){const std::vector<double> mon(a.begin()+a.size()/4,a.end());printFit(name,"monitor",mon,48000);continuity(name,"monitor",mon,48000);}
        writeWav(prefix+"_"+name+"_monitor.wav",audio,48000);audio.clear();
    }
    press(B::Digit0,8000000);lcd("stop");
    if(liveOn){const auto& l=*live.in;
        std::printf("[%s] live timeline: sim wall %.3f s, emulated %.3f s, quanta %llu, pace holds %llu, prefills %llu, underruns %llu, overruns %llu, discarded %llu, max buffer %.0f ms\n",name,
            double(g_simNs-live.start)/1e9,double(m->ticks-live.t0)/4e6,(unsigned long long)live.quanta,(unsigned long long)l.paceHolds.load(),(unsigned long long)l.prefills.load(),
            (unsigned long long)l.underruns.load(),(unsigned long long)l.overruns.load(),(unsigned long long)l.discarded.load(),double(live.maxBuffered)/48);
        liveOn=false;}
    std::size_t changed=0,lo=SIZE_MAX,hi=0;
    for(std::size_t i=0;i<m->memory.size();++i)if(m->memory[i]!=before[i]||m->dramKnown[i]!=beforeKnown[i]){++changed;lo=std::min(lo,i);hi=i;}
    std::printf("[%s] DRAM bytes changed during sampling: %zu (range %05zx..%05zx)\n",name,changed,lo==SIZE_MAX?0:lo,hi);
    // Decode the longest run of changed bytes as DPCM through the settled 7-44
    // deglitch recursion (analysis only; the machine does not use this).
    std::size_t best=0,bestStart=0,run=0;
    for(std::size_t i=0;i<m->memory.size();++i){if(m->memory[i]!=before[i]){++run;if(run>best){best=run;bestStart=i+1-run;}}else run=0;}
    std::vector<double> y;double iint=0;
    for(std::size_t i=bestStart;i<bestStart+best;++i){const double idac=eii::analog::Am6072::eiiSteadyVolts({m->memory[i],255},{4.98046875,true}).value;iint-=(200*iint+10000*idac)/10200;y.push_back(iint);}
    std::printf("[%s] longest changed run %zu bytes at %05zx; DPCM-decoded tone %.1f Hz at 27777.8 Hz\n",name,best,bestStart,toneHz(y,1e7/360));
    if(lo!=SIZE_MAX){   // the longest written region (changed runs merged across gaps < 64 bytes), in address order
        std::size_t rs=lo,re=lo,cs=lo,last=lo;
        for(std::size_t i=lo;i<=hi;++i)if(m->memory[i]!=before[i]){if(i-last>=64)cs=i;last=i;if(last-cs>re-rs){rs=cs;re=last;}}
        std::printf("[%s] recorded region %05zx..%05zx (%zu bytes, %.3f s)\n",name,rs,re,re-rs+1,double(re-rs+1)*360/1e7);
        std::vector<double> all;double v=0;
        for(std::size_t i=rs;i<=re;++i){const double idac=eii::analog::Am6072::eiiSteadyVolts({m->memory[i],255},{4.98046875,true}).value;v-=(200*v+10000*idac)/10200;all.push_back(v);}
        const std::vector<double> body(all.begin()+all.size()/20,all.end()-all.size()/20);
        printFit(name,"DRAM DPCM-decoded",body,1e7/360);continuity(name,"DRAM DPCM-decoded",body,1e7/360);}
    // Playback through the real voice path.
    press(B::Sample,30000000);lcd("leave");
    capture=true;const std::array<std::uint8_t,3> on{0x90,std::uint8_t(note),100},off{0x80,std::uint8_t(note),0};
    m->receiveMidi(on);adv(8000000);m->receiveMidi(off);adv(2000000);capture=false;lcd("played");
    std::vector<double> a(audio.begin(),audio.end());double peak=0;for(double v:a)peak=std::max(peak,std::fabs(v));
    std::printf("[%s] playback note %u: %zu frames, peak %.4f, tone %.1f Hz\n",name,note,a.size(),peak,toneHz(std::vector<double>(a.begin()+a.size()/8,a.begin()+a.size()*3/4),48000));
    if(a.size()>9600){const std::vector<double> pb(a.begin()+a.size()/8,a.begin()+a.size()*3/4);printFit(name,"playback",pb,48000);continuity(name,"playback",pb,48000);}
    writeWav(prefix+"_"+name+"_play.wav",audio,48000);
    Result r{fnv(m->memory),fnv(m->dramKnown),ah,changed};
    std::printf("[%s] HASH mem=%llu known=%llu audio=%llu\n",name,(unsigned long long)r.mem,(unsigned long long)r.known,(unsigned long long)r.audio);
    return r;
}
int main(int argc,char** argv) try {
    if(argc<5){std::cerr<<"usage: sample_wav_probe ROMDIR OSDISK WAV OUTPREFIX [reference|production|both] [volts] [seconds] [note]\n";return 2;}
    auto disk=eii::readFile(argv[2]);
    if(disk.size()>=8&&std::equal(disk.begin(),disk.begin()+8,"HXCPICFE"))disk=eii::HfeImage(std::move(disk)).extractEmuii();
    double rate=48000;const auto wav=readWav(argv[3],rate);
    const std::string which=argc>5?argv[5]:"both";Options o;
    if(argc>6)o.volts=std::stod(argv[6]);
    if(argc>7)o.seconds=std::stod(argv[7]);
    if(argc>8)o.note=unsigned(std::stoul(argv[8]));
    if(argc>9)o.native=std::string(argv[9])=="native";
    if(argc>10)o.arm=std::string(argv[10])=="arm";
    if(argc>11)o.gainSlider=std::stod(argv[11]);
    if(argc>12&&!std::strncmp(argv[12],"live:",5))o.liveSpeed=std::stod(argv[12]+5);
    std::printf("input %zu samples at %.0f Hz, +/-1.0 -> +/-%.3f V at +INPUT.V; audio %s; %s; GAIN slider %.2f V\n",wav.size(),rate,o.volts,o.native?"native":"legacy",o.arm?"ARM":"FORCE",o.gainSlider);
    Result r,p;
    if(which!="production")r=runEngine(argv[1],disk,wav,rate,false,argv[4],o);
    if(which!="reference")p=runEngine(argv[1],disk,wav,rate,true,argv[4],o);
    if(which=="both"){
        const bool eq=r.mem==p.mem&&r.known==p.known&&r.audio==p.audio;
        std::printf("%s reference vs production DRAM/known/audio hashes\n",eq?"EQUAL":"DIFFERENT");
        if(!eq||r.changed==0)return 1;
    }
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
