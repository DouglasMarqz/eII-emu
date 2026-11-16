// Slider latency probe: real ROMs, read-only bank, production controller, no
// GUI/DSP. Logs ADC conversions per channel, scanner->main traffic proxies and
// LCD changes after a slider step, in emulated time.
// usage: slider_latency_probe ROMDIR BANK MODE [fast]
//   MODE tune   : TUNE menu (slider A = data entry), step A 0->5 V
//   MODE rate   : idle preset, ADC conversion rate per channel for 2 s
//   MODE vca|filter : module page 1, step each slider; checks A-D settle parity
//   MODE jitter-play|jitter-vca|jitter-filter : 24 external MIDI notes per
//        condition (idle, wheels, sliders A-D swept 2/8 Hz); checks that note-on
//        servicing under a sweep stays within idle +3 ms mean / +6 ms max
//        (env JITTER_HOLD=n holds n voices, JITTER_VERBOSE=1 per note)
//   MODE arp    : ARP_MENU=play|filter|vca, attack cadence with 3 held notes
//        (needs a bank whose preset runs the arpeggiator)
#include "floppy_hfe.h"
#include "machine.h"
#include "vst3/scanner_panel_response.h"
#include <cstdio>
#include <iostream>
#include <map>
#include <vector>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <cstdlib>

