#pragma once
#include <array>
#include "am6072.h"
#include "ssm2045.h"

namespace eii::analog {
// Explicit pin-domain experiment controls. No invented mapping of the
// board frequency/Q/mixer CVs to gm, Q gain or VCA branch current.
struct PlaybackPins {
    KnownSignal filterGm{},vcaBranchAmps{},deglitchedVolts{},reconstructionVolts{};
    bool channel0PlaybackConfirmed=false;
    bool diagnosticDirectReconstruction=false;
    bool diagnosticDirectDeglitch=false;
};
struct PlaybackObservation {
    KnownSignal dacVolts{},filterInputVolts{},twoPoleVolts{},fourPoleVolts{};
    KnownSignal vcaInputVolts{},branchCurrentAmps{},feedbackVolts{};
    bool diagnosticDirectReconstruction=false;
    bool diagnosticDirectDeglitch=false;
};
// Ideal trapezoidal lowpass. Unknown input/state stays unknown until an
// explicit initial condition is supplied; no hidden zero substitution.
class KnownLowpass {
public:
    KnownLowpass(double rate,double tau):g_(1/(2*rate*tau)) {
        if(!std::isfinite(rate)||rate<=0||!std::isfinite(tau)||tau<=0||!std::isfinite(g_))
            throw std::invalid_argument("Invalid analog lowpass");
    }
    KnownSignal process(KnownSignal input) {
        if(input.known&&!std::isfinite(input.value))throw std::invalid_argument("Nonfinite analog input");
        if(!input.known||!state_.known){state_={};return {};}
        const double y=(state_.value+g_*input.value)/(1+g_);
        state_={2*y-state_.value,true};return {y,true};
    }
    void setTimeConstant(double rate,double tau) {if(!std::isfinite(rate)||rate<=0||!std::isfinite(tau)||tau<=0)throw std::invalid_argument("Invalid lowpass time constant");g_=1/(2*rate*tau);}
    void resetKnownZero() noexcept {state_={0,true};}
private:
    double g_;
    KnownSignal state_{};
};
// Channel1 values also used by the other seven identically valued channel
// drawings. Switched deglitch and S3528 are not approximated as a full model.
// The filter uses its zero-Q-current small-signal kernel only.
class PlaybackAnalogChannel {
public:
    explicit PlaybackAnalogChannel(double rate):reference_(rate,2350e-6),
        dacFeedback_(rate,2490*220e-12),coupling_(rate,10470e-6),filter_(rate),feedback_(rate) {}
    void resetKnownZero() noexcept {
        reference_.resetKnownZero();dacFeedback_.resetKnownZero();coupling_.resetKnownZero();
        filter_.reset();feedback_.reset();filterKnown_=feedbackKnown_=true;
    }
    PlaybackObservation process(KnownByte code,KnownSignal amplitudeCv,const PlaybackPins& pins,bool channel0=false) {
        PlaybackObservation out;out.diagnosticDirectReconstruction=pins.diagnosticDirectReconstruction;out.diagnosticDirectDeglitch=pins.diagnosticDirectDeglitch;
        if(amplitudeCv.known && (!std::isfinite(amplitudeCv.value)||amplitudeCv.value<0||amplitudeCv.value>5))
            throw std::invalid_argument("EII amplitude CV outside0..5V");
        // Split4.7k/4.7k reference network: midpoint C1uF, loaded by
        // the reference amplifier's ideal virtual ground. DC Iref=CV/9.4k,
        // tau=(4.7k||4.7k)*1uF=2.35ms.
        const auto cv=reference_.process(amplitudeCv);
        auto current=Am6072::decodePins(code,{cv.value/9400,cv.known});
        if(channel0&&!pins.channel0PlaybackConfirmed)current={};
        // Pin16 IOD+ sinks into the noninverting input: Vplus=-R8*I+.
        // Pin17 IOD- sinks into the summing node: Vout-Vplus=Zf*I-.
        // C187 is only across R17, so only that latter component is filtered.
        const auto minus=dacFeedback_.process({2490*current.iodMinusSink.value,current.iodMinusSink.known});
        out.dacVolts={minus.value-2490*current.iodPlusSink.value,minus.known&&current.iodPlusSink.known};
        // The active 3202/TL084 deglitch output drives R138=68k into
        // typical internal900ohm. Its clocked analog behavior is a required
        // external node unless the diagnostic direct route is explicit.
        const auto deglitched=pins.diagnosticDirectDeglitch?out.dacVolts:pins.deglitchedVolts;
        if(deglitched.known&&!std::isfinite(deglitched.value))throw std::invalid_argument("Invalid deglitch node");
        out.filterInputVolts={deglitched.value*(900.0/68900.0),deglitched.known};
        if(pins.filterGm.known)filter_.setTransconductance(pins.filterGm.value);
        if(!pins.filterGm.known||!out.filterInputVolts.known)filterKnown_=false;
        if(filterKnown_) {
            const auto taps=filter_.process(out.filterInputVolts.value);
            out.twoPoleVolts={taps.twoPole,true};out.fourPoleVolts={taps.fourPole,true};
        }
        auto reconstructed=pins.diagnosticDirectReconstruction?out.fourPoleVolts:pins.reconstructionVolts;
        if(reconstructed.known&&!std::isfinite(reconstructed.value))throw std::invalid_argument("Invalid reconstruction node");
        // C93=1uF couples S3528 FILT9 to R74=10k/R82=470. Nominal
        // highpass tau=(10k+470)*1uF; the VCA2 positive-input divider is
        // 470/(10k+470). Keep the laboratory current sign convention.
        const auto dc=coupling_.process(reconstructed);
        out.vcaInputVolts={(reconstructed.value-dc.value)*(470.0/10470.0),reconstructed.known&&dc.known};
        if(pins.vcaBranchAmps.known)vca_.setControlCurrent(pins.vcaBranchAmps.value);
        if(pins.vcaBranchAmps.known&&pins.vcaBranchAmps.value==0)out.branchCurrentAmps={0,true};
        else if(out.vcaInputVolts.known&&pins.vcaBranchAmps.known)
            out.branchCurrentAmps={vca_.outputCurrent(out.vcaInputVolts.value),true};
        if(!out.branchCurrentAmps.known)feedbackKnown_=false;
        if(feedbackKnown_)out.feedbackVolts={feedback_.process(out.branchCurrentAmps.value),true};
        return out;
    }
private:
    KnownLowpass reference_,dacFeedback_,coupling_;
    Ssm2045OpenLoop filter_;
    Ssm2045LinearVca vca_;
    Channel1Feedback feedback_;
    bool filterKnown_=false,feedbackKnown_=false;
};
class PlaybackAnalogBank {
public:
    explicit PlaybackAnalogBank(double rate):channels_{PlaybackAnalogChannel(rate),PlaybackAnalogChannel(rate),
        PlaybackAnalogChannel(rate),PlaybackAnalogChannel(rate),PlaybackAnalogChannel(rate),PlaybackAnalogChannel(rate),
        PlaybackAnalogChannel(rate),PlaybackAnalogChannel(rate)} {}
    void resetKnownZero() noexcept {for(auto& c:channels_)c.resetKnownZero();}
    std::array<PlaybackObservation,8> process(const PlaybackInputs& input,const std::array<PlaybackPins,8>& pins) {
        std::array<PlaybackObservation,8> out;
        for(unsigned ch=0;ch<8;++ch)out[ch]=channels_[ch].process(input.codes[ch],input.amplitudeCv[ch],pins[ch],ch==0);
        return out;
    }
private:
    std::array<PlaybackAnalogChannel,8> channels_;
};
}
