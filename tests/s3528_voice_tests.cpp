#include "nonlinear_voice.h"
#include "reference/nonlinear_voice_ce2e306.h"
#include <bit>
#include <chrono>
#include <iostream>
using namespace eii::analog;
static void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main()try{
    for(unsigned rate:{96000u,192000u}){
        // Only the documented level model differs from ce2e306 (docs/OUTPUT_STAGE.md):
        // with LevelModel::ce2e306() the live voice must match the frozen oracle bit for bit.
        NonlinearPlaybackVoice current(rate),enabled(rate);frozen_ce2e306::GoldenPlaybackVoice old(rate),oldEnabled(rate);
        current.levelModel(LevelModel::ce2e306());enabled.levelModel(LevelModel::ce2e306());
        double difference=0,energy=0;std::uint32_t rng=1234567;
        for(unsigned n=0;n<rate;++n){
            rng=rng*1664525u+1013904223u;
            eii::KnownByte code{std::uint8_t(rng>>24),std::uint8_t(n%7919?255:0)};
            const KnownSignal acv{2.+std::sin(n*.0005),true},fcv{1.+.5*std::sin(n*.0003),true},qcv{0,true};
            const double a=current.process(code,acv,fcv,qcv),b=old.process(code,acv,fcv,qcv);
            require(std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b),"bypass differs from frozen ce2e306 oracle");
            const double y=enabled.process(code,acv,fcv,qcv,{35,63},true);
            require(std::bit_cast<std::uint64_t>(y)==std::bit_cast<std::uint64_t>(oldEnabled.process(code,acv,fcv,qcv,{35,63},true)),"S3528 route differs from frozen ce2e306 oracle");
            require(std::isfinite(y),"enabled voice nonfinite");difference+=(a-y)*(a-y);energy+=a*a;
        }
        require(energy>1e-9&&difference>energy*.001,"S3528 must audibly affect broadband voice");
        require(enabled.process({120,255},{3,true},{1,true},{0,true},{0,0},true)==0,"unknown code must mute stage");
        const auto a=current.solverStats(),b=old.solverStats();
        require(a.samples==b.samples&&a.iterations==b.iterations&&a.fallbacks==b.fallbacks,"bypass solver workload changed");
        std::cout<<"PASS voice rate="<<rate<<" bypass and S3528 route bit exact vs frozen ce2e306; enabled difference/energy="<<difference/energy<<'\n';
        for(bool native:{false,true}){
            NonlinearPlaybackVoice test(rate),expected(rate);FilterCalibration profile;
            expected.calibrate(profile);profile[FilterParam::S3528Override]=1;profile[FilterParam::S3528Code]=35;test.calibrate(profile);
            for(unsigned n=0;n<12000;++n){const eii::KnownByte code{std::uint8_t((n/17)%2?0xb8:0x38),255};
                const KnownSignal acv{3,true},fcv{1,true},qcv{0,true},input{.1*std::sin(n*.3),true};
                const double a=test.process(code,acv,fcv,qcv,{59,63},true,input,native);
                const double b=expected.process(code,acv,fcv,qcv,{35,63},true,input,native);
                require(std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b),"S3528 lab override must select actual filter code on both routes");
            }
        }
        std::cout<<"PASS S3528 test override equals real code35, reference and native routes\n";
        {   // Hardware level model: S3528 OFF keeps the 7-45 input stage and removes only the
            // filter, one common host level: a 300 Hz tone has the same level ON and OFF.
            NonlinearPlaybackVoice off(rate),on(rate);double eo=0,en=0;
            for(unsigned n=0;n<rate/2;++n){const unsigned frame=n*48000/rate;const int v=int(std::lround(20*std::sin(2*3.14159265358979*300*frame/48000.0)));
                const eii::KnownByte code{std::uint8_t(v<0?0x80|-v:v),255};const KnownSignal acv{3,true},fcv{1,true},qcv{0,true};
                const double a=off.process(code,acv,fcv,qcv,{59,63},false),b=on.process(code,acv,fcv,qcv,{59,63},true);if(n>rate/4){eo+=a*a;en+=b*b;}}
            const double db=10*std::log10(en/eo);std::cout<<"PASS hardware level model S3528 ON/OFF level "<<db<<" dB\n";
            require(std::abs(db)<0.5,"S3528 ON and OFF must share one host level (input stage kept with the filter bypassed)");
        }
    }
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
