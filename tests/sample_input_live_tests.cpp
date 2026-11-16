// LiveSampleInput: deterministic capture-timeline tests (no ROMs).
// Producer: 10 ms (480-sample) packets on a device clock +150 ppm with
// +/-3 ms delivery jitter. Machine: 20000-tick (5 ms) quanta whose wall cost
// follows a speed profile 0.95..1.05 with 0.80x slow periods (2 s) and 45 ms
// stalls, paced to wall time AND gated by ready() exactly like the GUI worker.
// Required while sampling: 0 underruns, 0 overruns, 0 discards, and the
// consumed stream equals the source sample-for-sample (no splice/skip/repeat).
#include "sample_input_live.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
using eii::analog::LiveSampleInput;
static int failures=0;
#define CHECK(c,msg) do{if(!(c)){std::printf("FAIL: %s\n",msg);++failures;}}while(0)
struct Lcg{std::uint64_t s;double next(){s=s*6364136223846793005ull+1442695040888963407ull;return double(s>>11)/double(1ull<<53);}};
int main(){
    const double pi=3.14159265358979323846;
    {
        auto in=std::make_unique<LiveSampleInput>();
        Lcg r{12345};
        const double devRate=48000*(1+150e-6),f=440;
        auto content=[&](double n){return 0.5*std::sin(2*pi*f*n/48000);};   // per device sample index
        std::uint64_t produced=0;double nextPacket=0,wall=0,busyUntil=0,emuLead=0;
        std::uint64_t tick=1000;std::vector<float> packet(480);
        double maxErr=0,n0=-1;std::uint64_t checked=0,holds=0,quanta=0;double maxFill=0;
        auto speedAt=[&](double t){double s=0.95+0.1*r.next();if(std::fmod(t,10.0)>4&&std::fmod(t,10.0)<6)s=0.80;return s;};
        while(wall<62.0) {
            while(nextPacket<=wall){
                for(unsigned i=0;i<480;++i)packet[i]=float(content(double(produced+i)));
                produced+=480;in->push(packet);
                nextPacket=double(produced)/devRate+(r.next()-0.5)*0.006;
            }
            const bool stall=std::fmod(wall,6.0)>3.0&&std::fmod(wall,6.0)<3.045;
            if(!stall&&wall>=busyUntil&&emuLead<=0.0) {
                if(in->ready(tick,20000)) {
                    for(std::uint64_t k=0;k<20000;k+=21){   // ~192 kHz chain steps
                        const double v=in->volts(tick+k);
                        if(tick>1000+4000000) {             // after 1 s: check against the source
                            const double n=(double(tick+k)-1000)*48000/4e6;   // emulated 48 kHz index
                            if(n0<0){                        // align once (prefill latency)
                                double best=1e9;for(double c=0;c<6000;c+=0.25){double e=std::fabs(content(c+n)-v)+std::fabs(content(c+n+5)-in->volts(tick+k));if(e<best){best=e;n0=c;}}
                            } else {maxErr=std::max(maxErr,std::fabs(content(n0+n)-v));++checked;}
                        }
                    }
                    tick+=20000;++quanta;busyUntil=wall+0.005/speedAt(wall);emuLead+=0.005;
                    maxFill=std::max(maxFill,double(in->buffered()));
                } else ++holds;
            }
            wall+=0.0005;emuLead-=0.0005;if(emuLead<-0.02)emuLead=-0.02;   // pacer: bounded debt
        }
        std::printf("timeline 62 s: quanta=%llu holds=%llu underruns=%llu overruns=%llu discarded=%llu prefills=%llu max fill %.0f ms checked=%llu maxErr=%.2e\n",
            (unsigned long long)quanta,(unsigned long long)holds,(unsigned long long)in->underruns.load(),(unsigned long long)in->overruns.load(),
            (unsigned long long)in->discarded.load(),(unsigned long long)in->prefills.load(),maxFill/48,(unsigned long long)checked,maxErr);
        CHECK(in->underruns.load()==0&&in->overruns.load()==0,"no underrun/overrun events while sampling");
        CHECK(in->discarded.load()==0&&in->prefills.load()==1,"nothing discarded, single prefill");
        CHECK(checked>10000000&&maxErr<2e-3,"consumed stream = source, sample-continuous (cubic error only): no splice, skip or repeat");
        CHECK(holds>0,"input availability paced the machine");
    }
    {   // Not sampling: the producer keeps the fill near target quietly.
        auto in=std::make_unique<LiveSampleInput>();std::vector<float> p(480,0.1f);
        for(unsigned k=0;k<200;++k)in->push(p);
        CHECK(in->buffered()<=LiveSampleInput::target+LiveSampleInput::slack,"inactive fill held near target");
        CHECK(in->overruns.load()==0&&in->discarded.load()>0,"inactive excess discarded quietly, not overrun");
        double v=0;for(unsigned t=0;t<4000;t+=21)v=in->volts(1000+t);
        CHECK(in->underruns.load()==0&&std::fabs(v-0.1)<1e-6,"sampling starts at target latency without underrun");
        in->idle(0);for(unsigned k=0;k<100;++k)in->push(p);
        CHECK(in->overruns.load()==0&&in->buffered()<=LiveSampleInput::target+LiveSampleInput::slack,"idle(): producer trims quietly");
        for(unsigned t=0;t<4000;t+=21)v=in->volts(t);                       // reboot: ticks restart
        CHECK(in->underruns.load()==0,"reboot restarts quietly");
        // Unpaced consumer outrunning the producer: ONE event, 0 V, timeline kept.
        for(std::uint64_t t=4000;t<4000+4000000;t+=21)v=in->volts(t);
        CHECK(in->underruns.load()==1&&v==0,"starved consumer: a single counted underrun episode");
        const auto before=in->pos();for(unsigned k=0;k<10;++k)in->push(p);(void)in->volts(4000+4000000+4000);
        CHECK(in->pos()>before,"after an underrun the read position keeps emulated time (gap, not splice)");
    }
    std::printf(failures?"sample_input_live_tests FAIL %d\n":"PASS: sample input capture timeline (input-paced, continuous, quiet idle)\n",failures);
    return failures?1:0;
}
