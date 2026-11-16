#include "controller_timing.h"
#include "voice_transaction.h"
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
static void check(bool ok){if(!ok)throw std::runtime_error("voice transaction/reference mismatch");}
static void word(eii::Known24 a,eii::Known24 b){check(a.value==b.value && a.known==b.known);}
namespace eii {
struct ControllerRuntimeTestAccess {
    static void setup(ControllerTiming& c,unsigned channel,const std::array<Known24,4>& rf,unsigned seed) {
        auto& p=c.paths_[0];p.channel=channel;p.phase=8;p.shift={1,255};p.control={0x4a,65535};
        p.pipeline={3,3};p.captured={0,31};p.sameUcd4Sample=true;
        p.writePort.pending=p.writePort.flag={true,false};p.writePort.mpwenN={true,true};
        DigitalPowerOnBits bits(seed);p.alu.initializePowerOn(bits,{true,true});
        p.dram.initializePowerOn(bits,p.control,{true,false});
        for(unsigned slot=0;slot<4;++slot)p.registers.endWrite({std::uint8_t(channel*4+slot),63},rf[slot],{true,true});
        c.initializeUpperPaths();c.resetHeld_=false;
    }
    static const auto& path(const ControllerTiming& c){return c.paths_[0];}
};
}
int main(int argc,char** argv) try {
    std::mt19937 rng(0xe112037);std::uint64_t transactions=0,events=0;
    std::vector<std::uint8_t> memory(0x80000),known(memory.size(),255);
    for(unsigned fixture=0;fixture<3;++fixture) {
        eii::PromLatch prom;prom.upper.fill(0);
        for(unsigned a=0;a<512;++a) {
            const auto phase=(a>>5)&7;
            prom.lower[a]=phase==0?0x6b:phase==1?std::uint8_t((a&16)?0xae:0x4a):
                phase==2?std::uint8_t((a&16)?0x0f:0x4a):0x4a;
        }
        if(fixture==1)for(unsigned a=0;a<512;++a)if(((a>>5)&7)!=0)
            prom.lower[a]=std::uint8_t(rng()); // Generic simultaneous writes/captures/contention.
        if(fixture==2 && argc==3) {
            std::ifstream upper(argv[1],std::ios::binary),lower(argv[2],std::ios::binary);
            check(bool(upper.read(reinterpret_cast<char*>(prom.upper.data()),512)));
            check(bool(lower.read(reinterpret_cast<char*>(prom.lower.data()),512)));
        }
        eii::PromConsensus lower;lower.load(prom.lower);
        eii::VoicePrograms programs;programs.load(lower);
        for(unsigned mask=0;mask<32;++mask)for(unsigned value=0;value<32;++value) {
            const auto map=[](unsigned v){return ((v&1)<<4)|((v&2)<<2)|(v&4)|((v&8)>>2)|((v&16)>>4);};
            const auto& p=programs.get({std::uint8_t(value),std::uint8_t(mask)});bool valid=true;
            for(unsigned phase=0;phase<8;++phase) {
                const auto expected=lower.lookup((phase<<5)|map(value),0x1e0|map(mask));
                check(p.code[phase]==expected.value);valid&=expected.known==255;
            }
            check(valid==p.valid);
        }
        for(unsigned trial=0;trial<2048;++trial) {
            std::array<eii::Known24,4> rf;
            for(auto& cell:rf)cell={std::uint32_t(rng()&0xffffff),std::uint32_t(trial%3?rng()&0xffffff:0xffffff)};
            // Exercise every captured flag word with known/unknown low data.
            rf[2].value=(rf[2].value&0xfffff)|((trial&15)<<20);rf[2].known|=0xf00000;
            auto c=std::make_unique<eii::ControllerTiming>(8);c->loadProms(prom);c->attachCpuDram({&memory,&known});
            const auto channel=trial%8;eii::ControllerRuntimeTestAccess::setup(*c,channel,rf,trial+1);
            const auto incoming=eii::ControllerRuntimeTestAccess::path(*c).alu.output;
            const eii::LogicLevel request{true,bool(trial&16)};
            eii::VoiceTransaction tx;const std::uint64_t start=1000+trial*16;
            check(tx.compile(programs,rf,incoming,request,start));
            std::array<eii::LogicLevel,8> requests;requests.fill(request);
            unsigned cursor=0;
            for(unsigned phase=0;phase<8;++phase) {
                const auto before=eii::ControllerRuntimeTestAccess::path(*c).registerWrites;
                c->fallingClock();c->risingClock(requests);
                const auto& p=eii::ControllerRuntimeTestAccess::path(*c);const auto& beat=tx.beat(phase);
                word(p.sampledRfd,beat.rfd);word(p.alu.output,beat.alu);
                word(p.data.low20,beat.data.low20);check(p.data.flags==beat.data.flags && p.data.ucd4b==beat.data.ucd4b);
                check(p.captured==beat.captured && (p.control.value&255)==beat.nextControl.value);
                unsigned writes=0;
                while(cursor<tx.count && tx.events[cursor].halfClock<=start+phase*2) {
                    const auto& event=tx.events[cursor++];check(event.halfClock==start+phase*2);
                    if(event.kind==eii::VoiceTransaction::Kind::RegisterWrite){++writes;word(c->registerValue(channel*4+event.slot),event.value);}
                    if(event.kind==eii::VoiceTransaction::Kind::AddressCapture)word(p.upper.branches[0].dram.aluAddress.output,event.value);
                    if(event.kind==eii::VoiceTransaction::Kind::AluCapture)word(p.alu.output,event.value);
                }
                check(writes==p.registerWrites-before);
            }
            for(unsigned slot=0;slot<4;++slot)word(c->registerValue(channel*4+slot),tx.finalRegisters[slot]);
            check(cursor==tx.count);++transactions;events+=cursor;
        }
        eii::VoiceTransaction tx;std::array<eii::Known24,4> rf{};
        check(!tx.compile(programs,rf,{}, {},0) && tx.count==0);
        check(!tx.compile(programs,rf,{}, {true,false},UINT64_MAX) && tx.count==0);
    }
    std::cout<<"PASS:"<<transactions<<" functional voice transactions / "<<events<<" timed events vs edge oracle; raw RF/pipeline/ALU/address states match at every boundary\n";
} catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
