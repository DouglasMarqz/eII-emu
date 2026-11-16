#pragma once
#include "sample_input_analog.h"
#include "vst3/rt_primitives.h"
#include <cmath>

namespace eii::vst {
// Host callback -> machine owner. Timestamped samples keep gaps/epochs explicit;
// no wall clock, capture-device buffer or sample-rate-dependent machine clock.
class HostSampleInput final:public analog::SampleInputSource {
    struct Frame {std::uint64_t epoch=0,pos=0;float value=0;};
    std::unique_ptr<SpscQueue<Frame,1u<<18>> queue_=std::make_unique<SpscQueue<Frame,1u<<18>>();
    Frame previous_{};bool have_=false;
    std::uint64_t epoch_=0,anchor_=0,offset_=0;double rate_=48000,gain_=1;
    void advance(double position)noexcept {
        while(const auto* f=queue_->front()){
            if(f->epoch<epoch_){queue_->pop();continue;}
            if(f->epoch!=epoch_||double(f->pos)>position)break;
            previous_=*f;have_=true;queue_->pop();
        }
    }
public:
    std::atomic<bool> peakDiagnostics{false};
    std::atomic<float> peak{0};
    std::atomic<std::uint64_t> frames{0},drops{0},missing{0};
    // Producer only. Host silence flags are per channel, null means silence.
    void push(std::uint64_t epoch,std::uint64_t pos,const float* left,const float* right,int n,bool stereo,bool silentLeft=false,bool silentRight=false)noexcept {
        const bool trackPeak=peakDiagnostics.load(std::memory_order_relaxed);
        float blockPeak=0;
        for(int i=0;i<n;++i){
            double l=left&&!silentLeft?left[i]:0,r=right&&!silentRight?right[i]:0;
            if(!std::isfinite(l))l=0;
            if(!std::isfinite(r))r=0;
            const float value=float(std::clamp(stereo?(l+r)*.5:l,-1.0,1.0));
            if(trackPeak)blockPeak=std::max(blockPeak,std::abs(value));
            if(!queue_->push({epoch,pos+std::uint64_t(i),value}))drops.fetch_add(1,std::memory_order_relaxed);
        }
        if(trackPeak)peak.store(blockPeak,std::memory_order_relaxed);
        frames.fetch_add(std::uint64_t(n),std::memory_order_relaxed);
    }
    // Owner only, before advancing the machine. A one-host-sample causal delay
    // supplies the interpolation's right endpoint even at callback boundaries.
    void bind(std::uint64_t epoch,double rate,std::uint64_t anchor,std::uint64_t offset,double gain)noexcept {
        if(epoch!=epoch_){have_=false;epoch_=epoch;}rate_=rate;anchor_=anchor;offset_=offset;gain_=gain;
    }
    double volts(std::uint64_t tick,double frac=0)noexcept override {
        const double p=(double(tick)+frac+double(offset_)-double(anchor_))*rate_/4000000.0-1;
        if(p<0)return 0;
        const auto i=std::uint64_t(p);advance(double(i));
        if(!have_||previous_.epoch!=epoch_||previous_.pos!=i){missing.fetch_add(1,std::memory_order_relaxed);return 0;}
        const double t=p-double(i);if(t<1e-12)return previous_.value*gain_;
        const auto* next=queue_->front();
        if(!next||next->epoch!=epoch_||next->pos!=i+1){missing.fetch_add(1,std::memory_order_relaxed);return 0;}
        return (previous_.value+(next->value-previous_.value)*t)*gain_;
    }
    void idle(std::uint64_t tick)noexcept override {
        advance((double(tick)+double(offset_)-double(anchor_))*rate_/4000000.0-1);
    }
    void discard(std::uint64_t end)noexcept {advance(double(end));}
};
}
