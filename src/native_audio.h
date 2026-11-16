#pragma once
#include "am6072.h"
#include <bit>
#include "filter_calibration.h"
#include "level_model.h"
#include "channel_requests.h"
#include <algorithm>

namespace eii::analog {
// 1/120MHz units: hardware half-edge6, PCM frame2500,96k step1250,
// 192k step625. Digital edges keep their original integer timestamps.
inline constexpr unsigned nativeTimeRate=120000000,nativeFrameTime=2500;
inline std::uint8_t nativePhases(LogicLevel a,LogicLevel b)noexcept{
    return std::uint8_t((a.high?1:0)|(b.high?2:0)|(a.known?4:0)|(b.known?8:0));
}
struct NativePins{
    std::array<KnownByte,8> dac{},sw{};
    std::array<std::array<KnownByte,8>,3> cv{};
    std::array<std::uint8_t,8> phases{};
    bool channel0=false,monitorSampling=false;KnownSignal monitorIint{};
};
struct NativeEvent{
    enum Kind:std::uint8_t{Pins,Amp,Fc,Q,Switch,Mode,MonitorIint};
    std::uint16_t offset;
    std::uint8_t kind,channel,value,known,phases;
    std::uint32_t analogBits=0; // float IINT payload, only MonitorIint events
};
inline void applyNativeEvent(NativePins& s,const NativeEvent& e,bool includeMonitor=true)noexcept{
    const unsigned ch=e.channel&7;
    if(e.kind==NativeEvent::Pins){s.dac[ch]={e.value,e.known};s.phases[ch]=e.phases;}
    else if(e.kind>=NativeEvent::Amp&&e.kind<=NativeEvent::Q)s.cv[e.kind-NativeEvent::Amp][ch]={e.value,e.known};
    else if(e.kind==NativeEvent::Switch)s.sw[ch]={e.value,e.known};
    else if(e.kind==NativeEvent::Mode)s.channel0=e.known==255&&e.value!=0;
    else if(e.kind==NativeEvent::MonitorIint&&includeMonitor){s.monitorSampling=e.value!=0;s.monitorIint={std::bit_cast<float>(e.analogBits),e.known==255};}
}
struct NativeFrame{
    static constexpr unsigned capacity=192;
    NativePins initial{};
    std::array<NativeEvent,capacity> events;
    std::uint64_t start=0;
    std::uint16_t count=0;
    bool enabled=false,valid=true;
    // Only [0,count) is live. Do not zero/copy192 unused event slots for every
    // reference-path frame, or for each partially filled DSP transport block.
    NativeFrame()noexcept{}
    NativeFrame(const NativeFrame& other)noexcept{*this=other;}
    NativeFrame& operator=(const NativeFrame& other)noexcept{
        if(this==&other)return *this;
        initial=other.initial;start=other.start;enabled=other.enabled;valid=other.valid;
        count=enabled?other.count:0;std::copy_n(other.events.begin(),count,events.begin());
        return *this;
    }
};
// Fixed storage. Overflow mutes this frame, next frame has a complete snapshot.
class NativeCollector{
public:
    explicit NativeCollector(std::uint64_t halfEdge):start_(halfEdge*6),lastTime_(start_){frame_.enabled=true;frame_.start=start_;}
    template<class Sink>void advance(std::uint64_t halfEdge,Sink&& sink){
        const auto t=halfEdge*6;if(t<lastTime_){frame_.valid=false;++orderingErrors_;return;}
        advanceTime(t,sink);lastTime_=t;
    }
    template<class Sink>void event(std::uint64_t halfEdge,NativeEvent e,Sink&& sink){
        const auto t=halfEdge*6;if(t<lastTime_){frame_.valid=false;++orderingErrors_;return;}
        advanceTime(t,sink);lastTime_=t;e.offset=std::uint16_t(t-start_);
        if(frame_.count<frame_.capacity)frame_.events[frame_.count++]=e;
        else{frame_.valid=false;++overflows_;}
        applyNativeEvent(state_,e);
    }
    // Between frame boundaries advance() only records lastTime_; the next
    // event/boundary call checks ordering against the same later time.
    bool due(std::uint64_t halfEdge)const noexcept{const auto t=halfEdge*6;return t>=start_+nativeFrameTime||t<lastTime_;}
    std::uint64_t overflows()const noexcept{return overflows_;}
    std::uint64_t orderingErrors()const noexcept{return orderingErrors_;}
private:
    template<class Sink>void advanceTime(std::uint64_t t,Sink& sink){
        while(t>=start_+nativeFrameTime){sink(frame_);start_+=nativeFrameTime;
            frame_.start=start_;frame_.count=0;frame_.initial=state_;frame_.valid=true;}
    }
    NativeFrame frame_{};NativePins state_{};
    std::uint64_t start_=0,lastTime_=0,overflows_=0,orderingErrors_=0;
};

// Linear RC + ideal13202/TL084 between digital edges. Analytical charge
// evolution and output area; optional TL084 output swing (rails); noise,
// injection and finite op-amp BW omitted.
class NativeDacCoefficients{
public:
    static constexpr double tauIv=2490*220e-12,tau1=(150+200.0*10000/10200)*470e-12,tau2=150*470e-12;
    struct Fixed{std::array<double,1251> iv{},p1{},p2{},mean2{},sampleRef{},sampleMinus{};};
    NativeDacCoefficients(){configure(FilterCalibration{});}
    void configure(double tau){auto c=FilterCalibration{};c[FilterParam::NativeRefTau]=tau*1000;configure(c);}
    void configure(const FilterCalibration& c){
        c.validate();using P=FilterParam;
        const std::array key={c[P::NativeRefTau],c[P::NativeRref],c[P::NativeRivMinus],c[P::NativeRivPlus],c[P::NativeCiv],
            c[P::NativeRsample],c[P::NativeRfeedback],c[P::NativeRon],c[P::NativeCsample],c[P::NativeChold]};
        if(key==key_)return;
        key_=key;tau_=key[0]*1e-3;
        referenceR=key[1]*1000;minusR=key[2]*1000;plusR=key[3]*1000;
        const double rs=key[5],rf=key[6]*1000,ron=key[7];
        a=rf/(rs+rf);b=rs/(rs+rf);ratio=key[8]/key[9];
        const bool nominal=key[2]==2.49&&key[4]==220&&rs==200&&rf==10000&&ron==150&&key[8]==470;
        const double ti=nominal?tauIv:minusR*key[4]*1e-12;
        const double t1=nominal?tau1:(ron+rs*rf/(rs+rf))*key[8]*1e-12;
        const double t2=nominal?tau2:ron*key[8]*1e-12;
        const double ar=1/tau_,am=1/ti,p=1/t1;referenceToMinus=am/(am-ar);
        // The allowed I/V and sample RC ranges stay below the minimum10us
        // reference RC. I/V and sample poles may coincide: use the exact limit.
        auto convolution=[](double x,double y,double ex,double ey,double t){
            const double gap=y-x;
            if(gap==0)return y*t*ey;
            if(std::abs(gap)<1e-6*std::max(x,y))return y*ey*std::expm1(gap*t)/gap;
            return y*(ex-ey)/gap;
        };
        for(unsigned n=0;n<reference.size();++n){const double t=double(n)/nativeTimeRate;
            reference[n]=std::exp(-t/tau_);fixed_.iv[n]=std::exp(-t/ti);fixed_.p1[n]=std::exp(-t/t1);fixed_.p2[n]=std::exp(-t/t2);
            fixed_.mean2[n]=n?-std::expm1(-t/t2)/(t/t2):1;
            fixed_.sampleRef[n]=convolution(ar,p,reference[n],fixed_.p1[n],t);
            fixed_.sampleMinus[n]=convolution(am,p,fixed_.iv[n],fixed_.p1[n],t);
        }
    }
    const Fixed& tables()const noexcept{return fixed_;}
    double tau()const noexcept{return tau_;}
    std::array<double,1251> reference{};
    double referenceR=9400,minusR=2490,plusR=2490,a=10000./10200,b=200./10200,ratio=1,referenceToMinus=0;
private:double tau_=0;Fixed fixed_;std::array<double,10> key_{};
};
class NativeDacChannel{
public:
    void reset()noexcept{reference_=minus_=charge_=out_=0;ready_=true;}
    // swing > 0: IC73 TL084 output-swing limit on the integrator output (level_model.h).
    KnownSignal advance(unsigned units,KnownByte code,KnownSignal acv,std::uint8_t phases,const NativeDacCoefficients& c,double swing=0){
        if(!units)return {0,true};
        if(units>1250)throw std::invalid_argument("Native DAC interval exceeds solver step");
        if(code.known!=255||!acv.known||!std::isfinite(acv.value)||acv.value<0||acv.value>5||(phases&12)!=12||(phases&3)==3){ready_=false;return {};}
        if(!ready_)reset(); // Explicit nominal zero-charge recovery.
        const auto& f=c.tables();const double er=c.reference[units],em=f.iv[units];
        const double magnitude=Am6072::magnitudeUnits(code.value);
        const double km=(code.value&128)?0:c.minusR*magnitude/(c.referenceR*2112.0);
        const double kp=(code.value&128)?c.plusR*magnitude/(c.referenceR*2112.0):0;
        const double r=reference_-acv.value,minusBase=minus_-km*acv.value;
        double area=out_*units;
        if(phases&1){
            const double target=c.a*(km-kp)*acv.value+c.b*out_;
            charge_=target+(charge_-target)*f.p1[units]+c.a*minusBase*f.sampleMinus[units]
                +c.a*r*(km*c.referenceToMinus*(f.sampleRef[units]-f.sampleMinus[units])-kp*f.sampleRef[units]);
        }else if(phases&2){
            area=(out_-c.ratio*charge_+c.ratio*charge_*f.mean2[units])*units;
            const double transfer=charge_*(1-f.p2[units]);charge_-=transfer;out_-=c.ratio*transfer;
            if(swing>0){out_=opampSwing(out_,swing);area=opampSwing(area/units,swing)*units;}
        }
        reference_=acv.value+r*er;minus_=km*acv.value+minusBase*em+km*r*c.referenceToMinus*(er-em);
        if(!std::isfinite(area)||!std::isfinite(out_)||!std::isfinite(charge_)){ready_=false;return {};}
        return {area,true};
    }
    double output()const noexcept{return out_;}
private:double reference_=0,minus_=0,charge_=0,out_=0;bool ready_=true;
};
}
