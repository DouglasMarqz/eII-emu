#pragma once
#include <cmath>
#include <stdexcept>

namespace eii::analog {
// Small-signal, zero-Q-current kernel, derived from the manufacturer's
// two-pole equations. Not a complete nonlinear SSM2045 or EII audio channel.
struct Ssm2045Caps {
    double c1=20e-9,c2=3.9e-9,c3=20e-9,c4=1.8e-9; // EII channel 1, sheet 7-45
};
struct FilterTaps {double twoPole=0,fourPole=0;};
class Ssm2045OpenLoop {
public:
    explicit Ssm2045OpenLoop(double sampleRate,Ssm2045Caps caps={})
        : rate_(sampleRate),caps_(caps) {
        positive(rate_);positive(caps.c1);positive(caps.c2);positive(caps.c3);positive(caps.c4);
        setTransconductance(0);
    }
    // gm is required explicitly: absolute gm at a given FC voltage has not
    // been calibrated against an EII. Zero gm freezes the capacitor states.
    void setTransconductance(double siemens) {
        if(!std::isfinite(siemens) || siemens<0) throw std::invalid_argument("Invalid filter gm");
        const auto a=coefficients(siemens,caps_.c1,caps_.c2);
        const auto b=coefficients(siemens,caps_.c3,caps_.c4);
        first_.g=a.g;first_.damping=a.damping;second_.g=b.g;second_.damping=b.damping;
    }
    FilterTaps process(double inputVolts) noexcept {
        const double a=first_.process(inputVolts);
        return {a,second_.process(a)};
    }
    void reset() noexcept {first_.s1=first_.s2=second_.s1=second_.s2=0;}
    // Typical datasheet sensitivity: -19.3mV per octave AT pin 5.
    // This is a ratio only, not an absolute frequency or board-CV mapping.
    static double gmRatioForPin5Delta(double volts) {
        if(!std::isfinite(volts)) throw std::invalid_argument("Invalid FC delta");
        const double ratio=std::exp2(-volts/0.0193);
        if(!std::isfinite(ratio) || ratio==0) throw std::invalid_argument("FC delta out of range");
        return ratio;
    }
private:
    struct Section {
        double g=0,damping=1,s1=0,s2=0;
        double process(double input) noexcept {
            // Trapezoidal state-variable integrators; output polarity is
            // negative for each two-pole section in the SSM2045 equations.
            const double high=(input-(damping+g)*s1-s2)/(1+damping*g+g*g);
            const double band=g*high+s1;
            const double low=g*band+s2;
            s1=2*band-s1;s2=2*low-s2;
            return -low;
        }
    };
    struct Coefficients {double g,damping;};
    Coefficients coefficients(double gm,double c1,double c2) const {
        // omega0=gm/sqrt(C1*10*C2), damping=sqrt(10*C2/C1).
        const double g=gm/(2*rate_*std::sqrt(c1*10*c2));
        const double damping=std::sqrt(10*c2/c1);
        if(!std::isfinite(g) || !std::isfinite(damping) || !std::isfinite(g*g) || !std::isfinite(damping*g))
            throw std::invalid_argument("Filter coefficients out of range");
        return {g,damping};
    }
    static void positive(double value) {
        if(!std::isfinite(value) || value<=0) throw std::invalid_argument("Expected finite positive value");
    }
    double rate_;
    Ssm2045Caps caps_;
    Section first_,second_;
};

// One VCA branch in the small-signal linear-control regime, with branch
// current supplied explicitly. The mixer/exponential bias network is absent.
class Ssm2045LinearVca {
public:
    void setControlCurrent(double amperes) {
        if(!std::isfinite(amperes) || amperes<0 || amperes>500e-6)
            throw std::invalid_argument("VCA branch current outside characterized range");
        // Manufacturer typical 4085 micromhos at 500uA branch control.
        gm_=4085e-6*(amperes/500e-6);
    }
    double outputCurrent(double inputVolts) const noexcept {return gm_*inputVolts;}
private:
    double gm_=0;
};

// EII R154/C161 feedback impedance. Input current is defined as flowing INTO
// the buffer summing node; this sign convention does not calibrate IC polarity.
class Channel1Feedback {
public:
    explicit Channel1Feedback(double sampleRate):g_(1/(2*sampleRate*4700*1e-9)) {
        if(!std::isfinite(sampleRate) || sampleRate<=0 || !std::isfinite(g_))
            throw std::invalid_argument("Invalid feedback sample rate");
    }
    double process(double inputCurrent) noexcept {
        const double out=(s_+g_*(-4700*inputCurrent))/(1+g_);
        s_=2*out-s_;return out;
    }
    void reset() noexcept {s_=0;}
private:
    double g_,s_=0;
};
}
