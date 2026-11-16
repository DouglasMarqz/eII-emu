// Standalone tests for SampleSar (7-43 + MC14559B). No ROMs.
#include "sample_input.h"
#include "am6072.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace eii;
static int failures=0;
#define CHECK(c) do{if(!(c)){std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c);++failures;}}while(0)
static constexpr LogicLevel H{true,true},L{true,false},U{};
// Synthetic monotonic sign-magnitude DAC (NOT the Am6072 law): volts for the
// code on the bus; encode adds half a step. Q7=1 is negative (EII convention).
static double dac(KnownByte q,LogicLevel encode){
    const double m=double(q.value&127)+(encode.high?0.5:0.0);
    return (q.value&128)?-m:m;
}
struct Rig {
    SampleSar sar;std::uint64_t t=0;SampleSar::Inputs in{L,L,H,U};
    double target=0;bool comparatorKnown=true;
    void tick(){
        if(comparatorKnown){const double v=dac(sar.register_(),sar.encode(in.imode));in.comparator={true,v>target};}
        else in.comparator=U;
        sar.cpuTick(t++,in);
    }
    void run(unsigned n){while(n--)tick();}
    // Machine-like history: settle idle with IMODE=0, an ISMPL pulse presets
    // IC10a, then IMODE=1 starts one conversion of whatever is held.
    void enterSampling(){
        run(12*10);in.ismpl=H;run(6);in.ismpl=L;run(5);in.imode=H;
        while(sar.conversions<1)tick();
        run(3);
    }
    void sampleOnce(){in.ismpl=H;run(6);in.ismpl=L;}
};
int main(){
    {   // Power-up: unknown register settles idle with EOC=1 while IMODE=0.
        Rig r;r.comparatorKnown=false;
        CHECK(!r.sar.stateKnown());
        r.run(12*10);
        CHECK(r.sar.stateKnown()&&r.sar.eoc().known&&r.sar.eoc().high);
        CHECK(r.sar.register_().known==0);
        CHECK(r.sar.quiescent(L));
        const auto e=r.sar.encode(L);CHECK(e.known&&!e.high);
        const auto w=r.sar.waitN(L,L);CHECK(w.known&&w.high); // IMODE=0: never waits
        const auto rd=r.sar.readyN();CHECK(rd.known&&rd.high);
        // ISMPL while IMODE=0 presets IC10a but cannot start a conversion.
        r.in.ismpl=H;r.run(7);r.in.ismpl=L;r.run(40);
        CHECK(r.sar.ic10a().known&&r.sar.ic10a().high);
        CHECK(r.sar.quiescent(L)&&r.sar.conversions==0);
    }
    {   // Exhaustive grid of targets: result equals independent enumeration.
        unsigned checked=0;
        for(int k=-130;k<=130;++k)for(double frac:{0.1,0.25,0.75,0.9}) {
            const double target=k+(k<0?-frac:frac);
            Rig r;r.target=target;r.enterSampling();
            r.sampleOnce();
            std::uint64_t eocTick=0;bool sawLow=false;
            for(unsigned i=0;i<12*14;++i){r.tick();const auto e=r.sar.eoc();if(e.known&&!e.high)sawLow=true;if(sawLow&&e.known&&e.high&&!eocTick)eocTick=r.t;}
            CHECK(sawLow&&eocTick);
            // Independent reference: SB = target<0; magnitude = max m in 0..127
            // with decision rule (neg: m+.5<|T|, pos: m+.5<=T).
            const bool neg=target<0;const double a=std::fabs(target);int best=0;
            for(int m=0;m<128;++m)if(neg?(m+0.5<a):(m+0.5<=a))best=m;
            const auto q=r.sar.register_();
            CHECK(q.known==255);
            if(q.value!=std::uint8_t((neg?128:0)|best)){std::printf("target %g got %02x want %02x\n",target,q.value,(neg?128:0)|best);++failures;}
            ++checked;
            // Exactly one conversion: SC fell when Q6 rose (no recirculation).
            r.run(12*30);CHECK(r.sar.conversions==2);
            CHECK(r.sar.ic10a().known&&!r.sar.ic10a().high);
        }
        std::printf("grid conversions checked %u\n",checked);
    }
    {   // Timing: start clock, 8 decisions, EOC on the falling edge after Q0.
        Rig r;r.target=17.25;r.enterSampling();
        while(r.t%12!=3)r.tick();
        r.in.ismpl=H;r.tick();r.in.ismpl=L;           // preset at t%12==3
        const std::uint64_t firstRising=r.t+(12-r.t%12)%12; // next tick%12==0
        std::uint64_t eocAt=0;
        for(unsigned i=0;i<200&&!eocAt;++i){const auto before=r.sar.eoc();r.tick();const auto e=r.sar.eoc();if(before.known&&!before.high&&e.high)eocAt=r.t-1;}
        CHECK(eocAt==firstRising+8*12+6);
        // -IWAIT is low only while reading during conversion.
    }
    {   // Read handshake: -IWAIT during conversion, -INPRDY after EOC, read clears it.
        Rig r;r.target=-3.3;r.enterSampling();r.sampleOnce();
        while(!r.sar.converting())r.tick();
        auto w=r.sar.waitN(H,L);CHECK(w.known&&!w.high);
        w=r.sar.waitN(H,H);CHECK(w.known&&w.high);
        while(r.sar.converting()||!r.sar.eoc().high)r.tick();
        CHECK(r.sar.readyN().known&&!r.sar.readyN().high);
        w=r.sar.waitN(H,L);CHECK(w.known&&w.high);
        r.in.pcsrinN=L;r.tick();r.in.pcsrinN=H;
        CHECK(r.sar.readyN().known&&r.sar.readyN().high);
        CHECK(r.sar.register_().value==0x82); // -3.3: m=2 (2.5<3.3), SB=1
    }
    {   // Unknown comparator leaves the undecided bits unknown, not invented.
        Rig r;r.enterSampling();r.comparatorKnown=false;r.sampleOnce();
        r.run(12*12);
        CHECK(r.sar.register_().known==0);CHECK(r.sar.unknownDecisions==8);
        // The read-back byte at the Z80 boundary is the caller's business.
    }
    {   // IMODE unknown makes the register unknown; IMODE=0 recovers in 9 clocks.
        Rig r;r.run(12*10);r.in.imode=U;r.run(24);CHECK(!r.sar.stateKnown());
        r.in.imode=L;r.run(12*10);CHECK(r.sar.quiescent(L));
    }
    {   // Am6072 encode (AMD 1979 pp.3-55/56): +half step of the current chord.
        using A=analog::Am6072;
        CHECK(A::encodeMagnitudeUnits(0x7f)==8031+128);           // FS +32uA at Iref 528uA
        CHECK(std::fabs((8031+128)*528e-6/2112-2039.75e-6)<1e-12);
        CHECK(A::signedEncodeUnits(0x80)==1&&A::signedEncodeUnits(0x00)==-1); // +/-0.25uA
        for(unsigned c=0;c<256;++c){const unsigned ch=(c>>4)&7,st=c&15;
            const unsigned dec=A::magnitudeUnits(std::uint8_t(c)),enc=A::encodeMagnitudeUnits(std::uint8_t(c));
            CHECK(enc-dec==(1u<<ch));                                // half of the chord step 2^(ch+1)
            if(st<15)CHECK(A::magnitudeUnits(std::uint8_t(c+1))-dec==(2u<<ch));
            // decision level lies strictly between adjacent decode levels
            if(st<15)CHECK(enc>dec&&enc<A::magnitudeUnits(std::uint8_t(c+1)));
        }
        // Mode selection: decode path is the unchanged eiiSteadyVolts.
        const analog::KnownSignal acv{4.98046875,true};
        for(unsigned c=0;c<256;++c){const KnownByte k{std::uint8_t(c),255};
            const auto d=A::eiiSteadyVoltsMode(k,acv,L),r=A::eiiSteadyVolts(k,acv);
            CHECK(d.known&&r.known&&d.value==r.value);
            const auto e=A::eiiSteadyVoltsMode(k,acv,H);
            CHECK(e.known&&std::fabs(e.value-(-2490.0*A::signedEncodeUnits(std::uint8_t(c))*acv.value/9400/2112))<1e-12);
            const auto u=A::eiiSteadyVoltsMode(k,acv,U);CHECK(!u.known);   // encode/decode differ for every code
        }
        // SB=1 (positive current) gives negative +IDAC.V on EII 7-44.
        CHECK(A::eiiSteadyVoltsMode({0xff,255},acv,H).value<0&&A::eiiSteadyVoltsMode({0x7f,255},acv,H).value>0);
    }
    std::printf(failures?"sample_input_tests FAIL %d\n":"sample_input_tests PASS\n",failures);
    return failures?1:0;
}
