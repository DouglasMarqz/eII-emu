#pragma once
#include "am6072.h"
#include "channel_requests.h"

namespace eii::analog {
// EII 7-45: R26=200, R34=10k, C74=C83=470pF. Nominal LF13202
// Ron=150 ohms, ideal TL084 virtual ground. Settled, nonoverlapping phases;
// switching delay, injection, leakage, finite op-amp bandwidth and clipping
// are NOT modeled. The caller must split intervals at every phase/input edge.
class Deglitch3202 {
public:
    KnownSignal heldChargeVolts() const noexcept {return sample_;}
    KnownSignal output() const noexcept {return output_;}
    void resetKnownZero() noexcept {sample_={0,true};output_={0,true};}
    KnownSignal advance(double seconds,KnownSignal input,LogicLevel phi1,LogicLevel phi2) {
        if(!std::isfinite(seconds)||seconds<0 || (input.known&&!std::isfinite(input.value)))
            throw std::invalid_argument("Invalid deglitch interval/input");
        if(seconds==0)return output_;
        if(!phi1.known||!phi2.known || (phi1.high&&phi2.high)) {
            sample_={};output_={};return output_; // unsupported overlap/uncertain charge history
        }
        if(phi1.high) {
            if(!sample_.known||!output_.known||!input.known)sample_={};
            else {
                constexpr double rth=200.0*10000/10200;
                const double target=(10000*input.value+200*output_.value)/10200;
                sample_.value+=(target-sample_.value)*(-std::expm1(-seconds/((150+rth)*470e-12)));
            }
        } else if(phi2.high) {
            if(!sample_.known||!output_.known) {sample_={};output_={};}
            else {
                const double transfer=sample_.value*(-std::expm1(-seconds/(150*470e-12)));
                sample_.value-=transfer;output_.value-=transfer; // C74/C83=1
            }
        }
        // Open switches isolate the DAC; unknown input cannot poison a hold.
        return output_;
    }
private:
    KnownSignal sample_{},output_{};
};
}
