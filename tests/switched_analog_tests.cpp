#include "deglitch.h"
#include "s3528.h"
#include "machine.h"
#include "playback_analog.h"
#include <complex>
#include <iostream>
#include <memory>
#include <stdexcept>
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static bool nearly(double a,double b,double tol=1e-10){return std::abs(a-b)<tol;}
// Independent nodal equations, numerically integrated RK4. a is the R26/R34
// junction, b=C74, y=TL084 output. This does not reuse the exponential solver.
struct Nodal {double b=0,y=0;
    void advance(double time,double in,bool p1,bool p2) {
        constexpr double ron=150,c=470e-12;
        auto rhs=[&](Nodal v){
            const double a=(in/200+v.y/10000+(p1?v.b/ron:0))/(1.0/200+1.0/10000+(p1?1.0/ron:0));
            return Nodal{((p1?(a-v.b)/ron:0)-(p2?v.b/ron:0))/c,p2?-v.b/(ron*c):0};
        };
        const double h=time/2000;
        auto add=[](Nodal a,Nodal b,double h){return Nodal{a.b+h*b.b,a.y+h*b.y};};
        for(unsigned n=0;n<2000;++n){auto a=rhs(*this),b1=rhs(add(*this,a,h/2)),c1=rhs(add(*this,b1,h/2)),d=rhs(add(*this,c1,h));
            b+=h*(a.b+2*b1.b+2*c1.b+d.b)/6;y+=h*(a.y+2*b1.y+2*c1.y+d.y)/6;}
    }
};
int main(){try{
    using namespace eii;using namespace eii::analog;
    const LogicLevel off{true,false},on{true,true};
    Deglitch3202 d;require(!d.output().known,"capacitors unknown at startup");d.resetKnownZero();
    Nodal reference;
    for(unsigned n=0;n<400;++n) {
        const bool a=n%4==1,b=n%4==3;const double input=0.001*std::sin(n*.13);
        d.advance(0.2e-6,{input,true},{true,a},{true,b});reference.advance(0.2e-6,input,a,b);
        require(d.output().known&&nearly(d.output().value,reference.y)&&nearly(d.heldChargeVolts().value,reference.b),"charge transfer matches independent nodal RK4 over varying input");
    }
    d.resetKnownZero();
    for(unsigned n=0;n<1200;++n){d.advance(1.6e-6,{.001,true},on,off);d.advance(1.6e-6,{99,true},off,on);}
    require(nearly(d.output().value,-.05,1e-9),"small-signal DC closed-loop gain -R34/R26=-50");
    const double held=d.output().value;d.advance(1e-3,{},off,off);
    require(d.output().known&&d.output().value==held,"unknown DAC isolated while both switches open");
    Deglitch3202 clean,pulse;clean.resetKnownZero();pulse.resetKnownZero();
    for(unsigned n=0;n<100;++n){
        clean.advance(1e-6,{.001,true},on,off);pulse.advance(1e-6,{.001,true},on,off);
        clean.advance(1e-6,{0,true},off,off);pulse.advance(1e-6,{1,true},off,off);
        clean.advance(1e-6,{0,true},off,on);pulse.advance(1e-6,{},off,on);
        require(nearly(clean.output().value,pulse.output().value),"idle-window glitch rejected; transfer ignores current DAC");
    }
    d.advance(1e-6,{},on,off);require(d.output().known&&!d.heldChargeVolts().known,"unknown acquisition taints C74 first");
    require(!d.advance(1e-6,{0,true},off,on).known,"unknown acquired charge propagates on transfer");
    d.resetKnownZero();require(!d.advance(1e-6,{0,true},on,on).known,"unsupported switch overlap cannot claim known output");
    d.resetKnownZero();require(!d.advance(1e-6,{0,true},{},off).known,"unknown phase preserves uncertainty");
    bool bad=false;try{d.advance(-1,{},off,off);}catch(const std::invalid_argument&){bad=true;}require(bad,"negative interval rejected");
    // Use real HC393 settled phases at every 100ns boundary. A sample spans
    // both 1.6us active phases, not one host-rate endpoint observation.
    ChannelRequestTiming phase;phase.timer(on);for(unsigned n=0;n<64;++n)phase.fallingSourceClock(n);
    phase.timer(off);phase.timer(on);d.resetKnownZero();unsigned p1=0,p2=0;
    for(unsigned n=0;n<64;++n){auto a=phase.phi1(),b=phase.phi2();require(a.known&&b.known&&!(a.high&&b.high),"settled HC393 phases nonoverlapping");
        p1+=a.high;p2+=b.high;d.advance(100e-9,{.001,true},a,b);phase.fallingSourceClock(n);}
    require(p1==16&&p2==16&&d.output().known&&d.output().value<0,"actual divider phases acquire then invert/hold");
    S3528Control control;using CE=S3528Control::Enable;
    require(!control.cutoff().known,"unknown startup filter word");
    for(unsigned n=0;n<64;++n){
        auto old=control.activeCode();control.drive(CE::LoadGround,{std::uint8_t(n),255});
        require(control.activeCode().known==old.known&&control.activeCode().value==old.value,"CE ground loads without changing old ROM word");
        control.drive(CE::LoadGround,{std::uint8_t(n|192),255});control.drive(CE::HoldVdd,{});
        require(control.activeCode().known==63&&control.activeCode().value==n,"CE rising captures last six-bit data");
        control.drive(CE::HoldVdd,{std::uint8_t(n^63),255});require(control.activeCode().value==n,"CE VDD ignores live data");
    }
    require(nearly(control.cutoff().value,31250),"5MHz/code3F cutoff31.25kHz");
    control.completedWrite(0);require(nearly(control.cutoff().value,61.03515625),"5MHz/code00 cutoff61.035Hz");
    control.completedWrite(0x32);require(nearly(control.cutoff(3579545).value,3196.0223214285716),"manufacturer worked example code32 divisor28");
    // Selected table entries independently transcribed in a different address order.
    const unsigned sortedCode[]={63,62,61,60,59,58,56,53,47,46,45,44,39,57,55,54,52,51,50,49,48,25,24,23,22,21,20,19,18,17,16};
    const unsigned sortedN[]={4,5,6,9,10,12,14,15,16,18,20,22,33,23,24,25,26,27,28,29,30,47,50,53,56,60,64,69,74,81,89};
    for(unsigned i=0;i<std::size(sortedCode);++i){control.completedWrite(sortedCode[i]);require(nearly(control.cutoff().value,125000.0/sortedN[i]),"nonmonotone ROM address lookup");}
    control.drive(CE::TransparentVss,{9,63});require(control.activeCode().value==9,"VSS bypasses latch");
    control.drive(CE::Unknown,{});require(!control.cutoff().known,"unknown CE poisons programming history");
    control.drive(CE::TransparentVss,{0,0});require(!control.cutoff().known,"unknown live code has unknown cutoff");
    S3528Clock clock;
    require(!clock.masterHalfEdge({63,63}).known,"divider phase unknown at startup");
    for(unsigned code=0;code<64;++code){clock.alignForExperiment({std::uint8_t(code),63});unsigned pulses=0;
        for(unsigned n=1;n<=3*S3528Control::divisors[code];++n){auto tick=clock.masterHalfEdge({std::uint8_t(code),63});require(tick.known,"aligned phase known");
            pulses+=tick.high;require(tick.high==(n%S3528Control::divisors[code]==0),"cosine-prefilter sample event at every N master half edges");}
        require(pulses==3,"exact integer divider sample cadence");}
    require(!clock.masterHalfEdge({62,63}).known,"new divisor does not invent a divider reset phase");
    clock.alignForExperiment({63,63},3);require(clock.masterHalfEdge({63,63}).high,"explicit nonzero phase alignment");
    // Explicit surrogate regression against independent SciPy frequency response;
    // these are approximation values, NOT measurements of the physical IC.
    const double ratios[]={.2,.9,1.,1.06,1.27,1.3,1.32,1.62,2};
    const double magnitudes[]={.9910862175531351,.9952743662056227,.987790956046828,.36745989837044524,.002813676883048516,.0024857741122379503,.0020102477985675486,.002801920472798265,.0011629704141068056};
    for(unsigned f=0;f<std::size(ratios);++f){S3528NominalResponse response(S3528Profile::GenericElliptic);response.resetKnownZero();
        std::complex<double> sum{};constexpr unsigned frames=40000;const double w=2*std::acos(-1.0)*ratios[f]/40;
        for(unsigned n=0;n<frames+10000;++n){response.sampleAtTwiceFilterClock({std::cos(w*n),true});auto y=response.sampleAtTwiceFilterClock({std::cos(w*(n+.5)),true});
            if(n>=10000)sum+=y.value*std::exp(std::complex<double>(0,-w*n));}
        // Least-squares magnitude from sine and cosine basis removes finite-window leakage.
        double cc=0,ss=0,cs=0;for(unsigned n=10000;n<frames+10000;++n){const double c=std::cos(w*n),s=std::sin(w*n);cc+=c*c;ss+=s*s;cs+=c*s;}
        const double a=(sum.real()*ss+sum.imag()*cs)/(cc*ss-cs*cs),b=(-sum.imag()*cc-sum.real()*cs)/(cc*ss-cs*cs);
        require(nearly(std::hypot(a,b),magnitudes[f],2e-10),"explicit nominal elliptic/cosine response matches independent SciPy");
    }
    // Source-edge laboratory cascade: physical phase kernel -> SSM -> nominal
    // S3528 -> existing VCA. No diagnostic direct bypasses. Sampling phase and
    // capacitor initial conditions below are explicitly chosen experiments.
    PlaybackAnalogChannel channel(10e6);channel.resetKnownZero();
    PlaybackPins pins;pins.filterGm={100e-6,true};pins.vcaBranchAmps={10e-6,true};
    Deglitch3202 active;active.resetKnownZero();S3528NominalResponse reconstruction;reconstruction.resetKnownZero();
    S3528Clock cadence;cadence.alignForExperiment({63,63});
    ChannelRequestTiming request;request.timer(on);for(unsigned n=0;n<64;++n)request.fallingSourceClock(n);request.timer(off);
    KnownSignal dac{0,true};double peak=0;unsigned samples=0;
    for(unsigned edge=0;edge<200000;++edge){
        pins.deglitchedVolts=active.advance(100e-9,dac,request.phi1(),request.phi2());
        pins.reconstructionVolts=reconstruction.output();
        auto y=channel.process({std::uint8_t((edge/10000)%2?127:255),255},{.001,true},pins);
        require(y.dacVolts.known&&y.fourPoleVolts.known&&y.feedbackVolts.known&&std::isfinite(y.feedbackVolts.value),"clocked cascade remains finite and known without direct bypasses");
        dac=y.dacVolts;peak=std::max(peak,std::abs(y.feedbackVolts.value));
        if(cadence.masterHalfEdge({63,63}).high){reconstruction.sampleAtTwiceFilterClock(y.fourPoleVolts);++samples;}
        request.timer({true,edge%360!=359});request.fallingSourceClock(edge);
    }
    require(samples==50000&&peak>1e-7&&peak<.1,"nonzero bounded DAC/deglitch/SSM/S3528/VCA laboratory output at exact source cadence");
    S3528NominalResponse response;require(!response.sampleAtTwiceFilterClock({0,true}).known,"surrogate startup state unknown");
    response.resetKnownZero();response.sampleAtTwiceFilterClock({});require(!response.sampleAtTwiceFilterClock({0,true}).known,"unknown charge contaminates surrogate");
    // Real CPU OUT strobes, every channel including unrotated channel0.
    auto m=std::make_unique<Machine>();unsigned pc=0;
    for(unsigned ch=0;ch<8;++ch){m->main.rom[pc++]=0x3e;m->main.rom[pc++]=std::uint8_t(192+ch);m->main.rom[pc++]=0xd3;m->main.rom[pc++]=std::uint8_t(0x40+ch);}
    m->main.rom[pc]=0x76;
    bool sawSelected=false,sawCommit=false;
    for(unsigned tick=0;tick<1000;++tick){m->run(1);
        if(m->stagingWritePending&&m->stagingWritePort==0x40){sawSelected=true;require(m->reconstructionControl[0].writes==0,"no premature filter update while write asserted");}
        if(m->reconstructionControl[0].writes)sawCommit=true;
    }
    require(sawSelected&&sawCommit,"bus test observes both sides of strobe");
    auto snapshot=m->playbackSwitchInputs();
    for(unsigned ch=0;ch<8;++ch)require(snapshot.reconstructionCode[ch].known==63&&snapshot.reconstructionCode[ch].value==ch&&m->reconstructionControl[ch].writes==1,"all eight CPU ports update corresponding S3528 once");
    require(m->unmappedIo==0,"filter writes mapped");
    m->output(m->scanner,0x40,63);require(m->reconstructionControl[0].activeCode().value==0,"scanner cannot program main filters");
    std::cout<<"PASS: deglitch nodal RK4/charge/hold/glitch isolation/real HC393 phases; S3528 CE/ROM/cutoff, explicit nominal elliptic response, real Z80 all-eight strobe-release writes; analog scheduler and calibrated chip transfer remain pending\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
