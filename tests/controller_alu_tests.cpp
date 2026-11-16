#include "machine.h"
#include "production_alu.h"
#include <iostream>
#include <stdexcept>
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static unsigned reference(unsigned word,unsigned flags,unsigned captured,bool ci,bool a,bool b){
    // Trace each physical 74S283 separately, IC123 (low) to IC127.
    // All A pins join UCD04.1A except IC125 A0/A1 -> UCD04.1B.
    unsigned total=0,carry=unsigned(ci);
    for(unsigned chip=0;chip<5;++chip) {
        unsigned pins=a?15:0;
        if(chip==2)pins=(a?12:0)|(b?3:0);
        const unsigned nibble=((word>>(4*chip))&15)+pins+carry;
        total|=(nibble&15)<<(4*chip);carry=nibble>>4;
    }
    const bool toggle=!(captured&8) && (captured&2) && (captured&16);
    return (total&0xfffff)|(carry?0x800000:0)|((flags&1)?0x400000:0)|
        (((bool(flags&2))!=toggle)?0x200000:0)|((flags&4)?0x100000:0);
}
int main(){try{
    eii::ControllerDataPipeline d;d.flags={0,7};
    const unsigned words[]={0,1,15,255,256,511,1023,4095,65535,0xff000,0xffffe,0xfffff};
    for(unsigned word:words)for(unsigned ctl=0;ctl<8;++ctl)for(unsigned flags=0;flags<8;++flags)for(unsigned cap=0;cap<32;++cap){
        d.low20={word,0xfffff};d.flags={std::uint8_t(flags),7};d.ucd4b={true,bool(ctl&4)};
        auto result=eii::controllerAlu(d,{std::uint8_t(cap),31},{true,bool(ctl&1)},{true,bool(ctl&2)});
        require(result.known==0xffffff && result.value==reference(word,flags,cap,ctl&1,ctl&2,ctl&4),"five-adder arithmetic, operands and flag pin map vs integer reference");
        require(eii::production::aluOutput(word,ctl&1,ctl&2,ctl&4,std::uint8_t(flags),std::uint8_t(cap))==result.value,"production ALU matches independently traced five-chip pins");
    }
    unsigned address=0x120;
    for(unsigned n=0;n<1024;++n) {
        const auto next=eii::production::alu(address,false,true,true);
        require((next&0xfffff)==((address-1)&0xfffff),"reverse sample walk decrements one byte across nibble/byte/20-bit wrap");
        require(bool(next&0x800000)==bool(address),"decrement carry-out follows five-adder borrow convention");
        address=next&0xfffff;
    }
    require(eii::production::alu(0xabcde,true,true,true)==0x8abcde,"all A pins plus carry holds low20 and emits carry");
    // One physical UCD04.1B wire drives BOTH A8 and A9. For B=0x100,
    // CI=A4a=0, B4b unknown: outcomes0x100/0x400 both force sum9=0.
    // Treating those two pins as independent loses this certain zero.
    d.low20={0x100,0xfffff};d.flags={0,7};d.ucd4b={};
    const auto shared=eii::controllerAlu(d,{0,31},{true,false},{true,false});
    require(shared.known==(0xffffff^0x500)&&!(shared.value&shared.known),"shared UCD04.1B correlation retains known sum9 zero");
    // IC138 and IC152 are unrelated at power-up, then share a PROM sample.
    d.low20={0x100,0xfffff};d.ucd4b={};
    const auto separate=eii::controllerAlu(d,{0,31},{true,false},{});
    const auto linked=eii::controllerAlu(d,{0,31},{true,false},{},true);
    require(linked.known==(0xffffff^0x8001ff) && (linked.value&linked.known)==0,"joint UCD04 sample gives B or B-1, with overflow clear at zero offset");
    require((linked.known&~separate.known)!=0,"joint sample recovers bits without choosing control value");
    // Independent complete operand/control enumeration across pin/carry
    // boundaries. Require maximal local known masks, not soundness alone.
    const unsigned groups[][4]={{0,7,8,9},{8,9,10,11},{10,11,12,19},{0,8,12,19}};
    unsigned partialCases=0;
    for(bool sameSample:{false,true})
    for(const auto& group:groups)for(unsigned background:{0u,0x73a41u,0xfffffu})
    for(unsigned pattern=0;pattern<81;++pattern)for(unsigned controls=0;controls<27;++controls){
        unsigned variable=0;for(unsigned bit:group)variable|=1u<<bit;
        unsigned p=pattern,k=0xfffff^variable,v=background&k;
        for(unsigned bit:group){unsigned digit=p%3;p/=3;if(digit)k|=1u<<bit;if(digit==2)v|=1u<<bit;}
        unsigned q=controls;eii::LogicLevel ctl[3];
        for(auto& x:ctl){unsigned digit=q%3;q/=3;x={digit!=0,digit==2};}
        if(sameSample && ctl[1].known && ctl[2].known && ctl[1].high!=ctl[2].high)continue;
        ++partialCases;
        d.low20={v,k};d.flags={5,7};d.ucd4b=ctl[2];
        auto got=eii::controllerAlu(d,{18,31},ctl[0],ctl[1],sameSample);
        unsigned expected=0,known=0xffffff;bool first=true;
        for(unsigned bits=0;bits<16;++bits){unsigned w=v;unsigned i=0;
            for(unsigned bit:group)w=(w&~(1u<<bit))|(((bits>>i++)&1)<<bit);
            if((w^v)&k)continue;
            for(unsigned c=0;c<8;++c){bool fits=true;
                for(unsigned bit=0;bit<3;++bit)if(ctl[bit].known&&ctl[bit].high!=bool(c&(1u<<bit)))fits=false;
                if(sameSample && bool(c&2)!=bool(c&4))fits=false;
                if(!fits)continue;
                const unsigned r=reference(w,5,18,c&1,c&2,c&4);
                if(first){first=false;expected=r;}known&=~(expected^r);
            }
        }
        require(got.known==known&&!((got.value^expected)&known),"partial ALU equals exact compatible integer outcome consensus");
    }
    require(partialCases==46656,"independent and jointly sampled partial-case coverage");
    d.low20={0,0};d.flags={0,0};d.ucd4b={true,false};
    require(eii::controllerAlu(d,{}, {true,false},{true,false}).known==0x800000,"all-unknown word plus zero has known zero carry only");
    require(eii::controllerAlu(d,{}, {true,true},{true,false}).known==0,"all-unknown word plus one cannot invent carry or sum");
    d.ucd4b={};
    require(eii::controllerAlu(d,{}, {true,false},{true,false},true).known==0x800000,"known shared zero propagates before unknown-word fast path");
    require(eii::controllerAlu(d,{}, {true,false},{true,false}).known==0,"independent unknown startup control cannot inherit zero");
    d.ucd4b={true,true};
    const auto unknownHold=eii::controllerAlu(d,{}, {true,true},{true,true});
    require(unknownHold.known==0x800000 && (unknownHold.value&0x800000),"arbitrary20-bit operand plus0x100000 has known carry ONE without known sum bits");
    // DECCTL ternary inputs: independently enumerate four relevant bits.
    for(unsigned pattern=0;pattern<81;++pattern){unsigned p=pattern,k=0,v=0;for(unsigned bit=0;bit<4;++bit){unsigned t=p%3;p/=3;if(t)k|=1u<<bit;if(t==2)v|=1u<<bit;}
        eii::KnownByte flags{std::uint8_t((v&1)<<1),std::uint8_t((k&1)<<1)};
        eii::KnownByte cap{std::uint8_t((v&2)|((v&4)<<1)|((v&8)<<1)),std::uint8_t((k&2)|((k&4)<<1)|((k&8)<<1))};
        unsigned possibilities=0;for(unsigned x=0;x<16;++x)if(!((x^v)&k)){bool r=bool(x&1)!=(!(x&4)&&bool(x&2)&&bool(x&8));possibilities|=r?2:1;}
        auto got=eii::decrementControl(flags,cap);require(got.known==(possibilities!=3) && (!got.known || got.high==(possibilities==2)),"DECCTL partial truth table");
    }
    eii::ControllerAluLatch latch;
    latch.driveClock({true,true},{1,0xffffff});require(latch.output.known==0,"unknown initial clock does not fabricate capture");
    latch.driveClock({true,false},{2,0xffffff});require(latch.output.known==0,"falling gated clock holds");
    latch.driveClock({true,true},{3,0xffffff});require(latch.output.known==0xffffff && latch.output.value==3,"known rising gated clock captures");
    latch.driveClock({true,true},{4,0xffffff});require(latch.output.value==3,"held high clock holds");
    latch.driveClock({},{});latch.driveClock({true,true},{1,0xffffff});require(latch.output.known==0xfffffd,"possible rising clock merges hold/capture bits");
    // Synthesized microprogram: channel1/register1 is incremented exactly
    // once each scan, from pipelined data, then written back through RFD.
    eii::PromLatch rom;rom.upper.fill(0);
    for(unsigned a=0;a<512;++a){unsigned phase=(a>>5)&7;
        rom.lower[a]=(a&256)?0x45:std::uint8_t(1|8|((phase==0)?32:0)|((phase==1||phase==2)?0:64)|((phase==2)?4:0));
    }
    eii::ControllerTiming c(8);c.loadProms(rom);c.enableRegisterWrites();c.reset(false);
    eii::CpuRegisterStaging staged;staged.write(0x48,5);staged.write(0x50,16);staged.write(0x58,0);staged.write(0x60,0);c.cpuRegisterWrite(staged,true);
    std::array<eii::LogicLevel,8> req;req.fill({true,false});unsigned increments=0;
    for(unsigned n=0;n<400;++n){auto before=c.registerValue(5);auto pipe=c.dataPipelineValue();auto ctrl=c.control;auto oldAlu=c.aluOutputValue();
        c.fallingClock();c.risingClock(req);auto after=c.registerValue(5);
        if(before.known==0xffffff && after.known==0xffffff && before.value!=after.value){++increments;require(after.value==before.value+1,"ALU output bus writes one increment, not direct SRAM manipulation");}
        if((ctrl.known&32) && !(ctrl.value&32)) {auto now=c.aluOutputValue();require(now.known==oldAlu.known && !((now.value^oldAlu.value)&now.known),"UCD05 low holds ALU output through source rising edge");}
        if((ctrl.known&40)==40 && (ctrl.value&32) && pipe.known==0xfffff && (pipe.value&0xfffff)<100){auto out=c.aluOutputValue();require((out.known&0xfffff)==0xfffff && (out.value&0xfffff)==pipe.value+1,"ALU uses OLD pipeline on shared source edge");}
    }
    require(increments>=3 && c.registerValue(5).known==0xffffff,"connected ALU/SRAM feedback microprogram runs multiple scans");
    eii::ControllerTiming envelope;envelope.loadProms(rom);envelope.enableRegisterWrites();envelope.reset(false);
    std::array<eii::ControllerTiming,16> concrete;
    for(unsigned i=0;i<16;++i){concrete[i]=eii::ControllerTiming(i);concrete[i].loadProms(rom);concrete[i].enableRegisterWrites();concrete[i].reset(false);}
    for(unsigned n=0;n<200;++n){
        if(n==17){envelope.cpuRegisterWrite(staged,true);for(auto& x:concrete)x.cpuRegisterWrite(staged,true);}
        envelope.fallingClock();envelope.risingClock(req);
        for(auto& x:concrete){x.fallingClock();x.risingClock(req);}
        auto expected=concrete[0].aluOutputValue();auto cell=concrete[0].registerValue(5);
        for(unsigned i=1;i<16;++i){expected=eii::merge24(expected,concrete[i].aluOutputValue());cell=eii::merge24(cell,concrete[i].registerValue(5));}
        auto got=envelope.aluOutputValue(),stored=envelope.registerValue(5);
        require(got.known==expected.known && !((got.value^expected.value)&got.known),"ALU output envelope equals16 concrete startup phases");
        require(stored.known==cell.known && !((stored.value^cell.value)&stored.known),"ALU writeback envelope equals16 concrete startup phases");
    }
    // Connected proof: uncertain PRECAR drives raw PROM4 while a CPU-staged
    // low20 word is known. No direct pipeline/ALU injection or chosen flag.
    eii::PromLatch linkedRom;linkedRom.upper.fill(0);
    for(unsigned a=0;a<512;++a)linkedRom.lower[a]=(a&256)?0x45:std::uint8_t(0x61|((a&8)?16:0));
    eii::ControllerTiming sharedPath(8);sharedPath.loadProms(linkedRom);sharedPath.enableRegisterWrites();sharedPath.reset(false);
    auto partialStaged=staged;partialStaged.data={0x100,0xfffff};sharedPath.cpuRegisterWrite(partialStaged,true);
    unsigned linkedCaptures=0;
    for(unsigned n=0;n<600;++n) {
        const auto ctrl=sharedPath.control;const auto pipe=sharedPath.dataPipelineValue();
        const auto b4=sharedPath.delayedUcd4();
        if(sharedPath.ucd4SamplesLinked() && (ctrl.known&32) && (ctrl.value&32) &&
           !(ctrl.known&16) && !b4.known && pipe.known==0xfffff) {
            eii::ControllerDataPipeline observed;observed.low20=pipe;observed.ucd4b=b4;
            const auto independent=eii::controllerAlu(observed,{}, {bool(ctrl.known&8),bool(ctrl.value&8)},{});
            const auto joint=eii::controllerAlu(observed,{}, {bool(ctrl.known&8),bool(ctrl.value&8)},{},true);
            sharedPath.fallingClock();sharedPath.risingClock(req);
            const auto out=sharedPath.aluOutputValue();
            require((out.known&0x8fffff)==(joint.known&0x8fffff) && !((out.value^joint.value)&joint.known&0x8fffff),"Machine controller ALU consumes proven joint sample relation");
            if(joint.known&~independent.known&0xfffff)++linkedCaptures;
        } else {sharedPath.fallingClock();sharedPath.risingClock(req);}
    }
    require(linkedCaptures>0,"connected uncertain-PROM fixture actually exercises extra known sum bits");
    // Joint-sample provenance starts only when the data pipeline clocks.
    eii::ControllerTiming provenance;provenance.loadProms(rom);
    require(!provenance.ucd4SamplesLinked(),"PROM loading does not initialize physical control latches");
    provenance.risingClock(req);
    require(!provenance.ucd4SamplesLinked(),"disabled data path cannot establish joint sampling");
    provenance.enableRegisterWrites();provenance.reset(false);provenance.fallingClock();
    require(!provenance.ucd4SamplesLinked(),"falling source edge cannot establish joint sampling");
    provenance.risingClock(req);
    require(provenance.ucd4SamplesLinked(),"first shared rising edge establishes relation in all16 phases");
    provenance.reset(true);
    require(provenance.ucd4SamplesLinked(),"CRST retains unreset UCD04 latch relation");
    provenance.loadProms(rom);
    require(provenance.ucd4SamplesLinked(),"reloading lookup contents does not erase sampled latch relation");
    auto keep=c.aluOutputValue();c.reset(true);require(c.aluOutputValue().known==keep.known && c.aluOutputValue().value==keep.value,"CRST does not clear ALU latches");
    std::cout<<"Partial cases: "<<partialCases<<"\n";
    std::cout<<"PASS:20-bit five-adder operands/carry/flag wiring, exact correlated partial arithmetic vs exhaustive reference, DECCTL ternary table, gated latch edges/unknowns/reset and repeated connected ALU-to-SRAM increments; local correlations preserved, full controller-history correlations remain conservative\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
