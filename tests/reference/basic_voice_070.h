#pragma once
// Frozen0.70 Legacy reference for optional-stage bypass regression.
#include "runtime_audio.h"
namespace eii::analog {
class ReferenceBasicPlaybackVoice070 {
public:
    explicit ReferenceBasicPlaybackVoice070(double rate):filter_(rate),feedback_(rate),
        reference_(rate,2350e-6),coupling_(rate,10470e-6) {
        vca_.setControlCurrent(500e-6);reset();
    }
    // 7-45: R122=22.6k from FCV, R130=124k to-15V, R112=1k
    // to ground. Ideal static pin voltage; bias current/RC settling omitted.
    static constexpr double frequencyPinVolts(double fcv) noexcept {
        return (fcv/22600.0-15.0/124000.0)/(1.0/22600.0+1.0/124000.0+1.0/1000.0);
    }
    static double nominalPoleHz(double fcv) {
        // Keep the old provisional midpoint anchor, not its reversed slope.
        // Manufacturer typical pin5 sensitivity is -19.3mV/octave.
        constexpr double anchorHz=30.0*22.62741699796952; // FCV2.5V
        return anchorHz*Ssm2045OpenLoop::gmRatioForPin5Delta(
            frequencyPinVolts(fcv)-frequencyPinVolts(2.5));
    }
    // The active deglitch stage is bypassed dynamically, not at unity gain:
    // its existing switched kernel converges to -R34/R26 at DC (7-45).
    static constexpr double nominalDeglitchGain=-10000.0/200.0;
    double process(KnownByte code,KnownSignal acv,KnownSignal fcv) {
        if(code.known!=255 || !acv.known || !fcv.known) {reset();return 0;}
        if(!std::isfinite(acv.value)||acv.value<0||acv.value>5 ||
           !std::isfinite(fcv.value)||fcv.value<0||fcv.value>5)
            throw std::invalid_argument("Invalid basic voice CV");
        if(fcv.value!=lastFcv_) {
            constexpr double pi=3.14159265358979323846;
            // Provisional absolute anchor; f0 is the first section's pole,
            // not a calibrated EII -3dB cutoff. Cache until firmware changes CV.
            filter_.setTransconductance(2*pi*nominalPoleHz(fcv.value)*std::sqrt(20e-9*10*3.9e-9));
            lastFcv_=fcv.value;
        }
        const auto cv=reference_.process(acv);
        const double dac=-2490*Am6072::signedDecodeCurrent(code.value,cv.value/9400);
        // Retain deglitch's nominal low-frequency gain while bypassing its
        // switched dynamics. S3528 remains bypassed; rail effects unmodeled.
        const double filtered=filter_.process(dac*nominalDeglitchGain*(900.0/68900.0)).fourPole;
        const double dc=coupling_.process({filtered,true}).value;
        return feedback_.process(vca_.outputCurrent((filtered-dc)*(470.0/10470.0)));
    }
    void reset() noexcept {
        filter_.reset();feedback_.reset();reference_.resetKnownZero();coupling_.resetKnownZero();
    }
private:
    Ssm2045OpenLoop filter_;
    Ssm2045LinearVca vca_;
    Channel1Feedback feedback_;
    KnownLowpass reference_,coupling_;
    double lastFcv_=-1;
};
}
