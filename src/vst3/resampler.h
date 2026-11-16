#pragma once
// Host-rate conversion of the emergent 48 kHz output. Emulated clocks are never
// changed: the plug-in renders the same 48 kHz stream as the standalone and a
// fixed-ratio windowed-sinc interpolator reads it at the host rate.
//
// Kernel: 64 taps (K=32 each side), Kaiser window beta 8, 1024 phase tables
// with linear interpolation between adjacent phases, every phase normalized to
// unity DC gain. Cutoff (-6 dB) = 0.94 * min(24 kHz, host Nyquist): an
// anti-aliasing low-pass when the host runs slower than 48 kHz. 44.1 kHz:
// passband flat (+-0.1 dB) beyond 18 kHz, > 60 dB stopband from ~22.6 kHz.
// Group delay K frames at 48 kHz is part of the reported plug-in latency.
// 48 kHz hosts bypass the interpolator entirely (bit-exact stream).
#include <array>
#include <cmath>
#include <vector>
#include <algorithm>

namespace eii::vst {
class SincResampler {
public:
    static constexpr int K=32,taps=2*K,phases=1024;
    explicit SincResampler(double hostRate=48000){configure(hostRate);}
    void configure(double hostRate){
        const double fc=0.94*std::min(1.0,hostRate/48000.0);
        table_.assign(std::size_t(phases+1)*taps,0.f);
        for(int p=0;p<=phases;++p){
            const double frac=double(p)/phases;double sum=0;std::array<double,taps> h{};
            for(int k=0;k<taps;++k){
                const double t=double(k-K+1)-frac; // tap k multiplies x[ip-K+1+k]
                const double x=std::abs(t)/K;
                const double w=x>=1?0:bessel0(8.0*std::sqrt(1-x*x))/bessel0(8.0);
                const double s=t==0?1:std::sin(pi*fc*t)/(pi*fc*t);
                h[k]=fc*s*w;sum+=h[k];
            }
            for(int k=0;k<taps;++k)table_[std::size_t(p)*taps+k]=float(h[k]/sum);
        }
    }
    // x(i) must return frame ip-K+1+i for i in [0,taps).
    template<class Get> float interpolate(double frac,Get&& x)const{
        const double fp=frac*phases;int p=int(fp);if(p>=phases)p=phases-1;const float a=float(fp-p);
        const float* h0=&table_[std::size_t(p)*taps];const float* h1=h0+taps;
        float acc=0;for(int k=0;k<taps;++k)acc+=x(k)*(h0[k]+a*(h1[k]-h0[k]));
        return acc;
    }
private:
    static constexpr double pi=3.14159265358979323846;
    static double bessel0(double x){double s=1,t=1;for(int k=1;k<40;++k){t*=(x/(2*k))*(x/(2*k));s+=t;if(t<1e-12*s)break;}return s;}
    std::vector<float> table_;
};
}
