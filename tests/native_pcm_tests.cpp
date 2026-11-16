#include "runtime_audio.h"
#include <bit>
#include <chrono>
#include <iostream>
using namespace eii;using namespace eii::analog;
static void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
using Event=std::pair<std::uint64_t,NativeEvent>;
static auto stimulus(bool phases){
    std::vector<Event> events;auto put=[&](unsigned time,unsigned kind,unsigned ch,unsigned value,unsigned mask,unsigned phase=0){events.push_back({time,{0,std::uint8_t(kind),std::uint8_t(ch),std::uint8_t(value),std::uint8_t(mask),std::uint8_t(phase)}});};
    put(0,NativeEvent::Mode,0,1,255);
    for(unsigned ch=0;ch<8;++ch){put(0,NativeEvent::Pins,ch,0xb8,255,12);put(0,NativeEvent::Amp,ch,155,255);
        put(0,NativeEvent::Fc,ch,45,255);put(0,NativeEvent::Q,ch,0,255);put(0,NativeEvent::Switch,ch,35,63);
        unsigned value=0xb8;
        for(unsigned cycle=0;cycle<2000;++cycle){const unsigned origin=cycle*720+ch*17;
            for(auto point:{std::pair{32u,13u},std::pair{64u,12u},std::pair{96u,14u},std::pair{128u,12u}})
                put(origin+point.first,NativeEvent::Pins,ch,value,255,phases?point.second:12);
            value=cycle%2?0xb8:0x38;put(origin+138,NativeEvent::Pins,ch,value,255,12);
        }
    }
    std::stable_sort(events.begin(),events.end(),[](auto&a,auto&b){return a.first<b.first;});return events;
}
static auto render(const std::vector<Event>& events,unsigned factor,bool reconstruction,unsigned chunk=20000){
    host::AudioQueue queue(1056);RuntimeAudioEngine engine([&](auto frames){queue.push(frames);},factor==2?RuntimeAudioMode::NonlinearChip:RuntimeAudioMode::NonlinearChip192);
    engine.reconstruction(reconstruction);engine.nativeAudio(true,0);check(engine.nativeAudioEnabled(),"native route active");
    std::vector<std::uint32_t> pcm;unsigned next=0;
    for(std::uint64_t done=0;done<1460000;){
        const auto end=std::min<std::uint64_t>(done+chunk,1460000);done=end;
        while(next<events.size()&&events[next].first<=end){engine.nativeEvent(events[next].first,events[next].second);++next;}
        engine.nativeAdvance(end);engine.flush();const auto count=unsigned(engine.frames()-pcm.size());
        const auto timeout=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(queue.stats().queued<count){check(std::chrono::steady_clock::now()<timeout&&!engine.chipStats().failures,"native worker timeout/failure");std::this_thread::sleep_for(std::chrono::microseconds(100));}
        std::vector<host::StereoFrame> out(count);queue.pop(out);
        for(auto v:out){check(std::isfinite(v.left)&&v.left==v.right,"native finite stereo");pcm.push_back(std::bit_cast<std::uint32_t>(v.left));}
    }
    check(engine.nativeOverflows()==0&&engine.nativeOrderingErrors()==0,"native event transport must not lose/reorder inputs");
    check(queue.stats().dropped==0&&queue.stats().missing==0&&engine.chipStats().dropped==0&&engine.chipStats().failures==0,"native PCM transport loss");
    return pcm;
}
int main()try{
    const auto events=stimulus(true),open=stimulus(false);
    for(unsigned factor:{2u,4u}){
        const auto a=render(events,factor,false),repeat=render(events,factor,false),filtered=render(events,factor,true),silent=render(open,factor,false);
        check(a==repeat,"native playback deterministic across worker scheduling");
        check(a==render(events,factor,false,10000)&&a==render(events,factor,false,40000),"native PCM invariant under0.5/1/2ms producer chunks");
        unsigned active=0,different=0;double peak=0;
        for(unsigned n=0;n<a.size();++n){const auto v=std::bit_cast<float>(a[n]);active+=v!=0;different+=a[n]!=filtered[n];peak=std::max(peak,double(std::abs(v)));check(std::bit_cast<float>(silent[n])==0,"open deglitch switches must hold initial zero output");}
        check(active>100&&different>100&&peak>1e-6,"native deglitch and S3528 must reach final PCM");
        std::cout<<"PASS native PCM factor="<<factor<<" frames="<<a.size()<<" active="<<active<<" S3528changed="<<different<<" peak="<<peak<<" repeat bit exact; open switches silent; no loss\n";
        NonlinearVoiceBank bank(factor),fresh(factor);NonlinearVoiceBank::Input frame;
        frame.native.enabled=true;frame.native.initial.channel0=true;
        for(unsigned ch=0;ch<8;++ch){auto& p=frame.native.initial;p.dac[ch]={0xb8,255};p.phases[ch]=13;
            p.cv[0][ch]={155,255};p.cv[1][ch]={45,255};p.cv[2][ch]={0,255};p.sw[ch]={35,63};}
        for(unsigned n=0;n<50;++n){frame.native.start=n*nativeFrameTime;bank.process(frame);}
        frame.native.start+=nativeFrameTime;frame.native.valid=false;check(bank.process(frame)==0,"invalid packet must mute final PCM");
        frame.native.start+=nativeFrameTime;frame.native.valid=true;
        check(bank.process(frame)==fresh.process(frame),"valid snapshot after invalid packet restarts nominal analog history");
        frame.native.start+=3*nativeFrameTime;NonlinearVoiceBank afterGap(factor);
        check(bank.process(frame)==afterGap.process(frame),"timestamp gap must not retain stale native charge/filter history");
        // Post-chain level trim must not change nonlinear drive or solver work.
        NonlinearVoiceBank plain(factor),boosted(factor);FilterCalibration profile;
        plain.calibrate(profile);profile[FilterParam::NativeOutDb]=20;boosted.calibrate(profile);
        NativeCollector collector(0);unsigned count=0;double gain=1;
        auto compare=[&](const NativeFrame& packet){NonlinearVoiceBank::Input in;in.native=packet;
            const double a=plain.process(in),b=boosted.process(in);gain+=(10-gain)*.001;++count;
            check(std::abs(b-a*gain)<1e-7,"native trim must be output gain only");};
        for(const auto& event:events)collector.event(event.first,event.second,compare);
        collector.advance(1460000,compare);check(count>3000,"gain test covers settled worker-sized packets");
        const auto p=plain.solverStats(),q=boosted.solverStats();
        check(p.samples==q.samples&&p.iterations==q.iterations&&p.fallbacks==q.fallbacks&&p.recoveries==q.recoveries&&p.held==q.held,"output trim cannot change nonlinear solver trajectory");
        std::cout<<"PASS native +20dB monitor trim: identical solver samples/iterations/recovery at factor="<<factor<<'\n';
    }
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