#ifdef WAIT_REASONS
namespace eii{extern std::uint64_t g_waitReason[8],g_waitHist[64];}using eii::g_waitReason;using eii::g_waitHist;
#endif
using eii::ScannerButton;using eii::ScannerAnalogInput;
int main(int argc,char** argv)try{
    if(argc<4){std::cerr<<"usage: slider_latency_probe ROMDIR BANK MODE [fast]\n";return 2;}
    const auto folder=std::filesystem::u8path(argv[1]);const std::string mode=argv[3];const bool fast=argc>4&&std::string(argv[4])=="fast";
    auto m=std::make_unique<eii::Machine>(0);
    m->channelTimers.enableControllerTiming();m->channelTimers.controllerTiming.enableRegisterWrites();
    m->load(folder/"main.bin",folder/"scanner.bin",folder/"ic135.bin",folder/"ic137.bin");
    if(fast)eii::vst::applyFastScannerPanel(m->scanner.rom,true);
    m->initializeDramPowerOn(1);m->enableProductionController();
    m->initializeControllerPowerOn(0xe1122026u);m->enableNormalRuntime();
    // PROBE_AUDIO=n attaches the runtime DSP like the app (7 = Ssm2Chip192), output discarded.
    if(const char* pa=getenv("PROBE_AUDIO"))m->enableRuntimeAudio([](auto){},eii::analog::RuntimeAudioMode(std::atoi(pa)));
    auto bytes=eii::readFile(std::filesystem::u8path(argv[2]));
    if(bytes.size()>=8&&std::equal(bytes.begin(),bytes.begin()+8,"HXCPICFE"))bytes=eii::HfeImage(std::move(bytes)).extractEmuii();
    m->floppy.attach(0,37,400000);m->floppy.drives[0]->insertSensorDisk(true);
    m->diskRead=std::make_unique<eii::DiskReadPath>(87,8);
    m->diskRead->mount(0,std::make_shared<const eii::EmuiiImage>(std::move(bytes)),eii::EmuiiGap::Hxc2026);
    constexpr std::uint64_t ms=4000;std::uint64_t t=0;int failures=0; // regression checks: exit code 3
    auto run=[&](std::uint64_t ticks){m->run(ticks);t+=ticks;};
    for(unsigned i=0;i<250&&!m->lcd.line(0).starts_with("P");++i)run(1000*ms);
    run(1000*ms);
    std::printf("BOOT t=%.3f lcd='%s'\n",double(t)/4e6,m->lcd.line(0).c_str());
    auto& adc=m->scannerControls.adc;
    // Per-channel conversion log, sampled every 64 CPU ticks (one conversion = 512 ticks).
    std::array<std::uint64_t,8> conv{};std::uint64_t lastCompleted=adc.completed;
    static const std::uint64_t chunk=getenv("PROBE_CHUNK")?std::strtoull(getenv("PROBE_CHUNK"),nullptr,10):64;static std::uint64_t owed=0;auto step=[&](std::uint64_t ticks){owed+=ticks;for(;owed>=chunk;owed-=chunk){run(chunk);if(adc.completed!=lastCompleted){conv[adc.channel.value&7]+=adc.completed-lastCompleted;lastCompleted=adc.completed;}}};
    auto press=[&](ScannerButton b){m->scannerControls.setButton(b,true);step(80*ms);m->scannerControls.setButton(b,false);step(200*ms);};
    if(mode=="rate"||mode=="tune"){
        if(mode=="tune")press(ScannerButton::Tune);
        conv.fill(0);step(2000*ms);
        std::printf("ADC conversions in 2 s (%s): ",mode.c_str());for(unsigned c=0;c<8;++c)std::printf("IN%u=%llu ",c,(unsigned long long)conv[c]);std::printf("\n");
    }
    if(mode=="cpu"){
        auto snap=[&]{return std::array<std::uint64_t,3>{m->dramWaitClocks,m->dramReadCycles,m->dramWriteCycles};};
        for(int notes:{0,8}){
            for(int k=0;k<notes;++k){const std::array<std::uint8_t,3> on{0x90,std::uint8_t(48+k*3),100};m->receiveMidi(on);}
            step(300*ms);const auto a=snap();const auto t0=t;step(2000*ms);const auto b=snap();
            std::printf("MAINCPU notes=%d wait=%.1f%% dramReads/s=%.0f dramWrites/s=%.0f\n",notes,100.0*double(b[0]-a[0])/double(t-t0),double(b[1]-a[1])/2,double(b[2]-a[2])/2);
        }
    }
    if(mode=="sweep"){ // Tune menu, slider A triangle 0..5 V, then hold; LCD settle time and scanner TX backlog
        press(ScannerButton::Tune);
        auto backlog=[&]{const unsigned w=m->scannerRam[0x26e&2047]|m->scannerRam[0x26f&2047]<<8,r=m->scannerRam[0x270&2047]|m->scannerRam[0x271&2047]<<8;return int((w-r)&0xff);};
        int maxBacklog=0;
        for(unsigned k=0;k<2000;++k){const double ph=std::fmod(k/200.0,1.0);m->scannerControls.setVoltage(ScannerAnalogInput::SliderA,5.0*(ph<.5?2*ph:2-2*ph));step(ms);maxBacklog=std::max(maxBacklog,backlog());}
        m->scannerControls.setVoltage(ScannerAnalogInput::SliderA,2.5);std::string last=m->lcd.line(0);unsigned settled=0;
        for(unsigned k=1;k<=2000;++k){step(ms);const auto l=m->lcd.line(0);maxBacklog=std::max(maxBacklog,backlog());if(l!=last){last=l;settled=k;}}
        std::printf("SWEEP done: LCD last change %u ms after hold, final '%s', max scanner TX backlog %d bytes\n",settled,last.c_str(),maxBacklog);
    }
    if(mode=="rt"){ // play mode, one held note: which CV follows each slider, first/last change
        const std::array<std::uint8_t,3> on{0x90,60,100};m->receiveMidi(on);step(1500*ms);
        const ScannerAnalogInput in[4]={ScannerAnalogInput::SliderA,ScannerAnalogInput::SliderB,ScannerAnalogInput::SliderC,ScannerAnalogInput::SliderD};
        for(unsigned i=0;i<4;++i)for(double v:{5.0,0.0}){
            auto snapshot=[&]{std::array<std::uint8_t,24> c{};for(unsigned b=0;b<3;++b)for(unsigned ch=0;ch<8;++ch)c[b*8+ch]=m->controlVoltage.held[b][ch].code;return c;};
            const auto before=snapshot();auto prev=before;m->scannerControls.setVoltage(in[i],v);
            int first=-1,last=-1;unsigned changes=0;std::array<unsigned,24> hit{};
            for(unsigned k=1;k<=1500;++k){step(ms);const auto c=snapshot();for(unsigned j=0;j<24;++j)if(c[j]!=prev[j]){++hit[j];if(first<0)first=int(k);last=int(k);++changes;}prev=c;}
            std::string which;for(unsigned j=0;j<24;++j)if(hit[j])which+=std::string(" ")+"AFQ"[j/8]+std::to_string(j%8)+"x"+std::to_string(hit[j]);
            std::printf("RT slider %c -> %.0f V: first CV change %d ms, last %d ms, %u updates:%s\n",char('A'+i),v,first,last,changes,which.c_str());
        }
    }
    if(mode=="filter"||mode=="vca"){ // module menu with a current voice: step each slider, LCD field timeline
        press(mode=="filter"?ScannerButton::Filter:ScannerButton::VcaLfo);step(2500*ms);
        const std::array<std::uint8_t,3> on{0x90,60,100},off{0x80,60,0};m->receiveMidi(on);step(300*ms);m->receiveMidi(off);step(1500*ms);
        std::printf("MENU '%s|%s'\n",m->lcd.line(0).c_str(),m->lcd.line(1).c_str());
        press(ScannerButton::Enter);step(500*ms);
        std::printf("MENU '%s|%s'\n",m->lcd.line(0).c_str(),m->lcd.line(1).c_str());
        press(ScannerButton::Digit7);step(300*ms); // "Addl Voices? Y/N" -> NO
        std::printf("MENU '%s|%s'\n",m->lcd.line(0).c_str(),m->lcd.line(1).c_str());
        press(ScannerButton::Digit1);step(500*ms); // page 1
        std::printf("MENU '%s|%s'\n",m->lcd.line(0).c_str(),m->lcd.line(1).c_str());
        const ScannerAnalogInput in[4]={ScannerAnalogInput::SliderA,ScannerAnalogInput::SliderB,ScannerAnalogInput::SliderC,ScannerAnalogInput::SliderD};
        std::array<std::array<int,2>,4> settledMs{};
        for(unsigned i=0;i<4;++i)for(double v:{3.0,0.0}){
            std::string last=m->lcd.line(0)+"|"+m->lcd.line(1);m->scannerControls.setVoltage(in[i],v);
            int first=-1,lastT=-1;unsigned n=0;
            // Scanner "last reported" value of this ADC channel (RAM 8258h+ch, ROM 0C08h/0CA5h):
            // it changes exactly when the scanner queues the slider message for the main CPU.
            const unsigned slot=0x258+4+i;std::uint8_t rep=m->scannerRam[slot];std::vector<double> reports,lcds;
            for(unsigned k=1;k<=1200*4;++k){step(ms/4);const auto l=m->lcd.line(0)+"|"+m->lcd.line(1);
                if(m->scannerRam[slot]!=rep){rep=m->scannerRam[slot];reports.push_back(k/4.0);}
                if(l!=last){if(first<0)first=int(k/4);lastT=int(k/4);++n;last=l;lcds.push_back(k/4.0);}}
            double lag=0;unsigned pairs=0;for(double r:reports)for(double c:lcds)if(c>=r){lag+=c-r;++pairs;break;}
            std::printf("MENU slider %c -> %.0f V: first LCD %d ms, settled %d ms, %u updates, scanner reports %zu (first %.2f ms, last %.2f ms), report->LCD mean %.2f ms, '%s'\n",
                char('A'+i),v,first,lastT,n,reports.size(),reports.empty()?-1:reports.front(),reports.empty()?-1:reports.back(),pairs?lag/pairs:-1.0,last.c_str());
            settledMs[i][v>0?0:1]=lastT;
            if(first<0||first>60){std::printf("CHECK FAIL slider %c first LCD %d ms (limit 60)\n",char('A'+i),first);++failures;}
        }
        // A-D parity: the four sliders share one firmware path (ROM 0C20h-0C7Eh) and one schedule slot each.
        for(unsigned d=0;d<2;++d){int lo=1<<30,hi=0;for(unsigned i=0;i<4;++i){lo=std::min(lo,settledMs[i][d]);hi=std::max(hi,settledMs[i][d]);}
            const bool ok=lo>0&&hi<=lo*5/4;std::printf("CHECK %s A-D settle parity %s: %d..%d ms\n",ok?"PASS":"FAIL",d?"down":"up",lo,hi);failures+=!ok;}
    }
    if(mode=="replay"){ // Filter page, current voice: owner gesture = fast A flicks, then release; does the firmware keep replaying?
        press(ScannerButton::Filter);step(2500*ms);
        const std::array<std::uint8_t,3> on{0x90,60,100},off{0x80,60,0};m->receiveMidi(on);step(300*ms);m->receiveMidi(off);step(1500*ms);
        if(getenv("REPLAY_DIRECT")){press(ScannerButton::Digit1);step(500*ms);}else if(getenv("REPLAY_YES")){press(ScannerButton::Enter);step(500*ms);press(ScannerButton::Digit9);step(300*ms);std::printf("AFTER YES1 '%s|%s'\n",m->lcd.line(0).c_str(),m->lcd.line(1).c_str());press(ScannerButton::Digit9);step(300*ms);std::printf("AFTER YES2 '%s|%s'\n",m->lcd.line(0).c_str(),m->lcd.line(1).c_str());press(ScannerButton::Digit1);step(500*ms);}else{press(ScannerButton::Enter);step(500*ms);press(ScannerButton::Digit7);step(300*ms);press(ScannerButton::Digit1);step(500*ms);}
        std::printf("MENU '%s|%s'\n",m->lcd.line(0).c_str(),m->lcd.line(1).c_str());
        const double hz=argc>5?std::atof(argv[5]):4.0;const unsigned sweepMs=3000;
        auto bl=[&]{const unsigned w=m->scannerRam[0x26e&2047]|m->scannerRam[0x26f&2047]<<8,r=m->scannerRam[0x270&2047]|m->scannerRam[0x271&2047]<<8;return int((w-r)&0xff);};int maxBl=0;unsigned blZeroAt=0;static std::array<std::uint64_t,65536> pf0{};pf0=m->main.fetched;const auto pt0=t;const auto pw0=m->dramWaitClocks;std::string last=m->lcd.line(1);unsigned changesDuring=0;
        for(unsigned k=0;k<sweepMs;++k){const double ph=std::fmod(k*hz/1000.0,1.0);{const char* sh=getenv("REPLAY_SHAPE");const std::string s=sh?sh:"tri";double v=5.0*(ph<.5?2*ph:2-2*ph);if(s=="square")v=ph<.5?5.0:0.0;else if(s=="random"&&k%8==0)v=5.0*double((k*2654435761u>>7)%1000)/999.0;else if(s=="random")v=-1;if(v>=0)m->scannerControls.setVoltage(ScannerAnalogInput::SliderA,v);}step(ms);maxBl=std::max(maxBl,bl());const auto l=m->lcd.line(1);if(l!=last){last=l;++changesDuring;}}
        m->scannerControls.setVoltage(ScannerAnalogInput::SliderA,0.0);
        unsigned changesAfter=0,lastAt=0;
        {std::uint64_t tot=0;std::vector<std::pair<std::uint64_t,unsigned>> v;for(unsigned p=0;p<65536;++p){const auto d=m->main.fetched[p]-pf0[p];tot+=d;if(d)v.push_back({d,p});}std::sort(v.rbegin(),v.rend());const double msE=double(t-pt0)/4000.0;std::printf("PROF sweep %.0f ms: main fetches %llu (%.0f/ms) dramWait %llu clk (%.1f%%)\nPCS:",msE,(unsigned long long)tot,tot/msE,(unsigned long long)(m->dramWaitClocks-pw0),100.0*double(m->dramWaitClocks-pw0)/double(t-pt0));for(unsigned k=0;k<32&&k<v.size();++k)std::printf(" %04X:%.1f%%",v[k].second,100.0*v[k].first/tot);std::printf("\n");}{const char* rs=getenv("DUMP_RANGES");if(rs){std::string s=rs;size_t q=0;while(q<s.size()){auto c=s.find(',',q);if(c==std::string::npos)c=s.size();auto part=s.substr(q,c-q);auto dash=part.find('-');unsigned a0=std::stoul(part.substr(0,dash),nullptr,16),a1=std::stoul(part.substr(dash+1),nullptr,16);std::printf("DUMP %04X:",a0);for(unsigned a=a0;a<=a1;++a)std::printf(" %02X",m->readMemory(false,std::uint16_t(a)));std::printf("\n");q=c+1;}}}const int blRel=bl();for(unsigned k=1;k<=6000;++k){step(ms);if(!blZeroAt&&bl()==0)blZeroAt=k;const auto l=m->lcd.line(1);if(l!=last){last=l;++changesAfter;lastAt=k;if(changesAfter<=40)std::printf("  +%5u ms '%s'\n",k,l.c_str());}}
        
#ifdef WAIT_REASONS
        std::printf("WAITR total %llu hwUnknown %llu hwLow %llu notReady %llu notServiced %llu hwLowOnly %llu adapterOnly %llu\n",(unsigned long long)g_waitReason[0],(unsigned long long)g_waitReason[1],(unsigned long long)g_waitReason[2],(unsigned long long)g_waitReason[3],(unsigned long long)g_waitReason[4],(unsigned long long)g_waitReason[5],(unsigned long long)g_waitReason[6]);
        std::printf("WAITHIST");for(unsigned k=0;k<64;++k)if(g_waitHist[k])std::printf(" %u:%llu",k,(unsigned long long)g_waitHist[k]);std::printf("\n");
#endif
        std::printf("BACKLOG max during %d, at release %d, empty %u ms after release\n",maxBl,blRel,blZeroAt);std::printf("REPLAY %.1f Hz flicks %u ms: LCD changes during=%u after-release=%u, last change %u ms after release, final '%s'\n",hz,sweepMs,changesDuring,changesAfter,lastAt,last.c_str());
    }
    if(mode=="notes"){ // MIDI note-on -> amplitude-CV attack latency, sliders static vs. swept (all four, 5 Hz triangle)
        const ScannerAnalogInput in[4]={ScannerAnalogInput::SliderA,ScannerAnalogInput::SliderB,ScannerAnalogInput::SliderC,ScannerAnalogInput::SliderD};
        for(int sweep:{0,1,0}){
            std::vector<double> lat;
            for(unsigned n=0;n<24;++n){
                std::array<std::uint8_t,24> prev{};for(unsigned b=0;b<1;++b)for(unsigned ch=0;ch<8;++ch)prev[ch]=m->controlVoltage.held[0][ch].code;
                const std::array<std::uint8_t,3> on{0x90,std::uint8_t(48+(n*5)%24),110},off{0x80,std::uint8_t(48+(n*5)%24),0};
                m->receiveMidi(on);double hit=-1;
                for(unsigned k=1;k<=125*4;++k){
                    if(sweep){const double ph=std::fmod((double(t)/4e6)*5.0,1.0);for(unsigned i=0;i<4;++i)m->scannerControls.setVoltage(in[i],5.0*(ph<.5?2*ph:2-2*ph));}
                    step(ms/4);
                    if(hit<0)for(unsigned ch=0;ch<8;++ch){const int c=m->controlVoltage.held[0][ch].code;if(c-int(prev[ch])>40){hit=k/4.0;break;}prev[ch]=std::uint8_t(c);}
                    if(k==60*4)m->receiveMidi(off);
                }
                if(hit>=0)lat.push_back(hit);
            }
            double mean=0,mn=1e9,mx=0;for(double x:lat){mean+=x;mn=std::min(mn,x);mx=std::max(mx,x);}mean/=std::max<size_t>(1,lat.size());
            double var=0;for(double x:lat)var+=(x-mean)*(x-mean);var/=std::max<size_t>(1,lat.size());
            std::printf("NOTES sweep=%d: %zu/24 attacks, MIDI->attack mean %.2f ms, sd %.2f ms, min %.2f, max %.2f\n",sweep,lat.size(),mean,std::sqrt(var),mn,mx);
        }
    }
    if(mode.starts_with("jitter-")){ // external MIDI notes while one control is swept: idle / wheels / sliders A-D
        const std::string menu=mode.substr(7);
        if(menu!="play"){
            press(menu=="filter"?ScannerButton::Filter:ScannerButton::VcaLfo);step(2500*ms);
            const std::array<std::uint8_t,3> on{0x90,60,100},off{0x80,60,0};m->receiveMidi(on);step(300*ms);m->receiveMidi(off);step(1500*ms);
            press(ScannerButton::Enter);step(500*ms);press(ScannerButton::Digit7);step(300*ms);press(ScannerButton::Digit1);step(500*ms);
        }
        const unsigned hold=getenv("JITTER_HOLD")?unsigned(std::atoi(getenv("JITTER_HOLD"))):0u;
        for(unsigned h=0;h<hold&&h<7;++h){const std::array<std::uint8_t,3> on{0x90,std::uint8_t(84+h*2),100};m->receiveMidi(on);step(20*ms);}
        if(hold)step(500*ms);
        std::printf("JITTER held voices=%u\n",hold);
        std::printf("JITTER menu=%s '%s|%s'\n",menu.c_str(),m->lcd.line(0).c_str(),m->lcd.line(1).c_str());
        struct Cond{const char* name;int input;double hz;};
        const Cond conds[]={{"idle",-1,0},{"LWheel",2,2},{"RWheel",3,2},{"A",4,2},{"B",5,2},{"C",6,2},{"D",7,2},
                            {"idle",-1,0},{"RWheel",3,8},{"A",4,8},{"B",5,8},{"C",6,8},{"D",7,8},{"idle",-1,0}};
        static std::array<std::uint64_t,65536> idleFetch{},condFetch{},f0{};bool haveIdle=false;double idleMean=-1,idleMax=0;
        for(const auto& c:conds){
            std::vector<double> latA,latT;std::uint64_t ovr0=m->midiSerial.overruns;unsigned lcdChanges=0;std::string lastLcd=m->lcd.line(0)+m->lcd.line(1);
            double maxReady=0;std::uint64_t readyStart=0;bool wasReady=false;
            f0=m->main.fetched;const auto t0=t;
            auto writes=[&]{std::uint64_t w=0;for(unsigned i=0;i<8;++i)w+=m->channelTimers.counters[i].completeWrites;return w;};
            for(unsigned n=0;n<24;++n){
                std::array<int,8> prevA{};for(unsigned ch=0;ch<8;++ch)prevA[ch]=m->controlVoltage.held[0][ch].code;
                const auto w0=writes();
                const std::uint8_t key=std::uint8_t(48+(n*5)%24);const std::array<std::uint8_t,3> on{0x90,key,110},off{0x80,key,0};
                m->receiveMidi(on);double hitA=-1,hitT=-1;const auto tOn=t;
                for(unsigned k=1;k<=125*4;++k){
                    if(c.input>=0){const double ph=std::fmod((double(t)/4e6)*c.hz,1.0);m->scannerControls.setVoltage(ScannerAnalogInput(c.input),0.5+4.0*(ph<.5?2*ph:2-2*ph));}
                    for(unsigned q=0;q<16;++q){step(64);const bool r=m->midiSerial.ready;if(r&&!wasReady)readyStart=t;if(!r&&wasReady)maxReady=std::max(maxReady,double(t-readyStart)/4.0);wasReady=r;}
                    if(hitA<0)for(unsigned ch=0;ch<8;++ch){const int v=m->controlVoltage.held[0][ch].code;if(v-prevA[ch]>=2){hitA=double(t-tOn)/4000.0;break;}prevA[ch]=std::min(prevA[ch],v);}
                    if(hitT<0&&writes()!=w0)hitT=double(t-tOn)/4000.0;
                    const auto l=m->lcd.line(0)+m->lcd.line(1);if(l!=lastLcd){++lcdChanges;lastLcd=l;}
                    if(k==60*4)m->receiveMidi(off);
                }
                if(getenv("JITTER_VERBOSE"))std::printf("  note %u key %u ampRise %.2f timerWrite %.2f\n",n,key,hitA,hitT);
                if(hitA>=0)latA.push_back(hitA);
                if(hitT>=0)latT.push_back(hitT);
            }
            if(c.input>=0)m->scannerControls.setVoltage(ScannerAnalogInput(c.input),c.input>=4?0.0:2.5);
            auto stats=[](const std::vector<double>& v){double mean=0,mx=0,var=0;for(double x:v){mean+=x;mx=std::max(mx,x);}mean/=std::max<size_t>(1,v.size());for(double x:v)var+=(x-mean)*(x-mean);
                char b[96];std::snprintf(b,sizeof b,"%zu/24 mean %.2f sd %.2f max %.2f",v.size(),mean,std::sqrt(var/std::max<size_t>(1,v.size())),mx);return std::string(b);};
            std::uint64_t instr=0;for(unsigned a=0;a<65536;++a)instr+=m->main.fetched[a]-f0[a];
            std::printf("JITTER %-6s %.0fHz: ampRise[%s] timerWrite[%s] midiOverruns %llu maxRxWait %.0f us lcdChanges %u mainFetch/ms %.0f\n",c.name,c.hz,stats(latA).c_str(),stats(latT).c_str(),
                (unsigned long long)(m->midiSerial.overruns-ovr0),maxReady,lcdChanges,double(instr)/(double(t-t0)/4000.0));
            {double mean=0,mx=0;for(double x:latT){mean+=x;mx=std::max(mx,x);}mean/=std::max<size_t>(1,latT.size());
             if(c.input<0&&idleMean<0){idleMean=mean;idleMax=mx;}
             else if(c.input>=0){const bool ok=latT.size()==24&&mean<=idleMean+3&&mx<=idleMax+6&&m->midiSerial.overruns==ovr0;
                std::printf("CHECK %s %s %.0fHz MIDI note-on servicing vs idle: mean %.2f/%.2f ms max %.2f/%.2f ms\n",ok?"PASS":"FAIL",c.name,c.hz,mean,idleMean,mx,idleMax);failures+=!ok;}}
            std::fflush(stdout);
            if(c.input<0&&!haveIdle){haveIdle=true;for(unsigned a=0;a<65536;++a)idleFetch[a]=m->main.fetched[a]-f0[a];}
            if(c.input==4&&c.hz==8)for(unsigned a=0;a<65536;++a)condFetch[a]=m->main.fetched[a]-f0[a];
        }
        // Main-CPU code pages (256 B) that gain the most fetches under an 8 Hz slider-A sweep versus idle.
        std::vector<std::pair<long long,unsigned>> pages;for(unsigned p=0;p<256;++p){long long d=0;for(unsigned a=p*256;a<p*256+256;++a)d+=(long long)condFetch[a]-(long long)idleFetch[a];pages.push_back({d,p});}
        std::sort(pages.rbegin(),pages.rend());
        std::printf("PAGES (A 8 Hz minus idle, fetches/3 s):");for(unsigned i=0;i<12;++i)std::printf(" %02X00h:%+lld",pages[i].second,pages[i].first);std::printf("\n");
        std::vector<std::pair<long long,unsigned>> pcs;for(unsigned a=0;a<65536;++a){long long d=(long long)condFetch[a]-(long long)idleFetch[a];if(d>0)pcs.push_back({d,a});}
        std::sort(pcs.rbegin(),pcs.rend());
        std::printf("PCS:");for(unsigned i=0;i<24&&i<pcs.size();++i)std::printf(" %04X:%+lld",pcs[i].second,pcs[i].first);std::printf("\n");
    }
    if(mode.starts_with("arp")){ // firmware arpeggiator (Marcato bank P01): note-on cadence while a control is swept
        const char* menuEnv=getenv("ARP_MENU");const std::string menu=menuEnv?menuEnv:"play";
        const std::array<std::uint8_t,3> n1{0x90,60,100},n2{0x90,64,100},n3{0x90,67,100};
        if(menu!="play"){
            press(menu=="filter"?ScannerButton::Filter:ScannerButton::VcaLfo);step(2500*ms);
            m->receiveMidi(n1);step(300*ms);press(ScannerButton::Digit1);step(500*ms);
        }else{m->receiveMidi(n1);step(50*ms);}
        m->receiveMidi(n2);step(30*ms);m->receiveMidi(n3);step(2000*ms);
        std::printf("ARP menu=%s '%s|%s'\n",menu.c_str(),m->lcd.line(0).c_str(),m->lcd.line(1).c_str());
        if(getenv("ARP_DUMP"))for(unsigned k=0;k<150;++k){step(4*ms);std::printf("DUMP %4u ms A:",k*4);for(unsigned i=0;i<8;++i)std::printf(" %3d",m->controlVoltage.held[0][i].code);
            std::printf(" W:");for(unsigned i=0;i<8;++i)std::printf(" %llu",(unsigned long long)m->channelTimers.counters[i].completeWrites);std::printf("\n");}
        struct Cond{const char* name;int input;double hz;};
        const Cond conds[]={{"idle",-1,0},{"A",4,2},{"idle",-1,0},{"A",4,8},{"B",5,8},{"C",6,8},{"D",7,8},{"RWheel",3,8},{"LWheel",2,8},{"idle",-1,0}};
        std::array<bool,8> armed{};for(unsigned i=0;i<8;++i)armed[i]=m->controlVoltage.held[0][i].code<20;
        for(const auto& c:conds){
            const auto w0=std::chrono::steady_clock::now();std::vector<double> ons;unsigned lcdChanges=0;std::string lastLcd=m->lcd.line(0)+m->lcd.line(1);
            for(unsigned k=0;k<4000;++k){
                if(c.input>=0){const double ph=std::fmod((double(t)/4e6)*c.hz,1.0);m->scannerControls.setVoltage(ScannerAnalogInput(c.input),0.5+4.0*(ph<.5?2*ph:2-2*ph));}
                for(unsigned q=0;q<4;++q){step(250);
                    // Attack edge: amplitude CV of a channel crosses 40 upward; re-armed below 20.
                    for(unsigned i=0;i<8;++i){const int v=m->controlVoltage.held[0][i].code;if(armed[i]&&v>=40){armed[i]=false;ons.push_back(double(t)/4000.0);}else if(v<20)armed[i]=true;}}
                const auto l=m->lcd.line(0)+m->lcd.line(1);if(l!=lastLcd){++lcdChanges;lastLcd=l;}
            }
            if(c.input>=0)m->scannerControls.setVoltage(ScannerAnalogInput(c.input),c.input>=4?0.0:2.5);
            // Merge edges closer than 3 ms (a chord or a doubled voice).
            std::vector<double> notes;for(double x:ons)if(notes.empty()||x-notes.back()>3)notes.push_back(x);
            std::vector<double> gaps;for(size_t i=1;i<notes.size();++i)gaps.push_back(notes[i]-notes[i-1]);
            double mean=0,mn=1e9,mx=0,var=0;for(double g:gaps){mean+=g;mn=std::min(mn,g);mx=std::max(mx,g);}mean/=std::max<size_t>(1,gaps.size());for(double g:gaps)var+=(g-mean)*(g-mean);
            std::printf("WALL %.0f ms for 4000 ms emulated  ",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-w0).count());
            std::printf("ARP %-6s %.0fHz: %zu note-ons in 4 s, gap mean %.2f ms sd %.2f min %.2f max %.2f, lcdChanges %u\n",c.name,c.hz,notes.size(),mean,std::sqrt(var/std::max<size_t>(1,gaps.size())),gaps.empty()?0:mn,mx,lcdChanges);
            std::fflush(stdout);
        }
    }
    if(mode=="tune"){
        for(int dir:{1,0,1}){
            const double v=dir?5.0:0.0;std::string last=m->lcd.line(0);
            m->scannerControls.setVoltage(ScannerAnalogInput::SliderA,v);conv.fill(0);
            std::printf("STEP A -> %.1f V\n",v);
            for(unsigned k=1;k<=1500;++k){step(ms);const auto l=m->lcd.line(0);if(l!=last){std::printf("  t=%4u ms lcd='%s' conv4=%llu\n",k,l.c_str(),(unsigned long long)conv[4]);last=l;}}
        }
    }
    if(failures)std::printf("CHECK %d failure(s)\n",failures);
    return failures?3:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
