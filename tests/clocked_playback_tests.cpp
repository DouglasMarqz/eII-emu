#include "machine.h"
#include "transport_fixture.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static bool nearly(double a,double b,double eps=1e-11){return std::abs(a-b)<eps;}
static void fixture(eii::Machine& m) {
    m.channelTimers.controllerTiming.loadProms(transportProm());m.enableCpuDram();
    std::vector<std::uint8_t> code;
    auto out=[&](unsigned p,unsigned d){code.insert(code.end(),{0x3e,std::uint8_t(d),0xd3,std::uint8_t(p)});};
    out(0x48,63);out(0x60,0);code.insert(code.end(),64,0);
    for(unsigned ch=0;ch<8;++ch){unsigned address=(ch<<16)|0x5234;m.memory[address]=std::uint8_t(0x91+11*ch);m.dramKnown[address]=255;
        out(0x48,ch*4+1);out(0x50,address&255);out(0x58,(address>>8)&255);out(0x60,address>>16);code.insert(code.end(),32,0);
        out(0x40+ch,0x3b);out(0x70,0x30|ch);out(0x78,1);
        const unsigned base=ch<3?0x18:(ch<6?0x10:8),reg=ch%3;
        out(base+3,(reg<<6)|0x34);out(base+reg,104);out(base+reg,1);
    }
    code.push_back(0x76);std::copy(code.begin(),code.end(),m.main.rom.begin());m.run(20000);
    for(unsigned ch=0;ch<8;++ch){require(m.channelTimers.channels[ch].dividerKnown(),"real timer programming converges divider phase");require(m.channelTimers.controllerTiming.channelDacCode(ch).known==255,"real transport fixture initializes DAC");}
}
int main(){try{
    using namespace eii;using namespace eii::analog;
    // Independent overlap integral at exact rational frame boundaries, with
    // a single unknown source interval. No endpoint-picking/downsample shortcut.
    for(unsigned rate:{44100u,48000u,96000u}){
        PlaybackFrameIntegrator frames(rate);unsigned count=0;
        for(unsigned edge=0;edge<100000;++edge){std::array<KnownSignal,8> input{};
            input[0]={double(edge)*1e-5,true};input[1]={2,true};input[2]={3,edge!=421};
            frames.interval(input,[&](const PlaybackFrame& f){++count;
                const double a=double(count-1)*20000000/rate,b=double(count)*20000000/rate;
                double integral=0;bool known=true;
                for(unsigned n=unsigned(std::floor(a));double(n)<b;++n){const double width=std::max(0.,std::min(b,double(n+1))-std::max(a,double(n)));integral+=width*n*1e-5;if(width>0&&n==421)known=false;}
                require(f.index==count&&nearly(f.meanVolts[0].value,integral/(b-a))&&f.meanVolts[0].known,"frame mean matches independent overlap integral");
                require(f.meanVolts[1].known&&nearly(f.meanVolts[1].value,2),"constant amplitude preserved");
                require(f.meanVolts[2].known==known&&!f.meanVolts[3].known,"unknown interval contaminates only overlapping frame; unused channel unknown");
            });
        }
        require(count==std::uint64_t(100000)*rate/20000000&&frames.frames()==count,"exact rational frame count");
    }
    for(unsigned rate:{0u,20000001u}){bool threw=false;try{PlaybackFrameIntegrator invalid(rate);}catch(const std::invalid_argument&){threw=true;}require(threw,"invalid frame rate rejected");}
    ChannelTimers plain,observed;plain.write(0x1b,0x34);observed.write(0x1b,0x34);plain.write(0x18,104);observed.write(0x18,104);plain.write(0x18,1);observed.write(0x18,1);
    std::uint64_t calls=0;
    for(unsigned n=0;n<1000;++n){plain.advanceCpuTick();observed.advanceCpuTickObserved([&](std::uint64_t edge){require(edge==++calls&&observed.halfClocks+1==edge,"observer runs before each edge exactly once");});}
    require(calls==5000&&plain.channels[0].requestClocks==observed.channels[0].requestClocks&&plain.channels[0].secondLowBits()==observed.channels[0].secondLowBits(),"observed timer execution preserves digital results");
    auto bare=std::make_unique<Machine>();auto whole=std::make_unique<Machine>();auto chunks=std::make_unique<Machine>();
    fixture(*bare);fixture(*whole);fixture(*chunks);
    ClockedPlaybackOptions options;options.zeroCapacitorsForExperiment=true;options.alignDividersForExperiment=true;options.channel0PlaybackConfirmed=true;
    for(auto& x:options.filterGm)x={100e-6,true};
    for(auto& x:options.vcaBranchAmps)x={10e-6,true};
    std::vector<PlaybackFrame> a,b;
    whole->enableClockedPlayback(options,[&](const PlaybackFrame& f){a.push_back(f);});chunks->enableClockedPlayback(options,[&](const PlaybackFrame& f){b.push_back(f);});
    bare->run(40000);whole->run(40000);for(unsigned n=0;n<400;++n)chunks->run(100);
    require(bare->report()==whole->report()&&bare->report()==chunks->report(),"opt-in analog observer cannot change real CPU/PROM/DRAM/timer reports");
    require(a.size()==480&&a.size()==b.size(),"10ms capture produces480 frames");double peak=0;
    for(unsigned n=0;n<a.size();++n)for(unsigned ch=0;ch<8;++ch){const auto x=a[n].meanVolts[ch],y=b[n].meanVolts[ch];
        require(x.known&&y.known&&x.value==y.value&&std::isfinite(x.value),"chunking preserves all8 finite known output streams bit for bit");peak=std::max(peak,std::abs(x.value));}
    require(peak>1e-7&&peak<1,"real Z80 sample/CV/timer/filter writes drive nonzero bounded continuous output");
    const auto& engine=*whole->clockedPlayback();require(engine.intervals()==200000&&engine.originHalfEdge()==100000&&engine.rangeExceededMask()==0,"engine follows exact Machine half-edge count");
    for(unsigned ch=0;ch<8;++ch){require(engine.reconstructionSamples(ch)==10000,"3B/N10 actual reconstruction cadence");require(!engine.channel(ch).analog.diagnosticDirectDeglitch&&!engine.channel(ch).analog.diagnosticDirectReconstruction,"no direct bypass used");}
    // Changes must invalidate phase before the next master edge, including an odd edge.
    whole->output(whole->main,0x41,0x3a);whole->run(1);
    require(!whole->clockedPlayback()->channel(1).reconstructionPhaseKnown&&!whole->clockedPlayback()->channel(1).analog.feedbackVolts.known,"reprogramming phase unknown cannot leak known host output");
    require(whole->clockedPlayback()->channel(2).analog.feedbackVolts.known,"reprogramming isolated to selected channel");
    whole->disableClockedPlayback();require(!whole->clockedPlayback(),"engine detachable");
    // Explicit experiment range guard rejects an impossible linear op-amp
    // output instead of synthesizing a rail-clipping characteristic.
    ClockedPlaybackOptions hotOptions=options;hotOptions.channels=2;
    ClockedPlayback hot(hotOptions,0);PlaybackInputs hotInput;
    hotInput.codes[1]={127,255};hotInput.amplitudeCv[1]={5,true};
    std::array<LogicLevel,8> p1{},p2{};std::array<KnownByte,8> programs{};programs[1]={63,63};
    for(unsigned edge=1;edge<=80000;++edge){const unsigned phase=(edge/32)%4;
        p1[1]={true,phase==1};p2[1]={true,phase==3};hot.advance(edge,hotInput,p1,p2,programs);}
    require((hot.rangeExceededMask()&2)&&!hot.channel(1).analog.feedbackVolts.known,"supply-exceeding linear solution cannot become known audio");
    // Unknown hardware controls/startup are retained; default does not fabricate silence.
    auto unknown=std::make_unique<Machine>();std::vector<PlaybackFrame> u;unknown->enableClockedPlayback({},[&](const PlaybackFrame& f){u.push_back(f);});unknown->run(1000);
    require(u.size()==12,"unknown stream still advances physical time");for(const auto& f:u)for(const auto& x:f.meanVolts)require(!x.known,"unknown boot is not recorded as known zero");
    std::cout<<"PASS: exact44100/48000/96000 rational area/unknown spans; pre-edge observer; real Z80 eight-channel continuous DAC/deglitch/SSM/S3528-surrogate/VCA output,480frames/10ms, block invariance and unchanged CPU/PROM/WAIT/DRAM reports; reprogramming/unknown guards; not calibrated hardware audio\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
