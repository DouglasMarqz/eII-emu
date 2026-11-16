#include "s3528_runtime.h"
#include <complex>
#include <iostream>
#include <stdexcept>
using namespace eii::analog;
static void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
static std::complex<double> response(unsigned rate,unsigned code,double hz){
    S3528Runtime f(rate);double yc=0,ys=0,cc=0,ss=0,cs=0;const double w=2*std::acos(-1.)*hz/rate;
    for(unsigned n=0;n<rate/2;++n){const auto y=f.process({std::cos(w*n),true},{std::uint8_t(code),63});require(y.known&&std::isfinite(y.value),"finite sine response");
        if(n>=rate/4){const double c=std::cos(w*n),s=std::sin(w*n);yc+=y.value*c;ys+=y.value*s;cc+=c*c;ss+=s*s;cs+=c*s;}}
    const double d=cc*ss-cs*cs;return {(yc*ss-ys*cs)/d,-(ys*cc-yc*cs)/d};
}
int main()try{
    for(unsigned rate:{96000u,192000u}){
        double dcError=0,maxJump=0;
        for(unsigned code=0;code<64;++code){S3528Runtime f(rate);KnownSignal y;
            for(unsigned n=0;n<rate/4;++n)y=f.process({1,true},{std::uint8_t(code),63});
            dcError=std::max(dcError,std::abs(y.value-1));require(y.known&&std::abs(y.value-1)<.002,"all64 codes DC unity/stability");
            require(std::abs(f.cutoffHz()-5e6/(40*S3528Control::divisors[code]))<1e-9,"exact divider cutoff");}
        // A settled DC input must not turn into impulses when the code changes.
        S3528Runtime f(rate);for(unsigned n=0;n<rate;++n)f.process({1,true},{35,63});
        for(unsigned n=0;n<rate/2;++n){const auto y=f.process({1,true},{std::uint8_t((n*37)%64),63});require(y.known,"code jump known");maxJump=std::max(maxJump,std::abs(y.value-1));}
        require(maxJump<.002,"TPT states preserve DC through aggressive code changes");
        require(!f.process({1,true},{0,0}).known,"unknown code must not invent cutoff");
        require(f.process({0,true},{35,63}).known,"known code reacquires explicit zero state");
        require(!f.process({}, {35,63}).known,"unknown input guarded");
        const double fc=5e6/(40*S3528Control::divisors[35]);
        const auto pass=response(rate,35,.6*fc),edge=response(rate,35,1.06*fc),stop=response(rate,35,2*fc);
        const double edgeDb=20*std::log10(std::abs(edge/pass)),stopDb=20*std::log10(std::abs(stop/pass));
        require(std::abs(20*std::log10(std::abs(pass)))<.15,"nominal passband gain");require(edgeDb>-5.5&&edgeDb<-.5,"manufacturer edge envelope");require(stopDb<-45,"stopband rejection");
        constexpr double freqs[]={300,1200,2100,2850,3200,3350,3550},delayUs[]={201.4,218.3,277.5,453.5,881.6,1060.9,546.5};double delayError=0;
        for(unsigned i=0;i<7;++i){const double hz=freqs[i]*fc/3200,d=1;const auto a=response(rate,35,hz-d),b=response(rate,35,hz+d);const double us=-std::arg(b/a)/(4*std::acos(-1.)*d)*1e6;delayError=std::max(delayError,std::abs(us-delayUs[i]*3200/fc));}
        require(delayError<40,"runtime group delay vs held-out AMI points under40us");
        std::cout<<"PASS S3528 rate="<<rate<<"64codes DCerror="<<dcError<<" codeJumpDCerror="<<maxJump<<" edgeDb="<<edgeDb<<" stopDb="<<stopDb<<" delayErrorUs="<<delayError<<'\n';
    }
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
