#include "playback_analog.h"
#include <algorithm>
#include <complex>
#include <iostream>
#include <limits>
#include <stdexcept>
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static bool near(double a,double b,double tol=1e-11){return std::abs(a-b)<tol;}
int main(){try{
    using namespace eii::analog;
    constexpr unsigned base[]={0,33,99,231,495,1023,2079,4191};
    // Independent table endpoints/steps, not the implementation's formula.
    for(unsigned code=0;code<256;++code){const unsigned chord=(code>>4)&7;
        const int n=int(base[chord]+(2u<<chord)*(code&15))*((code&128)?1:-1);
        require(Am6072::signedUnits(code)==n,"all256 raw pin codes match manufacturer chord/step tables and sign");
        require(near(Am6072::signedDecodeCurrent(code,528e-6),n*0.25e-6),"nominal current uses Table3 scale");
        const auto pinCurrent=Am6072::decodePins({std::uint8_t(code),255},{528e-6,true});
        require(pinCurrent.iodPlusSink.known&&pinCurrent.iodMinusSink.known&&near(pinCurrent.iodPlusSink.value,(code&128)?std::abs(n)*0.25e-6:0)&&near(pinCurrent.iodMinusSink.value,(code&128)?0:std::abs(n)*0.25e-6),"sign steers sink current to the correct physical DAC pin");
        const auto volts=Am6072::eiiSteadyVolts({std::uint8_t(code),255},{5,true});
        require(volts.known&&near(volts.value,-2490*n*(5.0/9400/2112)),"EII I/V polarity and reference resistance");
    }
    require(near(Am6072::eiiSteadyVolts({127,255},{5,true}).value,5.036365147485493),"negative-SB full magnitude drives positive EII voltage");
    require(Am6072::signedUnits(0)==0&&Am6072::signedUnits(128)==0,"both decode signs have exact zero");
    for(unsigned mask=0;mask<256;++mask)for(unsigned value=0;value<256;++value)if(!(value&~mask)){
        bool first=true,equal=true;int n=0;
        for(unsigned c=0;c<256;++c)if(!((c^value)&mask)){
            const int x=int(base[(c>>4)&7]+(2u<<((c>>4)&7))*(c&15))*((c&128)?1:-1);
            if(first){n=x;first=false;}else equal&=n==x;
        }
        const auto result=Am6072::decodeCurrent({std::uint8_t(value),std::uint8_t(mask)},{528e-6,true});
        require(result.known==equal&&(!equal||near(result.value,n*0.25e-6)),"all6561 partial codes match independent compatible-code enumeration");
    }
    require(Am6072::decodeCurrent({0,0},{0,true}).known,"zero reference gives ideal zero even for unknown code");
    require(Am6072::decodeCurrent({0,127},{}).known,"both signed zeros stay zero with unknown reference");
    require(!Am6072::decodeCurrent({1,255},{}).known,"nonzero code with unknown reference stays unknown");
    for(double invalid:{-1e-6,0.002,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
        bool threw=false;try{Am6072::signedDecodeCurrent(255,invalid);}catch(const std::invalid_argument&){threw=true;}require(threw,"invalid DAC references rejected");
    }
    PlaybackAnalogChannel unknown(192000);PlaybackPins p;p.filterGm={100e-6,true};p.vcaBranchAmps={10e-6,true};
    p.diagnosticDirectReconstruction=true;p.diagnosticDirectDeglitch=true;
    require(!unknown.process({255,255},{5,true},p).dacVolts.known,"startup capacitor state not invented as zero");
    unknown.resetKnownZero();auto u=unknown.process({0,0},{5,true},p);
    require(!u.dacVolts.known&&!u.fourPoleVolts.known&&!u.feedbackVolts.known,"unknown sample taints stateful downstream stages");
    require(!unknown.process({255,255},{5,true},p).dacVolts.known,"later known input cannot erase unknown capacitor history");
    PlaybackAnalogChannel external(192000);external.resetKnownZero();p.diagnosticDirectReconstruction=false;
    auto e=external.process({128,255},{5,true},p);
    require(e.fourPoleVolts.known&&!e.branchCurrentAmps.known,"missing S3528 node is explicit, not silently bypassed");
    p.reconstructionVolts={0.01,true};external.resetKnownZero();e=external.process({128,255},{5,true},p);
    require(e.vcaInputVolts.known&&near(e.vcaInputVolts.value,0.01*470/10470/(1+1/(2*192000*10470e-6))),"external reconstruction feeds physical AC-coupled VCA input divider");
    PlaybackAnalogChannel deglitchGuard(192000);deglitchGuard.resetKnownZero();auto blocked=p;blocked.diagnosticDirectDeglitch=false;
    const auto d=deglitchGuard.process({127,255},{5,true},blocked);
    require(d.dacVolts.known&&!d.filterInputVolts.known,"unmodeled active deglitch node is not silently bypassed");
    PlaybackAnalogChannel channel0(192000);channel0.resetKnownZero();
    require(!channel0.process({255,255},{5,true},p,true).dacVolts.known,"unconfirmed channel0 MODE/SAR pins do not become playback");
    // Independent bilinear transfer product, including reference/current/I-V,
    // static input network, both filter pairs, VCA and feedback impedance.
    constexpr double rate=192000,gm=100e-6,current=10e-6;
    for(unsigned code:{127u,255u})for(double hz:{100.0,1000.0,5000.0}){
        PlaybackAnalogChannel c(rate);c.resetKnownZero();PlaybackPins pin;
        pin.filterGm={gm,true};pin.vcaBranchAmps={current,true};pin.diagnosticDirectReconstruction=true;pin.diagnosticDirectDeglitch=true;
        const double w=2*std::acos(-1.0)*hz/rate;
        std::complex<double> measured=0;
        // CV impulse, fixed full-scale code: all stages remain linear.
        for(unsigned n=0;n<96000;++n){const auto y=c.process({std::uint8_t(code),255},{n==0?1.0:0.0,true},pin);require(y.feedbackVolts.known,"known diagnostic path remains known");measured+=y.feedbackVolts.value*std::exp(std::complex<double>(0,-w*n));}
        const std::complex<double> s(0,2*rate*std::tan(w/2));
        auto pair=[&](double c1,double c2){const double ww=gm*gm/(c1*10*c2);return -ww/(s*s+s*(gm/c1)+ww);};
        const auto expected=((code==255?-2490.0:2490.0)/9400*8031/2112)/(1.0+s*2350e-6)/(code==127?(1.0+s*(2490*220e-12)):std::complex<double>(1))*(900.0/68900)*pair(20e-9,3.9e-9)*pair(20e-9,1.8e-9)*(470.0/10470)*(s*10470e-6)/(1.0+s*10470e-6)*(4085e-6*current/500e-6)*(-4700.0)/(1.0+s*(4700*1e-9));
        const double tolerance=1e-8*std::abs(expected)+1e-16;
        if(std::abs(measured-expected)>=tolerance)std::cerr<<hz<<" measured="<<measured<<" expected="<<expected<<" diff="<<std::abs(measured-expected)<<"\n";
        require(std::abs(measured-expected)<tolerance,"entire diagnostic cascade matches independent complex transfer product");
    }
    std::cout<<"PASS: Am6072 all256 codes/all6561 partial inputs vs manufacturer table, signed zeros/current/reference/EII polarity, unknown-state and MODE/S3528 guards; explicit zero-Q DAC/filter/VCA diagnostic cascade vs independent transfer at100/1000/5000Hz\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
