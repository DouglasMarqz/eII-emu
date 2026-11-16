#pragma once
#include <vector>
#include "dram_cpu.h"
#include "sample_dac.h"

namespace eii {
// Retain upper-PROM feedback, request latch, completion FF and DRAM history
// together. Bitwise consensus is an observation, never the feedback state.
// Unknown external requests branch at sampling; this overapproximates the
// shared channel network without assuming quiet channels or a startup phase.
class ControllerUpperPaths {
public:
    struct Branch {
        KnownByte upper{},pipeline{};
        LogicLevel request{true,false},ack{},delayed9{};
        CpuRequestHandshake cpu;
        DramCpuPort dram;
        SampleDacRegisters dac;
        bool sameState(const Branch& b) const noexcept {
            return upper==b.upper && pipeline==b.pipeline && request==b.request &&
                ack==b.ack && delayed9==b.delayed9 && cpu==b.cpu && dram.sameState(b.dram) && dac.sameClockState(b.dac);
        }
    };
    std::vector<Branch> branches;
    void initialize(KnownWord control,KnownByte pipeline,LogicLevel request,
                    const CpuRequestHandshake& cpu,const DramCpuPort& dram) {
        branches.clear();
        for(unsigned f=0;f<4;++f)if(!(((f<<6)^(control.value>>8))&(control.known>>8)&192)) {
            Branch b;b.upper={std::uint8_t((control.value>>8 &63)|(f<<6)),std::uint8_t((control.known>>8)|192)};
            b.pipeline=pipeline;b.request=request;b.cpu=cpu;b.dram=dram;branches.push_back(b);
        }
    }
    void reset() noexcept {
        for(auto& b:branches){b.request={true,false};b.dac.drive({0,7},b.ack,b.dram.channelLatch);}
    }
    void driveCpu(LogicLevel inactive) noexcept {for(auto& b:branches)b.cpu.drive(inactive);}
    void cpuClock() noexcept {for(auto& b:branches)b.cpu.cpuClock();}
    void driveDram(bool active,std::uint32_t address,KnownByte data,bool read) noexcept {
        for(auto& b:branches)b.dram.cpuBus(active,address,data,read);
    }
    bool mayCaptureAddress() const noexcept {
        for(const auto& b:branches)if(b.dram.aluAddress.mayCapture())return true;
        return false;
    }
    void falling(KnownWord lower,DramBackingView memory) noexcept {
        for(auto& b:branches)b.dram.falling(word(b,lower),b.delayed9,memory);
    }
    void rising(const PromConsensus& prom,unsigned phase,unsigned channel,
                LogicLevel capture,LogicLevel input,bool resetHeld,LogicLevel readInactive,
                KnownWord lower,Known24 sum,DramBackingView memory) {
        // A single concrete address history needs neither enumeration nor
        // copying its DRAM/DAC state into a replacement vector. Unknown
        // requests/pipeline/capture still use the original branching path.
#ifndef EII_REFERENCE_UPPER_PATH
        if(branches.size()==1 && (branches[0].pipeline.known&3)==3 &&
           branches[0].request.known && (resetHeld||capture.known)) {
            auto& b=branches[0];
            const auto oldUpper=b.upper;
            const auto oldRequest=b.request;
            const auto oldCpuRequest=b.cpu.requestToPipeline();
            const unsigned q=b.pipeline.value&3;
            b.dram.rising(word(b,lower),sum,memory);
            const unsigned a=((oldUpper.value&192)<<1)|((phase&7)<<4)|
                ((q&1)<<3)|((q&2)<<1)|((channel&8)?2:0)|unsigned(oldRequest.high);
            b.upper=prom.lookup(a,511);
            b.ack={bool(oldUpper.known&1),bool(oldUpper.value&1)};
            b.delayed9={bool(oldUpper.known&2),bool(oldUpper.value&2)};
            b.pipeline={std::uint8_t(unsigned(oldCpuRequest.high)|(unsigned(readInactive.high)<<1)),
                std::uint8_t(unsigned(oldCpuRequest.known)|(unsigned(readInactive.known)<<1))};
            b.cpu.completionClock({true,bool(b.upper.value&2)});
            b.dram.settle(word(b,lower),memory);
            const unsigned nextChannel=resetHeld?0:phase==15?((channel&8)?0:channel+1):channel;
            b.dac.drive({std::uint8_t(nextChannel),7},b.ack,b.dram.channelLatch);
            b.request=resetHeld?LogicLevel{true,false}:capture.high?input:oldRequest;
            return;
        }
#endif
        next_.clear();
        for(const auto& old:branches) {
            auto base=old;
            base.dram.rising(word(old,lower),sum,memory);
            // Only bits14/15 feed back to the upper PROM. All remaining
            // address inputs below are enumerated jointly with their history.
            for(unsigned q=0;q<4;++q)if(!((q^old.pipeline.value)&old.pipeline.known&3))
            for(unsigned r=0;r<2;++r)if(!old.request.known || r==unsigned(old.request.high)) {
                auto b=base;
                const unsigned a=((old.upper.value&192)<<1)|((phase&7)<<4)|
                    ((q&1)<<3)|((q&2)<<1)|((channel&8)?2:0)|r;
                b.upper=prom.lookup(a,511);
                b.ack={bool(old.upper.known&1),bool(old.upper.value&1)};
                b.delayed9={bool(old.upper.known&2),bool(old.upper.value&2)};
                const auto request=old.cpu.requestToPipeline();
                b.pipeline={std::uint8_t(unsigned(request.high)|(unsigned(readInactive.high)<<1)),
                    std::uint8_t(unsigned(request.known)|(unsigned(readInactive.known)<<1))};
                b.cpu.completionClock({true,bool(b.upper.value&2)});
                b.dram.settle(word(b,lower),memory);
                const unsigned nextChannel=resetHeld?0:phase==15?((channel&8)?0:channel+1):channel;
                b.dac.drive({std::uint8_t(nextChannel),7},b.ack,b.dram.channelLatch);
                if(resetHeld){b.request={true,false};insert(b);}
                else {
                    if(!capture.known || !capture.high){b.request={true,bool(r)};insert(b);}
                    if(!capture.known || capture.high){b.request=input;insert(b);}
                }
            }
        }
        branches.swap(next_);
    }
    KnownByte upper() const noexcept {return consensus([](const Branch& b){return b.upper;});}
    KnownByte pipeline() const noexcept {return consensus([](const Branch& b){return b.pipeline;});}
    LogicLevel request() const noexcept {return level([](const Branch& b){return b.request;});}
    LogicLevel ack() const noexcept {return level([](const Branch& b){return b.ack;});}
    LogicLevel wait() const noexcept {return level([](const Branch& b){return b.cpu.waitInactive();});}
private:
    std::vector<Branch> next_;
    static KnownWord word(const Branch& b,KnownWord lower) noexcept {
        return {std::uint16_t((unsigned(b.upper.value)<<8)|(lower.value&255)),
                std::uint16_t((unsigned(b.upper.known)<<8)|(lower.known&255))};
    }
    void insert(const Branch& b) {
        // DAC codes do not feed this digital controller. Merge their
        // observable consensus when future controller/clock state agrees;
        // retain the per-history IC40 data and ACK clocks themselves.
        for(auto& x:next_)if(x.sameState(b)){x.dac.mergeOutputs(b.dac);return;}
        next_.push_back(b);
    }
    template<class F> KnownByte consensus(F f) const noexcept {
        auto out=f(branches[0]);for(unsigned i=1;i<branches.size();++i)out=mergeDramByte(out,f(branches[i]));return out;
    }
    template<class F> LogicLevel level(F f) const noexcept {
        auto out=f(branches[0]);for(unsigned i=1;i<branches.size();++i){auto x=f(branches[i]);out.known&=x.known&&x.high==out.high;}return out;
    }
};
}
