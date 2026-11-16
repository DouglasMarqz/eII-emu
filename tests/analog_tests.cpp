#include "ssm2045.h"
#include <complex>
#include <iostream>
#include <numbers>
#include <stdexcept>

static void require(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
static bool close(double a,double b,double tolerance=1e-9) {return std::abs(a-b)<tolerance;}
// Manufacturer transfer function evaluated in the analog s-plane. The test
// compares sampled impulse responses to this independent analytic reference.
static std::complex<double> pair(std::complex<double> s,double gm,double c1,double c2) {
    const double w2=gm*gm/(c1*10*c2);
    return -w2/(s*s+(gm/c1)*s+w2);
}
int main() {
    try {
        constexpr double rate=192000,gm=200e-6,pi=std::numbers::pi;
        eii::analog::Ssm2045Caps caps;
        eii::analog::Ssm2045OpenLoop filter(rate,caps);filter.setTransconductance(gm);
        // These damping factors follow EII's actual C1..C4, not a generic
        // identical-pole cascade or assumed Butterworth coefficient table.
        require(close(std::sqrt(10*caps.c2/caps.c1),1.396424004376894,1e-12),"EII first-section damping");
        require(close(std::sqrt(10*caps.c4/caps.c3),0.948683298050514,1e-12),"EII second-section damping");
        const double frequencies[]={100,1000,5000,15000,40000};
        std::complex<double> measured2[5]{},measured4[5]{};
        double sum2=0,sum4=0;
        for(int n=0;n<32768;++n) {
            const auto taps=filter.process(n==0?1:0);
            require(std::isfinite(taps.twoPole)&&std::isfinite(taps.fourPole),"finite impulse response");
            sum2+=taps.twoPole;sum4+=taps.fourPole;
            for(int j=0;j<5;++j) {
                const auto phase=std::polar(1.0,-2*pi*frequencies[j]*n/rate);
                measured2[j]+=taps.twoPole*phase;measured4[j]+=taps.fourPole*phase;
            }
        }
        require(close(sum2,-1) && close(sum4,1),"SSM2045 open-loop DC gains and tap polarities");
        for(int j=0;j<5;++j) {
            const std::complex<double> s(0,2*rate*std::tan(pi*frequencies[j]/rate));
            const auto expected2=pair(s,gm,caps.c1,caps.c2);
            const auto expected4=expected2*pair(s,gm,caps.c3,caps.c4);
            require(std::abs(measured2[j]-expected2)<1e-10,"2-pole impulse agrees with manufacturer equation");
            require(std::abs(measured4[j]-expected4)<1e-10,"4-pole impulse agrees with manufacturer equation");
        }
        require(close(eii::analog::Ssm2045OpenLoop::gmRatioForPin5Delta(-0.0193),2),"negative FC voltage raises frequency one octave");
        require(close(eii::analog::Ssm2045OpenLoop::gmRatioForPin5Delta(0.0193),0.5),"positive FC voltage lowers frequency one octave");
        filter.reset();filter.setTransconductance(0);
        for(int i=0;i<100;++i)require(filter.process(1).fourPole==0,"zero gm preserves reset capacitor states");
        for(int n=0;n<100000;++n) {
            if(n%100==0) filter.setTransconductance(1e-6*std::exp2((n/100)%16));
            const auto out=filter.process(std::sin(n*0.1)*0.01);
            require(std::isfinite(out.fourPole)&&std::abs(out.fourPole)<1,"finite bounded response under gm changes");
        }
        eii::analog::Ssm2045LinearVca vca;
        require(vca.outputCurrent(0.01)==0,"VCA defaults off");
        vca.setControlCurrent(500e-6);
        require(close(vca.outputCurrent(0.001),4.085e-6,1e-15),"VCA datasheet transconductance point");
        vca.setControlCurrent(250e-6);
        require(close(vca.outputCurrent(0.001),2.0425e-6,1e-15),"VCA linear current scaling");
        require(close(vca.outputCurrent(-0.001),-2.0425e-6,1e-15),"VCA small-signal odd symmetry");
        eii::analog::Channel1Feedback feedback(rate);
        double dc=0;for(int i=0;i<10000;++i)dc=feedback.process(1e-6);
        require(close(dc,-0.0047),"R154 transimpedance DC gain");
        feedback.reset();std::complex<double> measured=0;
        for(int i=0;i<10000;++i) measured+=feedback.process(i==0?1e-6:0)*std::polar(1.0,-2*pi*10000*i/rate);
        const std::complex<double> s(0,2*rate*std::tan(pi*10000/rate));
        const auto expected=-4700e-6/(1.0+s*4700.0*1e-9);
        require(std::abs(measured-expected)<1e-12,"R154/C161 complex feedback impedance");
        bool rejected=false;try {vca.setControlCurrent(-1);}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"negative VCA control current rejected");
        rejected=false;try {eii::analog::Ssm2045OpenLoop invalid(0);}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"invalid filter sample rate rejected");
        std::cout<<"PASS: SSM2045 zero-Q-current pole equations, EII capacitor ratios, FC sensitivity, linear VCA gm and R154/C161 impedance; not hardware/audio calibration\n";
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
