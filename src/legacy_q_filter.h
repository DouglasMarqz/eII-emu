#pragma once
#include "../vendor/ssm2045-chip/include/ssm2045/chip.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace eii::analog {
// Fast approximation: the two original linear SSM2045 two-pole sections,
// coupled by the SSM Ga/Q feedback equation. Trapezoidal integration solves
// the four linear states analytically; only the bounded Ga feedback needs a
// scalar root. OTA/output saturation and chip offsets are deliberately absent.
// No firmware/sample clock is changed here. sampleRate is the DSP solver rate.
class LegacyQFilter {
public:
    explicit LegacyQFilter(ssm2045::FilterConfig c={}){configure(c);}
    void configure(const ssm2045::FilterConfig& c){
        if(!std::isfinite(c.sampleRate)||c.sampleRate<=0||!std::isfinite(c.gm0)||c.gm0<=0||
           !std::isfinite(c.vtef)||c.vtef<=0||!std::isfinite(c.fcBiasVolts)||
           !std::isfinite(c.qOscillationCurrent)||c.qOscillationCurrent<=0||
           !std::isfinite(c.vlGa)||c.vlGa<=0||!std::isfinite(c.w1)||!std::isfinite(c.w8))throw std::invalid_argument("Invalid Legacy+Q configuration");
        for(double cap:{c.caps.c1,c.caps.c2,c.caps.c3,c.caps.c4})if(!std::isfinite(cap)||cap<=0)throw std::invalid_argument("Invalid Legacy+Q capacitor");
        c_=c;lastFc_=1e300;
    }
    ssm2045::Outputs process(double input,double fcPin,double qCurrent){
        if(!std::isfinite(input)||!std::isfinite(fcPin)||!std::isfinite(qCurrent)||qCurrent<0)throw std::invalid_argument("Invalid Legacy+Q input");
        if(fcPin!=lastFc_){
            const double gm=c_.gm0*std::exp(std::clamp(-(fcPin-c_.fcBiasVolts)/c_.vtef,-40.,40.));
            const double h=gm/(2*c_.sampleRate);
            g_={h/c_.caps.c1,h/c_.caps.c2,h/c_.caps.c3,h/c_.caps.c4};
            d1_=1+g_[0]+g_[0]*g_[1]/10;d2_=1+g_[2]+g_[2]*g_[3]/10;
            b_=-g_[0]*g_[1]/(10*d1_);d_=-g_[2]*g_[3]/(10*d2_);lastFc_=fcPin;
        }
        // Open-loop taps y1 = a + b*(input-ga), y2 = c + d*y1.
        const double a=(z_[1]-g_[1]*(z_[0]+g_[0]*z_[1]/10)/d1_)/10;
        const double c=(z_[3]*(1+g_[2])-g_[3]*z_[2])/(10*d2_);
        const double aq=qCurrent/c_.qOscillationCurrent;
        const double k=c_.w8*c+(c_.w1+c_.w8*d_)*(a+b_*input);
        const double l=(c_.w1+c_.w8*d_)*b_;
        double ga=0;
        ++stats_.samples;
        if(aq>0){
            const double bound=aq*c_.vlGa;double lo=-bound,hi=bound;
            ga=std::clamp(lastGa_,lo,hi);bool converged=false;
            // Safeguarded scalar Newton: no four-state Newton/Jacobian solve.
            // Bracketing also covers high-Q multiple-root cases without NaNs.
            for(unsigned n=0;n<24;++n){
                const double t=std::tanh((k-l*ga)/c_.vlGa),f=ga-bound*t;
                ++stats_.iterations;
                if(std::abs(f)<1e-12+bound*1e-10){converged=true;break;}
                if(f<0)lo=ga;else hi=ga;
                const double derivative=1+aq*l*(1-t*t);
                const double candidate=ga-f/derivative;
                ga=(n<6&&std::isfinite(candidate)&&candidate>lo&&candidate<hi)?candidate:(lo+hi)*.5;
            }
            if(!converged){++stats_.fallbacks;ga=(lo+hi)*.5;}
        }
        lastGa_=ga;
        const double x0=(z_[0]+g_[0]*(input-ga+z_[1]/10))/d1_;
        const double x1=z_[1]-g_[1]*x0;
        const double x2=(z_[2]+g_[2]*(x1+z_[3])/10)/d2_;
        const double x3=z_[3]-g_[3]*x2;
        const std::array<double,4> x{x0,x1,x2,x3};
        for(unsigned i=0;i<4;++i){if(!std::isfinite(x[i])){reset();++stats_.held;return {};}}
        for(unsigned i=0;i<4;++i)z_[i]=2*x[i]-z_[i];
        return {x1/10,x3/10};
    }
    void reset()noexcept{z_.fill(0);lastGa_=0;}
    ssm2045::Filter::SolverStats solverStats()const noexcept{return stats_;}
private:
    ssm2045::FilterConfig c_;ssm2045::Filter::SolverStats stats_{};
    std::array<double,4> z_{},g_{};double lastFc_=1e300,lastGa_=0,d1_=1,d2_=1,b_=0,d_=0;
};
}
