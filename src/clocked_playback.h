#pragma once
#include <algorithm>
#include <functional>
#include "playback_analog.h"
#include "deglitch.h"
#include "s3528.h"

namespace eii::analog {
struct ClockedPlaybackOptions {
    unsigned outputRate=48000;
    S3528Profile reconstructionProfile=S3528Profile::Ami1993TypicalFit;
    std::uint8_t channels=255;
    std::array<KnownSignal,8> filterGm{},vcaBranchAmps{};
    bool channel0PlaybackConfirmed=false;
    bool zeroCapacitorsForExperiment=false;
    bool alignDividersForExperiment=false;
};
struct PlaybackFrame {
    std::uint64_t index=0; // first frame index1, end time=index/outputRate
    std::array<KnownSignal,8> meanVolts{};
};
// Exact rational area accumulation of a piecewise-constant source-grid
// waveform. Box integration is NOT a production-quality anti-alias filter.
class PlaybackFrameIntegrator {
public:
    static constexpr unsigned sourceRate=20000000;
    explicit PlaybackFrameIntegrator(unsigned outputRate):rate_(outputRate) {
        if(!rate_||rate_>sourceRate)throw std::invalid_argument("Invalid playback frame rate");
        known_.fill(true);
    }
    template<class Sink> void interval(const std::array<KnownSignal,8>& volts,Sink&& sink) {
        unsigned remaining=rate_;
        while(remaining) {
            const unsigned take=std::min(remaining,sourceRate-phase_);
            for(unsigned ch=0;ch<8;++ch) {
                if(volts[ch].known) {
                    if(!std::isfinite(volts[ch].value))throw std::invalid_argument("Nonfinite playback frame input");
                    sums_[ch]+=volts[ch].value*take;
                } else known_[ch]=false;
            }
            phase_+=take;remaining-=take;
            if(phase_==sourceRate) {
                PlaybackFrame frame;frame.index=++frames_;
                for(unsigned ch=0;ch<8;++ch)frame.meanVolts[ch]={known_[ch]?sums_[ch]/sourceRate:0,known_[ch]};
                phase_=0;sums_={};known_.fill(true);sink(frame);
            }
        }
    }
    std::uint64_t frames() const noexcept {return frames_;}
    unsigned pendingPhase() const noexcept {return phase_;}
private:
    unsigned rate_,phase_=0;
    std::uint64_t frames_=0;
    std::array<double,8> sums_{};
    std::array<bool,8> known_{};
};
struct ClockedPlaybackObservation {
    PlaybackObservation analog{};
    KnownSignal deglitchedVolts{},reconstructedVolts{};
    bool reconstructionPhaseKnown=false;
};
// Opt-in nominal experiment. All8 channels advance on actual20MHz half-edge
// intervals. No invented FC/Q laws, startup code, sample data or channel0 MODE.
class ClockedPlayback {
public:
    using Sink=std::function<void(const PlaybackFrame&)>;
    static constexpr double rate=PlaybackFrameIntegrator::sourceRate;
    explicit ClockedPlayback(ClockedPlaybackOptions options,std::uint64_t originHalfEdge,Sink sink={})
        :options_(options),origin_(originHalfEdge),frames_(options.outputRate),sink_(std::move(sink)) {
        for(unsigned ch=0;ch<8;++ch) {
            reconstruction_[ch]=S3528NominalResponse(options.reconstructionProfile);
            if(options.filterGm[ch].known) {Ssm2045OpenLoop check(rate);check.setTransconductance(options.filterGm[ch].value);}
            if(options.vcaBranchAmps[ch].known) {Ssm2045LinearVca check;check.setControlCurrent(options.vcaBranchAmps[ch].value);}
            if(options.zeroCapacitorsForExperiment) {
                analog_[ch].resetKnownZero();deglitch_[ch].resetKnownZero();reconstruction_[ch].resetKnownZero();dac_[ch]={0,true};
            }
        }
    }
    // The Machine passes absolute half-edge timestamps. Reprogramming a
    // divisor invalidates phase; explicit initial alignment happens only once.
    void advance(std::uint64_t endHalfEdge,const PlaybackInputs& input,
                 const std::array<LogicLevel,8>& phi1,const std::array<LogicLevel,8>& phi2,
                 const std::array<KnownByte,8>& program) {
        if(endHalfEdge!=origin_+intervals_+1)throw std::invalid_argument("Nonconsecutive playback half-edge");
        std::array<KnownSignal,8> output{};
        for(unsigned ch=0;ch<8;++ch)if(options_.channels&(1u<<ch)) {
            if(!intervals_&&options_.alignDividersForExperiment&&(program[ch].known&63)==63)
                clocks_[ch].alignForExperiment(program[ch]);
            if(!clocks_[ch].observeCode(program[ch]))reconstruction_[ch].invalidate();
            auto& o=observed_[ch];PlaybackPins pins;
            pins.filterGm=options_.filterGm[ch];pins.vcaBranchAmps=options_.vcaBranchAmps[ch];
            pins.channel0PlaybackConfirmed=options_.channel0PlaybackConfirmed;
            // DAC solution from the preceding grid sample is held throughout
            // this interval. This explicit50ns ZOH is numerical approximation.
            o.deglitchedVolts=deglitch_[ch].advance(1/rate,dac_[ch],phi1[ch],phi2[ch]);
            // A linear op-amp result outside the physical +/-15V supplies
            // cannot be treated as valid sound. No invented clipping law.
            if(o.deglitchedVolts.known&&std::abs(o.deglitchedVolts.value)>=15)rangeExceeded_|=std::uint8_t(1u<<ch);
            pins.deglitchedVolts=(rangeExceeded_&(1u<<ch))?KnownSignal{}:o.deglitchedVolts;
            pins.reconstructionVolts=reconstruction_[ch].output();
            o.analog=analog_[ch].process(input.codes[ch],input.amplitudeCv[ch],pins,ch==0);
            dac_[ch]=o.analog.dacVolts;
            // 20MHz integration grid; every second interval is one half-edge
            // of the5MHz S3528 master. Unknown phase poisons reconstruction.
            if((endHalfEdge&1)==0) {
                const auto sample=clocks_[ch].masterHalfEdge(program[ch]);
                if(!sample.known)reconstruction_[ch].invalidate();
                else if(sample.high) {reconstruction_[ch].sampleAtTwiceFilterClock(o.analog.fourPoleVolts);++samples_[ch];}
            }
            o.reconstructionPhaseKnown=clocks_[ch].phaseKnown();
            o.reconstructedVolts=reconstruction_[ch].output();
            output[ch]=o.analog.feedbackVolts;
        }
        ++intervals_;
        frames_.interval(output,[&](const PlaybackFrame& frame){if(sink_)sink_(frame);});
    }
    const ClockedPlaybackObservation& channel(unsigned ch) const {return observed_.at(ch);}
    std::uint64_t intervals() const noexcept {return intervals_;}
    std::uint64_t frames() const noexcept {return frames_.frames();}
    std::uint64_t reconstructionSamples(unsigned ch) const {return samples_.at(ch);}
    std::uint8_t rangeExceededMask() const noexcept {return rangeExceeded_;}
    std::uint64_t originHalfEdge() const noexcept {return origin_;}
    const ClockedPlaybackOptions& options() const noexcept {return options_;}
private:
    ClockedPlaybackOptions options_;
    std::uint64_t origin_,intervals_=0;
    PlaybackFrameIntegrator frames_;
    Sink sink_;
    std::array<PlaybackAnalogChannel,8> analog_{PlaybackAnalogChannel(rate),PlaybackAnalogChannel(rate),PlaybackAnalogChannel(rate),PlaybackAnalogChannel(rate),PlaybackAnalogChannel(rate),PlaybackAnalogChannel(rate),PlaybackAnalogChannel(rate),PlaybackAnalogChannel(rate)};
    std::array<Deglitch3202,8> deglitch_{};
    std::array<S3528Clock,8> clocks_{};
    std::array<S3528NominalResponse,8> reconstruction_{};
    std::array<KnownSignal,8> dac_{};
    std::array<ClockedPlaybackObservation,8> observed_{};
    std::array<std::uint64_t,8> samples_{};
    std::uint8_t rangeExceeded_=0;
};
}
