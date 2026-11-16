#include "runtime_audio.h"
#include "reference/basic_voice_070.h"
#include <bit>
#include <chrono>
#include <iostream>
using namespace eii;
static void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
static auto render(analog::RuntimeAudioMode mode,bool enabled){
    host::AudioQueue queue(1056);
    analog::RuntimeAudioEngine engine([&](auto frames){queue.push(frames);},mode);
    engine.reconstruction(enabled);
    require(engine.reconstructionEnabled()==enabled,"actual active flag including Legacy");
    std::vector<std::uint32_t> pcm;pcm.reserve(6000);
    for(unsigned begin=0;begin<6000;begin+=96){
        const auto count=std::min(96u,6000-begin);
        for(unsigned n=begin;n<begin+count;++n){
            analog::PlaybackInputs input;std::array<analog::KnownSignal,8> fc{},q{};std::array<KnownByte,8> sw{};
            for(unsigned ch=0;ch<8;++ch){input.codes[ch]={std::uint8_t(((n/(7+ch))&1)?0xb8:0x38),255};
                input.amplitudeCv[ch]={2,true};fc[ch]={1,true};q[ch]={0,true};sw[ch]={35,63};}
            engine.sample(input,fc,true,q,sw);
        }
        engine.flush();const auto timeout=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(queue.stats().queued<count){
            require(engine.chipStats().failures==0&&std::chrono::steady_clock::now()<timeout,"worker failure/timeout");
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        std::array<host::StereoFrame,96> frames{};queue.pop(std::span(frames.data(),count));
        for(unsigned n=0;n<count;++n){require(std::isfinite(frames[n].left)&&frames[n].left==frames[n].right,"finite stereo PCM");pcm.push_back(std::bit_cast<std::uint32_t>(frames[n].left));}
    }
    const auto stats=queue.stats();const auto dsp=engine.chipStats();
    require(stats.missing==0&&stats.dropped==0&&dsp.dropped==0&&dsp.failures==0,"transport must not drop or invent samples");
    require(pcm.size()==6000,"complete frame count");return pcm;
}
int main()try{
    analog::BasicPlaybackVoice current(48000);current.levelModel(analog::LevelModel::ce2e306());analog::ReferenceBasicPlaybackVoice070 old(48000); // only the level model differs
    for(unsigned n=0;n<24000;++n){const KnownByte code{std::uint8_t((n*37)>>4),std::uint8_t(n%997?255:0)};
        const analog::KnownSignal acv{2+std::sin(n*.002),true},fcv{1+.2*std::sin(n*.0007),true};
        require(std::bit_cast<std::uint64_t>(current.process(code,acv,fcv))==std::bit_cast<std::uint64_t>(old.process(code,acv,fcv)),"Legacy bypass differs from frozen0.70");}
    std::cout<<"PASS Legacy S3528 bypass bit exact against frozen0.70\n";

    for(auto mode:{analog::RuntimeAudioMode::BasicVoice,analog::RuntimeAudioMode::NonlinearChip,analog::RuntimeAudioMode::NonlinearChip192}){
        const auto off=render(mode,false),repeat=render(mode,false),on=render(mode,true);
        require(off==repeat,"scheduling must preserve repeat PCM bit for bit");
        unsigned changed=0,nonzero=0;for(unsigned n=0;n<off.size();++n){changed+=off[n]!=on[n];nonzero+=std::bit_cast<float>(off[n])!=0;}
        require(nonzero>100,"stimulus must be audible");
        require(changed>100,"S3528 must reach final PCM including Legacy");
        std::cout<<"PASS PCM mode="<<unsigned(mode)<<" frames="<<off.size()<<" changed="<<changed<<" repeat bit exact; no transport loss\n";
    }
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
