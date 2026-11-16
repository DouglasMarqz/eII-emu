#include "s3528.h"
#include <complex>
#include <iostream>
#include <stdexcept>
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
// Measure the executing C++ kernel with sinusoidal input, fit its complex
// output at actual pair-update timestamps, then include the continuous hold.
// This does not evaluate the embedded SOS coefficients analytically.
static std::complex<double> transfer(double ratio,eii::analog::S3528Profile profile=eii::analog::S3528Profile::Ami1993TypicalFit){
    eii::analog::S3528NominalResponse response(profile);response.resetKnownZero();
    const double pi=std::acos(-1.),w=2*pi*ratio/40;
    double yc=0,ys=0,cc=0,ss=0,cs=0;
    for(unsigned n=0;n<28000;++n){response.sampleAtTwiceFilterClock({std::cos(w*n),true});auto y=response.sampleAtTwiceFilterClock({std::cos(w*(n+.5)),true});
        require(y.known&&std::isfinite(y.value),"curve kernel finite after explicit initialization");
        if(n>=8000){const double c=std::cos(w*(n+.5)),s=std::sin(w*(n+.5));yc+=y.value*c;ys+=y.value*s;cc+=c*c;ss+=s*s;cs+=c*s;}
    }
    const double det=cc*ss-cs*cs;const std::complex<double> h{(yc*ss-ys*cs)/det,-(ys*cc-yc*cs)/det};
    const double holdMagnitude=std::sin(pi*ratio/40)/(pi*ratio/40);
    return h*holdMagnitude*std::exp(std::complex<double>(0,-w/2));
}
static double delay(double frequency,eii::analog::S3528Profile profile=eii::analog::S3528Profile::Ami1993TypicalFit){
    const double ratio=frequency/3200,h=.0001;
    return -std::arg(transfer(ratio+h,profile)/transfer(ratio-h,profile))/(4*std::acos(-1.)*h)*1e6/3200;
}
int main(){try{
    using namespace eii::analog;
    // Only points excluded from fitting are used as plotted validation targets.
    constexpr double frequencies[]={300,1200,2100,2850,3200,3350,3550};
    constexpr double observed[]={201.4,218.3,277.5,453.5,881.6,1060.9,546.5};
    double worstDelay=0,oldWorstDelay=0;
    for(unsigned i=0;i<std::size(frequencies);++i){
        const double predicted=delay(frequencies[i]),old=delay(frequencies[i],S3528Profile::GenericElliptic);
        worstDelay=std::max(worstDelay,std::abs(predicted-observed[i]));oldWorstDelay=std::max(oldWorstDelay,std::abs(old-observed[i]));
        require(std::abs(predicted-observed[i])<30,"held-out delay within stated scan-reading allowance30us");
    }
    constexpr double stopFrequencies[]={6500,10000,18000},loss[]={52.62,56.98,68.86};double worstLoss=0;
    for(unsigned i=0;i<std::size(stopFrequencies);++i){const double predicted=-20*std::log10(std::abs(transfer(stopFrequencies[i]/3200)));worstLoss=std::max(worstLoss,std::abs(predicted-loss[i]));require(std::abs(predicted-loss[i])<3,"held-out stopband fit error under3dB (model budget, not scan uncertainty)");}
    const double reference=20*std::log10(std::abs(transfer(.6)));double ripple=0;
    for(unsigned n=1;n<=100;++n)ripple=std::max(ripple,std::abs(20*std::log10(std::abs(transfer(n/100.)))-reference));
    require(ripple<.1,"passband relative to0.6fc under0.1dB across measured grid");
    const double edge=20*std::log10(std::abs(transfer(1.06)))-reference;
    require(edge>-5.5&&edge<-.5,"1.06fc response lies inside published response envelope");
    require(std::abs(transfer(1.325))<std::pow(10,-65./20)&&std::abs(transfer(1.62))<std::pow(10,-70./20),"two nominal notch regions show deep rejection without injected noise");
    require(worstDelay<oldWorstDelay/2,"documented delay fit materially improves over generic response");
    S3528NominalResponse unknown;require(unknown.profile()==S3528Profile::Ami1993TypicalFit&&!unknown.output().known,"new default has explicit profile and unknown startup");
    unknown.resetKnownZero();unknown.invalidate();require(!unknown.output().known,"unknown phase invalidates fitted model too");
    std::cout<<"PASS: executing C++ AMI1993 curve fit vs10 held-out plot points; delay max="<<worstDelay<<"us (generic="<<oldWorstDelay<<"us), stopband max="<<worstLoss<<"dB; passband max="<<ripple<<"dB, edge="<<edge<<"dB; deep notches/stability/unknown guards; documentation fit, not physical calibration\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
