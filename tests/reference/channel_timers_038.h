#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include "channel_requests.h"
#include "channel_service.h"
#include "controller_timing.h"

namespace eii {
// 8254 binary mode-2 subset for the EII's fixed-high GATE wiring. No other
// modes, BCD, gate transitions, count/status latches or CPU readback modeled.
struct ReferenceRateCounter8254 {
    std::uint8_t access=0,mode=0,lowByte=0;
    bool configured=false,supported=false,bcd=false,writeHighNext=false;
    bool running=false,pendingLoad=false,nullCount=true,invalidCount=false;
    bool output=false,outputKnown=false;
    std::uint16_t reloadCode=0;
    std::uint32_t reload=0,count=0;
    std::uint64_t dataWrites=0,completeWrites=0,risingEdges=0,fallingEdges=0;

    void configure(std::uint8_t value) noexcept {
        access=(value>>4)&3;mode=(value>>1)&7;if(mode>=6)mode-=4;
        bcd=value&1;configured=true;supported=mode==2 && !bcd;
        writeHighNext=false;running=false;pendingLoad=false;nullCount=true;
        invalidCount=false;outputKnown=supported;output=true;
    }
    void write(std::uint8_t value) noexcept {
        ++dataWrites;
        if(!configured) return;
        std::uint16_t code;
        if(access==3) {
            if(!writeHighNext) {lowByte=value;writeHighNext=true;return;}
            code=std::uint16_t(lowByte)|(std::uint16_t(value)<<8);writeHighNext=false;
        } else code=access==1?std::uint16_t(value):std::uint16_t(value)<<8;
        reloadCode=code;reload=code?code:65536;nullCount=true;++completeWrites;
        invalidCount=reload==1;
        if(!supported || invalidCount) {running=false;pendingLoad=false;outputKnown=false;return;}
        pendingLoad=true;outputKnown=true;
        // If already running, transfer to CE waits for the current period's
        // reload edge. A partial LSB write above changes neither CR nor CE.
    }
    void advance(std::uint64_t clocks) noexcept {
        if(!supported || invalidCount) return;
        if(!running) {
            if(!pendingLoad || clocks==0) return;
            count=reload;pendingLoad=false;nullCount=false;running=true;output=true;
            --clocks; // first falling CLK loads CE without decrementing
        }
        // The source-grid scheduler advances exactly one clock. Keep that
        // common case separate from bulk division/modulo and reload logic.
        if(clocks==1) {
            if(count>1){--count;if(count==1){output=false;++fallingEdges;}}
            else {count=reload;pendingLoad=false;nullCount=false;output=true;++risingEdges;}
            return;
        }
        while(clocks) {
            if(!pendingLoad && count==reload && clocks>=reload) {
                const auto periods=clocks/reload;
                risingEdges+=periods;fallingEdges+=periods;clocks%=reload;
                if(!clocks) break;
            }
            if(count>1) {
                const auto step=std::min<std::uint64_t>(clocks,count-1);
                count-=std::uint32_t(step);clocks-=step;
                if(count==1) {output=false;++fallingEdges;}
            }
            if(clocks && count==1) {
                --clocks;count=reload;pendingLoad=false;nullCount=false;
                output=true;++risingEdges;
            }
        }
    }
};

class ReferenceChannelTimers {
public:
    using AudioSink=std::function<void(std::uint64_t,unsigned,KnownByte,std::uint8_t)>;
    void audioSink(AudioSink sink){
        flushEventTiming();audioSink_=std::move(sink);audioSent_.fill(false);
        if(audioSink_){audioChannels_=channels;audioSource_.fill(sourceClocks);
            for(unsigned i=0;i<8;++i)audioSchedule(i);
            audioScheduleNext();for(unsigned i=0;i<8;++i)notifyAudio(i);}
        if(eventTiming_)scheduleNextEvent();
    }
    // indices 0..7 = C0TIM..C7TIM; index 8 = SIOCK.
    std::array<ReferenceRateCounter8254,9> counters{};
    std::array<ChannelRequestTiming,8> channels{};
    ChannelServiceBus serviceBus;
    ControllerTiming controllerTiming;
    bool controllerTimingEnabled=false;
    std::uint64_t halfClocks=0;
    void enableControllerTiming() noexcept {
        controllerTimingEnabled=true;controllerTiming.reset(false);
        driveTimingSelection();
    }
    explicit ReferenceChannelTimers(std::uint8_t initialPhase=255):controllerTiming(initialPhase) {channels[0]=ChannelRequestTiming(true);}
    void initializeOutputPowerOn(std::uint32_t seed) {
        if(halfClocks || sourceClocks)throw std::logic_error("divider power-on must precede clocks");
        DigitalPowerOnBits bits(seed);
        for(unsigned i=0;i<9;++i){counters[i].outputKnown=true;counters[i].output=bits.next()&1;
            if(i<8)channels[i].initializePowerOn(bits,{true,counters[i].output});}
        driveControllerPins(controllerTiming.channel.value,controllerTiming.channel.known,
                            controllerTiming.acknowledgeEnable);
        if(eventTiming_)scheduleNextEvent();
    }
    // Input boundary from the not-yet-clocked controller: CHSL2..0,
    // their known-bit mask, and latched UCD08. No CPU OUT aliases invented.
    void driveControllerPins(std::uint8_t select,std::uint8_t known,LogicLevel ucd08) noexcept {
        serviceBus.drive(select,known,ucd08);settleServiceBus();
    }
    std::uint64_t sourceClocks=0,writes=0,unimplementedCommands=0;
    // Production event scheduler; default edge engine remains the verifier.
    // Clocked analog observation bypasses this path and sees every half-edge.
    void enableEventTiming(bool enable=true) noexcept {
        flushEventTiming();eventTiming_=enable;syncedSource_=sourceClocks;channelSource_.fill(sourceClocks);scheduleNextEvent();
    }
    bool eventTimingEnabled() const noexcept {return eventTiming_;}
    void enableControllerSpans(bool enable=true) noexcept {controllerSpans_=enable;}
    void enableProductionSpans(bool enable=true) noexcept {productionSpans_=enable;}
    void flushEventTiming() noexcept {
        if(!eventTiming_ || syncedSource_==sourceClocks)return;
        const auto clocks=sourceClocks-syncedSource_;
        bool changed=false;
        for(unsigned i=0;i<9;++i) {
            auto& c=counters[i];
            if(i<8) {
                const auto before=channels[i].request;
                channels[i].advanceSourceClocks(sourceClocks-channelSource_[i],channelSource_[i]+1);
                channelSource_[i]=sourceClocks;
                const auto after=channels[i].request;
                changed|=before.known!=after.known || (after.known && before.high!=after.high);
            }
            const bool known=c.outputKnown,high=c.output;c.advance(clocks);
            if(i<8 && (known!=c.outputKnown || high!=c.output)){
                channels[i].timer({c.outputKnown,c.output});if(audioSink_)audioTimer(i,{c.outputKnown,c.output});}
        }
        syncedSource_=sourceClocks;
        if(changed)settleServiceBus();
        ++runtimeEvents;scheduleNextEvent();
    }
    std::uint64_t runtimeEvents=0;
    bool write(std::uint8_t port,std::uint8_t value) noexcept {
        const auto base=port&0xf8;
        if(base!=0x08 && base!=0x10 && base!=0x18) return false;
        // An OUT changes the future deadline and must see the preceding time.
        flushEventTiming();
        struct Reschedule {ReferenceChannelTimers* self;~Reschedule(){if(self->eventTiming_)self->scheduleNextEvent();}} reschedule{this};
        ++writes;
        const unsigned chip=base==0x18?0:(base==0x10?1:2);
        const unsigned reg=port&3;
        if(reg!=3) {
            const auto index=chip*3+reg;auto& c=counters[index];c.write(value);
            if(index<8) channels[index].timer({c.outputKnown,c.output});
            if(index<8&&audioSink_)audioTimer(index,{c.outputKnown,c.output});
            return true;
        }
        const unsigned selected=value>>6;
        if(selected==3 || ((value>>4)&3)==0) {
            // Read-back/count-latch commands do not reprogram the mode.
            // Their unread latches are absent in this board-specific subset.
            ++unimplementedCommands;return true;
        }
        const auto index=chip*3+selected;
        auto& counter=counters[index];counter.configure(value);
        if(index<8) channels[index].timer({counter.outputKnown,counter.output});
        if(index<8&&audioSink_)audioTimer(index,{counter.outputKnown,counter.output});
        if(!counter.supported) ++unimplementedCommands;
        return true;
    }
    void advanceSourceClocks(std::uint64_t clocks) noexcept {
        // Two half-edges per complete source period, preserving the current
        // clock level when this helper is used between CPU frames.
        while(clocks--) advanceHalfClocks(2);
    }
    void advanceCpuTick() noexcept {
        if(controllerTimingEnabled && eventTiming_ && controllerSpans_ && controllerTiming.normalRuntimeEnabled()) {
            controllerTiming.beginCpuInterval();controllerTiming.cpuClock();
            // Both engines certify their own quiet periods. Production never
            // enters the reference voice interpreter or skips CPU boundaries.
            if(controllerTiming.productionEngineEnabled() && !productionSpans_)
                advanceHalfClocksKernel(5,[](std::uint64_t) noexcept {});
            else advanceRuntimeHalfClocks(5);
            controllerTiming.endCpuInterval();
        } else {
            if(controllerTimingEnabled)controllerTiming.cpuClock();
            advanceHalfClocksKernel(5,[](std::uint64_t) noexcept {});
        }
        if(audioNext_<=sourceClocks&&audioSink_)advanceAudio();
    }
    template<class Observer> void advanceCpuTickObserved(Observer&& beforeHalfEdge) {
        // An observer can inspect phi1/phi2 and counter state on every edge.
        // Flush then temporarily use the complete edge scheduler.
        flushEventTiming();
        const bool events=eventTiming_;eventTiming_=false;
        if(controllerTimingEnabled)controllerTiming.cpuClock();
        advanceHalfClocksObserved(5,beforeHalfEdge);
        eventTiming_=events;syncedSource_=sourceClocks;channelSource_.fill(sourceClocks);if(events)scheduleNextEvent();
    }
    void advanceHalfClocks(unsigned edges) noexcept {
        advanceHalfClocksObserved(edges,[](std::uint64_t) noexcept {});
    }
    // Observer integrates the preceding50ns interval using pre-edge states.
    // It cannot alter edge ordering: controller rise and timer/divider fall
    // still settle below, after the observer sees the complete old interval.
    template<class Observer> void advanceHalfClocksObserved(unsigned edges,Observer&& beforeHalfEdge) {
        flushEventTiming();const bool events=eventTiming_;eventTiming_=false;
        advanceHalfClocksKernel(edges,beforeHalfEdge);
        if(audioSink_)advanceAudio();
        eventTiming_=events;syncedSource_=sourceClocks;channelSource_.fill(sourceClocks);if(events)scheduleNextEvent();
    }
private:
    AudioSink audioSink_;
    std::array<KnownByte,8> audioCode_{};std::array<std::uint8_t,8> audioPhase_{};std::array<bool,8> audioSent_{};
    // Independent, read-only divider histories: observing an analog phase
    // must not force all nine digital counters/controller to materialize.
    // OUT transitions are delivered by the unchanged digital scheduler.
    std::array<ChannelRequestTiming,8> audioChannels_{};
    std::array<std::uint64_t,8> audioSource_{},audioDue_{};
    std::uint64_t audioNext_=UINT64_MAX;
    void audioSchedule(unsigned i)noexcept{
        const auto d=audioChannels_[i].clocksUntilPhaseEvent();
        audioDue_[i]=d==UINT64_MAX?UINT64_MAX:audioSource_[i]+d;
    }
    void audioScheduleNext()noexcept{audioNext_=*std::min_element(audioDue_.begin(),audioDue_.end());}
    void emitAudio(unsigned i,std::uint64_t edge,KnownByte code)noexcept{
        const auto a=audioChannels_[i].phi1(),b=audioChannels_[i].phi2();
        const auto phase=std::uint8_t((a.high?1:0)|(b.high?2:0)|(a.known?4:0)|(b.known?8:0));
        if(!audioSent_[i]||code!=audioCode_[i]||phase!=audioPhase_[i]){
            audioSent_[i]=true;audioCode_[i]=code;audioPhase_[i]=phase;audioSink_(edge,i,code,phase);
        }
    }
    void advanceAudio(std::uint64_t until)noexcept{
        while(audioNext_<=until){const auto clock=audioNext_;
            for(unsigned i=0;i<8;++i)if(audioDue_[i]==clock){
                audioChannels_[i].advanceSourceClocks(clock-audioSource_[i],audioSource_[i]+1);audioSource_[i]=clock;
                emitAudio(i,clock*2,audioCode_[i]);audioSchedule(i);
            }
            audioScheduleNext();
        }
    }
    void advanceAudio()noexcept{advanceAudio(sourceClocks);}
    void audioTimer(unsigned i,LogicLevel timer)noexcept{
        // Same falling edge first clocks the divider, then settles OUT/reset.
        // Publish only its final level, not a zero-duration intermediate phase.
        advanceAudio(sourceClocks-(sourceClocks&&!sourceHigh_));auto& c=audioChannels_[i];
        c.advanceSourceClocks(sourceClocks-audioSource_[i],audioSource_[i]+1);audioSource_[i]=sourceClocks;c.timer(timer);
        emitAudio(i,halfClocks,audioCode_[i]);audioSchedule(i);audioScheduleNext();
    }
    void notifyAudio(unsigned i)noexcept{
        advanceAudio();emitAudio(i,halfClocks,controllerTiming.channelDacCode(i));
    }
    void advanceRuntimeHalfClocks(unsigned edges) noexcept {
        while(edges) {
            // Only whole source periods, never crossing a timer/request
            // deadline or a CPU boundary. No source-level observer is active.
            if(!sourceHigh_ && edges>=2) {
                const auto distance=nextEvent_>sourceClocks?nextEvent_-sourceClocks:0;
                const unsigned limit=unsigned(std::min<std::uint64_t>(edges/2,distance));
                // The production edge engine rejects any unknown request,
                // including unselected channels. Preserve that failure policy
                // even though no request latch is captured inside this span.
                bool requestsKnown=true;
                if(controllerTiming.productionEngineEnabled()) {
                    if(!controllerTiming.productionQuietCandidate())requestsKnown=false;
                    else for(const auto& c:channels)requestsKnown&=c.request.known;
                }
                if(const auto n=requestsKnown?controllerTiming.advanceQuietClocks(limit):0) {
                    edges-=n*2;halfClocks+=n*2;sourceClocks+=n;
                    driveTimingSelection();
                    if(sourceClocks>=nextEvent_)flushEventTiming();
                    continue;
                }
            }
            --edges;++halfClocks;sourceHigh_=!sourceHigh_;
            if(sourceHigh_) {
                std::array<LogicLevel,8> requests;
                for(unsigned i=0;i<8;++i)requests[i]=channels[i].request;
                controllerTiming.risingClock(requests);driveTimingSelection();
            } else fallingClock();
        }
    }
    template<class Observer> void advanceHalfClocksKernel(unsigned edges,Observer&& beforeHalfEdge) {
        while(edges--) {
            beforeHalfEdge(halfClocks+1);
            ++halfClocks;sourceHigh_=!sourceHigh_;
            if(sourceHigh_) {
                if(controllerTimingEnabled) {
                    std::array<LogicLevel,8> requests;
                    for(unsigned i=0;i<8;++i)requests[i]=channels[i].request;
                    controllerTiming.risingClock(requests);
                    driveTimingSelection();
                }
            } else fallingClock();
        }
    }

private:
    bool eventTiming_=false,controllerSpans_=true,productionSpans_=true;
    std::uint64_t syncedSource_=0,nextEvent_=0;
    std::array<std::uint64_t,8> channelSource_{};
    void scheduleNextEvent() noexcept {
        std::uint64_t distance=UINT64_MAX;
        for(unsigned i=0;i<9;++i) {
            const auto& c=counters[i];
            if(c.supported && !c.invalidCount) {
                if(c.running)distance=std::min(distance,std::uint64_t(c.count>1?c.count-1:1));
                else if(c.pendingLoad)distance=1;
            }
            if(i<8)distance=std::min(distance,channels[i].clocksUntilRequestEvent());
        }
        nextEvent_=distance==UINT64_MAX?UINT64_MAX:sourceClocks+distance;
    }
    void fallingClock() noexcept {
        ++sourceClocks;
        if(controllerTimingEnabled)controllerTiming.fallingClock();
        if(eventTiming_) {
            if(sourceClocks>=nextEvent_)flushEventTiming();
            return;
        }
        // Shared falling CLK reaches the divider before the timer's
        // newly generated OUT transition. This is an explicit ideal
        // propagation order, not calibrated chip delay timing.
        bool changed=false;
        for(unsigned i=0;i<9;++i) {
            auto& c=counters[i];
            if(i<8) {
                const auto before=channels[i].request;
                channels[i].fallingSourceClock(sourceClocks);
                const auto after=channels[i].request;
                changed|=before.known!=after.known || (after.known && before.high!=after.high);
            }
            const bool known=c.outputKnown,high=c.output;
            c.advance(1);
            if(i<8 && (known!=c.outputKnown || high!=c.output)){
                channels[i].timer({c.outputKnown,c.output});if(audioSink_)audioTimer(i,{c.outputKnown,c.output});}
        }
        if(changed) settleServiceBus();
    }
    void driveTimingSelection() noexcept {
        const auto ch=controllerTiming.selectedChannel();
        const auto enable=controllerTiming.hasUpperProm()
            ?controllerTiming.selectedAcknowledge()
            :serviceBus.acknowledgeEnable;
        if((ch.value&7)!=serviceBus.selection || (ch.known&7)!=serviceBus.selectionKnown ||
           enable.known!=serviceBus.acknowledgeEnable.known ||
           (enable.known && enable.high!=serviceBus.acknowledgeEnable.high)){
            const auto old=serviceBus.acknowledge;
            driveControllerPins(ch.value,ch.known,enable);
            if(audioSink_)for(unsigned i=0;i<8;++i){const auto next=serviceBus.acknowledge[i];
                if(!old[i].known||!next.known||(!old[i].high&&next.high))notifyAudio(i);}
        }
    }
    void settleServiceBus() noexcept {
        std::array<LogicLevel,8> inputs;
        for(unsigned i=0;i<8;++i) {
            // Settle the elapsed interval with the OLD clear level. Applying a
            // new ACK retroactively would corrupt unknown-timer request state.
            if(eventTiming_ && !channels[i].acknowledgeMatches(serviceBus.acknowledge[i])) {
                channels[i].advanceSourceClocks(sourceClocks-channelSource_[i],channelSource_[i]+1);
                channelSource_[i]=sourceClocks;
            }
            channels[i].acknowledge(serviceBus.acknowledge[i]);
            if(eventTiming_ && channels[i].unknownTimerNeedsClock())nextEvent_=std::min(nextEvent_,sourceClocks+1);
            inputs[i]=channels[i].request;
        }
        serviceBus.requests(inputs);
    }
    bool sourceHigh_=false;
};
}
