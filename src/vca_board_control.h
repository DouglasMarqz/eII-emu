#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace eii::analog {
// EII output channel 1, service sheet 7-45 (PDF107): ACV -> R91 4k7,
// C112 1uF to ground, R100 4k7 -> SSM2045 linear VCA2 pin15.
// The chip's nominal Figure4 input is 450ohm with about 0.5V cutoff when the
// control transistor base (EXPO pin14) is grounded. On the EII pin14 is
// -BIAS.V (sheet 7-52, PDF114): +15V -> R114 22k -> D5 1N914 (~0.572V at
// 0.66mA, Vishay 85622 Fig.1 typ.) -> R105 10k / R104 4.7k inverter IC96,
// i.e. about -0.269V. That base shift lowers the turn-on point to ~0.231V.
// This capacitor is distinct from DAC-reference C177. Piecewise analytical
// integration includes VCA-input loading and crosses the cutoff within a step.
class VcaBoardControl {
public:
    explicit VcaBoardControl(double rate):dt_(1/rate),
        offDecay_(std::exp(-dt_/offTau)),onDecay_(std::exp(-dt_/onTau)) {
        if(!std::isfinite(rate)||rate<=0)throw std::invalid_argument("Invalid VCA control rate");
    }
    double process(double acv) {
        const bool on=volts_>threshold;
        const double target=on?(acv*loadR+threshold*inputR)/(inputR+loadR):acv;
        const double decay=on?onDecay_:offDecay_;
        double next=target+(volts_-target)*decay;
        if((on&&next<threshold)||(!on&&next>threshold)) {
            const double tau=on?onTau:offTau;
            const double crossing=-tau*std::log((threshold-target)/(volts_-target));
            const double remaining=std::clamp(dt_-crossing,0.0,dt_);
            const double target2=on?acv:(acv*loadR+threshold*inputR)/(inputR+loadR);
            next=target2+(threshold-target2)*std::exp(-remaining/(on?offTau:onTau));
        }
        volts_=next;
        return std::max(0.0,(volts_-threshold)/loadR);
    }
    void reset() noexcept {volts_=0;}
    double capacitorVolts()const noexcept{return volts_;}
    static constexpr double chipThreshold=.5,biasVolts=-(4700.0/10000.0)*.572;
    static constexpr double inputR=4700,loadR=4700+450,capacitance=1e-6,threshold=chipThreshold+biasVolts;
    static constexpr double offTau=inputR*capacitance,onTau=inputR*loadR/(inputR+loadR)*capacitance;
private:
    double dt_,offDecay_,onDecay_,volts_=0;
};
}
