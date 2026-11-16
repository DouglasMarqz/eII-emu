#include "native_audio.h"
#include <iostream>
#include <vector>
using namespace eii;using namespace eii::analog;
static void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
// Independent differential-equation RK4 oracle. State R,C187,C74,C83-output.
using State=std::array<double,4>;
static State derivative(State x,unsigned code,double cv,unsigned phase,double tau,const FilterCalibration& c){
    using P=FilterParam;
    const double rm=c[P::NativeRivMinus]*1000,rp=c[P::NativeRivPlus]*1000,rr=c[P::NativeRref]*1000;
    const double k=Am6072::magnitudeUnits(code)/(rr*2112.),km=(code&128)?0:rm*k,kp=(code&128)?rp*k:0;
    const double rs=c[P::NativeRsample],rf=c[P::NativeRfeedback]*1000,ron=c[P::NativeRon],cs=c[P::NativeCsample]*1e-12;
    State d{(cv-x[0])/tau,(km*x[0]-x[1])/(rm*c[P::NativeCiv]*1e-12),0,0};
    if(phase==1)d[2]=((rf*(x[1]-kp*x[0])+rs*x[3])/(rs+rf)-x[2])/((ron+rs*rf/(rs+rf))*cs);
    if(phase==2){d[2]=-x[2]/(ron*cs);d[3]=d[2]*c[P::NativeCsample]/c[P::NativeChold];}
    return d;
}
static void rk4(State& x,double dt,unsigned code,double cv,unsigned phase,double tau,const FilterCalibration& profile){
    const auto add=[](State a,State b,double h){for(unsigned i=0;i<4;++i)a[i]+=b[i]*h;return a;};
    const auto a=derivative(x,code,cv,phase,tau,profile),b=derivative(add(x,a,dt/2),code,cv,phase,tau,profile),c=derivative(add(x,b,dt/2),code,cv,phase,tau,profile),d=derivative(add(x,c,dt),code,cv,phase,tau,profile);
    for(unsigned i=0;i<4;++i)x[i]+=dt*(a[i]+2*b[i]+2*c[i]+d[i])/6;
}
int main()try{
    for(unsigned variant=0;variant<5;++variant){
        const double tau=variant==0?.00001:variant==2?.02:.00235;
        FilterCalibration profile;profile[FilterParam::NativeRefTau]=tau*1000;
        if(variant==3){profile[FilterParam::NativeCsample]=680;profile[FilterParam::NativeChold]=330;profile[FilterParam::NativeRfeedback]=15;profile[FilterParam::NativeRon]=220;}
        if(variant==4){profile[FilterParam::NativeRivMinus]=1;profile[FilterParam::NativeCiv]=220;profile[FilterParam::NativeRsample]=200;profile[FilterParam::NativeRfeedback]=10;profile[FilterParam::NativeRon]=100;profile[FilterParam::NativeCsample]=220000./(100+200.*10000/10200);}
        NativeDacCoefficients c;c.configure(profile);NativeDacChannel analytic;State x{};
        double maxError=0,maxAreaError=0;
        for(unsigned step=0;step<1600;++step){
            const unsigned phase=(step%100<10)?1:(step%100<20)?2:0,code=(step/137)%2?0xb8:0x38;
            const double cv=step<700?3:1;
            const auto area=analytic.advance(120,{std::uint8_t(code),255},{cv,true},std::uint8_t(12|phase),c);
            double numericArea=0;
            for(unsigned n=0;n<1000;++n){const double before=x[3];rk4(x,1e-9,code,cv,phase,tau,profile);numericArea+=(before+x[3])*.5*.12;}
            maxError=std::max(maxError,std::abs(x[3]-analytic.output()));maxAreaError=std::max(maxAreaError,std::abs(numericArea-area.value));
        }
        check(maxError<1e-7&&maxAreaError<.001,"analytic deglitch vs independent1ns RK4");
        std::cout<<"PASS RC/deglitch tau="<<tau<<" voltageError="<<maxError<<" areaError="<<maxAreaError<<'\n';
    }
    NativeDacCoefficients c;NativeDacChannel a,b;
    for(unsigned n=0;n<10000;++n){const unsigned phase=n%3==0?1:n%3==1?2:0;
        const auto x=a.advance(192,{0xb8,255},{3,true},std::uint8_t(12|phase),c);double area=0;
        for(unsigned j=0;j<192;++j)area+=b.advance(1,{0xb8,255},{3,true},std::uint8_t(12|phase),c).value;
        check(std::abs(a.output()-b.output())<1e-8&&std::abs(x.value-area)<1e-6,"arbitrary interval splitting must preserve state/area");
    }
    check(!a.advance(1,{0,0},{3,true},12,c).known,"unknown input cannot produce known sound");
    check(a.advance(1,{0,255},{3,true},12,c).known,"explicit nominal zero recovery");
    NativeCollector collector(100);std::vector<NativeFrame> frames;auto sink=[&](const NativeFrame& f){frames.push_back(f);};
    collector.event(101,{0,NativeEvent::Pins,2,0xb8,255,13},sink);collector.event(517,{0,NativeEvent::Pins,2,0x38,255,12},sink);
    collector.advance(934,sink);
    check(frames.size()==2&&frames[0].events[0].offset==6&&frames[1].events[0].offset==2,"exact rational boundaries preserve6unit hardware timestamps");
    check(frames[1].initial.dac[2].value==0xb8,"next packet starts with complete pre-event pins");
    NativeCollector bounded(0);std::vector<NativeFrame> full;auto capture=[&](const NativeFrame& f){full.push_back(f);};
    for(unsigned n=0;n<193;++n)bounded.event(n,{0,NativeEvent::Pins,0,std::uint8_t(n),255,12},capture);
    bounded.advance(834,capture);check(!full[0].valid&&bounded.overflows()==1&&full[1].valid&&full[1].initial.dac[0].value==192,"overflow invalidates frame and preserves complete recovery snapshot");
    bounded.event(1,{},capture);check(bounded.orderingErrors()==1,"backward event time rejected");
    NativeCollector seconds(100);unsigned count=0;auto countFrame=[&](const NativeFrame&){++count;};seconds.advance(20000100,countFrame);
    check(count==48000,"one hardware second gives48000 PCM frames without rounded edges");
    std::cout<<"PASS block invariance, knownness, exact clocks, bounded overflow and event ordering\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
