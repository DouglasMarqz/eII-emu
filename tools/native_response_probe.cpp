// Asset-free isolation of DAC reconstruction and S3528. No VCF/VCA/gain trim.
// Raw companded sine codes deliberately are NOT differential-encoded audio.
#include "native_audio.h"
#include "s3528_runtime.h"
#include <complex>
#include <iomanip>
#include <iostream>
using namespace eii::analog;
static unsigned encode(double units){
    unsigned code=0;double error=1e30;
    for(unsigned c=0;c<256;++c){const double d=std::abs(Am6072::signedUnits(c)-units);if(d<error){error=d;code=c;}}
    return code;
}
int main(){
    constexpr unsigned rate=96000,period=4320; // 120MHz/4320 = 27,777.777...Hz
    constexpr double pi=3.14159265358979323846;
    std::cout<<"Hz,reference_dB,native_dB,reference_S3528_dB,native_S3528_dB\n"<<std::setprecision(9);
    for(double hz:{50.,100.,300.,1000.,3000.,6000.,10000.}){
        NativeDacCoefficients coefficients;NativeDacChannel dac;
        S3528Runtime referenceFilter(rate),nativeFilter(rate);
        std::array<std::complex<double>,4> spectrum{};unsigned code=0;
        for(unsigned n=0;n<rate;++n){
            unsigned time=n*1250,end=time+1250;double area=0,referenceArea=0;
            while(time<end){
                const unsigned position=time%period;
                if(position==0)code=encode(1000*std::sin(2*pi*hz*time/nativeTimeRate));
                unsigned boundary=period;unsigned phase=0;
                for(unsigned edge:{192u,384u,576u,768u})if(position<edge){boundary=edge;break;}
                if(position>=192&&position<384)phase=1;
                if(position>=576&&position<768)phase=2;
                const unsigned length=std::min(end-time,boundary-position);
                const auto y=dac.advance(length,{std::uint8_t(code),255},{3,true},12|phase,coefficients);
                if(!y.known)return 1;
                area+=y.value;referenceArea+=Am6072::signedUnits(code)*3.*2490/(9400*2112)*50*length;
                time+=length;
            }
            const double ref=referenceArea/1250,native=area/1250;
            const auto rf=referenceFilter.process({ref,true},{35,63}),nf=nativeFilter.process({native,true},{35,63});
            if(n>=rate/2){const auto phasor=std::polar(2./(rate/2),-2*pi*hz*(n+.5)/rate);
                const std::array values={ref,native,rf.value,nf.value};for(unsigned k=0;k<4;++k)spectrum[k]+=values[k]*phasor;}
        }
        const double reference=std::abs(spectrum[0]);std::cout<<hz;
        for(auto s:spectrum)std::cout<<','<<20*std::log10(std::abs(s)/reference);
        std::cout<<'\n';
        if(hz==6000&&!(std::abs(spectrum[2]/spectrum[0])<.01&&std::abs(spectrum[3]/spectrum[1])<.01))return 2;
    }
}
