#include "machine.h"
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
    eii::CpuRegisterStaging cpu;
    eii::ControllerRegisterFile rf;
    require(cpu.address.known==0 && cpu.data.known==0 && rf.read({0,63}).known==0,"power-up storage is unknown");
    require(!cpu.write(0x40,255) && !cpu.write(0x68,255),"adjacent decoder groups excluded");
    for(unsigned alias=0;alias<8;++alias){
        cpu.write(0x48+alias,0xe5);cpu.write(0x50+alias,0x12);cpu.write(0x58+alias,0x34);cpu.write(0x60+alias,0x56);
        require(cpu.address.value==0x25 && cpu.data.value==0x563412 && cpu.data.known==0xffffff,"all aliases and byte lanes");
    }
    for(unsigned ch=0;ch<16;++ch)for(unsigned reg=0;reg<4;++reg){
        const auto a=cpu.selectedAddress({std::uint8_t(ch),15},{std::uint8_t(reg),3},{true,true});
        require(a.value==ch*4+reg && a.known==63,"controller IC118 address wiring");
        require(cpu.selectedAddress({std::uint8_t(ch),15},{std::uint8_t(reg),3},{true,false}).value==0x25,"CPU address enable polarity");
        const auto uncertain=cpu.selectedAddress({std::uint8_t(ch),15},{std::uint8_t(reg),3},{});
        require(uncertain.known==unsigned((~(0x25^(ch*4+reg)))&63),"uncertain bus ownership consensus");
    }
    // Six nibble chips share one address; independently patterned values
    // expose lane/address swaps. Exhaust all 3^6 partially known addresses.
    for(unsigned a=0;a<64;++a)rf.endWrite({std::uint8_t(a),63},{0xa50000u|(a*257u),0xffffff},{true,true});
    for(unsigned t=0;t<729;++t){
        unsigned n=t,v=0,k=0;for(unsigned b=0;b<6;++b){unsigned d=n%3;n/=3;if(d)k|=1u<<b;if(d==2)v|=1u<<b;}
        unsigned expected=0,known=0xffffff;bool first=true;
        for(unsigned a=0;a<64;++a)if(((a^v)&k)==0){unsigned data=0xa50000u|(a*257u);if(first){expected=data;first=false;}known&=~(expected^data);}
        auto got=rf.read({std::uint8_t(v),std::uint8_t(k)});
        require(got.known==(known&0xffffff) && ((got.value^expected)&got.known)==0,"partial-address SRAM read consensus");
    }
    rf.endWrite({7,63},{0,0xffffff},{true,false});require(rf.read({7,63}).value==(0xa50000|7*257),"disabled write holds");
    rf.endWrite({7,63},{0xa50000,0xffffff},{});require(rf.read({7,63}).known==(0xffffffu^unsigned(7*257)),"unknown write enable preserves only common bits");
    rf.endWrite({0,62},{0,0xffffff},{true,true});require(rf.read({0,63}).known==0x5affff && rf.read({1,63}).known==0x5afefe,"ambiguous write address merges each candidate with held data");
    require(rf.read({2,63}).known==0xffffff,"ambiguous write cannot corrupt unselected cells");
    eii::RegisterWriteHandshake handshake;
    handshake.settle({true,false},{true,true});handshake.cpuStrobe();
    require(handshake.pending.known && handshake.pending.high && !handshake.flag.known,"CPU strobe cannot bypass second FF");
    handshake.sourceClock();handshake.settle({true,true},{true,false});
    require(handshake.mpwenN.high && handshake.flag.high,"request waits for ninth-slot STATE3");
    handshake.settle({true,true},{true,true});
    require(handshake.mpwenN.known && !handshake.mpwenN.high && handshake.pending.known && !handshake.pending.high,"window enables CPU and asynchronously clears first FF");
    handshake.cpuStrobe();require(!handshake.pending.high,"async clear dominates a simultaneous CPU strobe");
    handshake.sourceClock();handshake.settle({true,true},{true,true});
    require(handshake.mpwenN.high && !handshake.flag.high,"one source clock closes request window");
    for(unsigned bits=0;bits<32;++bits){
        auto l=[&](unsigned b){return eii::LogicLevel{true,bool(bits&(1u<<b))};};
        auto en=eii::registerWriteEnable(l(0),l(1),l(2),l(3),l(4));
        bool expected=!(bits&1) && (bits&16) && (!(bits&2) || !(bits&4) || (bits&8));
        require(en.known && en.high==expected,"RFWR NAND/AND truth table");
    }
    require(eii::registerWriteData({0x123456,0xffffff},{true,false},{true,true}).known==0xffffff,"CPU drives with ALU disabled");
    require(eii::registerWriteData({0x123456,0xffffff},{true,false},{}).known==0,"possibly enabled missing ALU cannot fabricate bus data");
    // Synthetic PROM: RFWR only in ninth slot, ALU OE disabled. CPU
    // register5 must change only after IC92 opens its source-clock window.
    eii::PromLatch prom;prom.upper.fill(0);
    for(unsigned a=0;a<512;++a)prom.lower[a]=64|((a&256)?4:0);
    eii::ControllerTiming timed(8);timed.loadProms(prom);timed.enableRegisterWrites();timed.reset(false);
    eii::CpuRegisterStaging staging;staging.write(0x48,5);staging.write(0x50,0x12);staging.write(0x58,0x34);staging.write(0x60,0x56);
    timed.cpuRegisterWrite(staging,true);
    require(timed.registerValue(5).known==0,"OUT staging alone does not commit SRAM");
    std::array<eii::LogicLevel,8> req;req.fill({true,false});
    timed.risingClock(req);
    require(timed.registerValue(5).known==0,"no write before first low-clock aperture");
    for(unsigned n=0;n<160;++n){timed.fallingClock();timed.risingClock(req);}
    require(timed.registerValue(5).known==0xffffff && timed.registerValue(5).value==0x563412,"ninth-slot CPU handshake writes real per-trajectory SRAM");
    require(timed.registerValue(4).known==0,"CPU write leaves unrelated register unknown");
    timed.reset(true);require(timed.registerValue(5).known==0xffffff && timed.registerValue(5).value==0x563412,"CRST does not erase SRAM");
    timed.reset(false);
    eii::ControllerTiming envelope;envelope.loadProms(prom);envelope.enableRegisterWrites();envelope.reset(false);
    std::array<eii::ControllerTiming,16> concrete;
    for(unsigned i=0;i<16;++i){concrete[i]=eii::ControllerTiming(i);concrete[i].loadProms(prom);concrete[i].enableRegisterWrites();concrete[i].reset(false);}
    for(unsigned n=0;n<170;++n){
        if(n==27 || n==99){staging.write(0x50,n);envelope.cpuRegisterWrite(staging,true);for(auto& c:concrete)c.cpuRegisterWrite(staging,true);}
        envelope.fallingClock();envelope.risingClock(req);
        for(auto& c:concrete){c.fallingClock();c.risingClock(req);}
        auto expected=concrete[0].registerValue(5);
        for(unsigned i=1;i<16;++i)expected=eii::merge24(expected,concrete[i].registerValue(5));
        auto got=envelope.registerValue(5);
        require(got.known==expected.known && !((got.value^expected.value)&got.known),"SRAM envelope equals all16 individual startup phases");
    }
    require(envelope.registerValue(5).known==0xffffff && (envelope.registerValue(5).value&255)==99,"second CPU request rearms and all startup phases converge on data");
    auto m=std::make_unique<eii::Machine>();
    m->channelTimers.controllerTiming.loadProms(prom);
    const std::uint8_t code[]={0x3e,0xe5,0xd3,0x4f,0x3e,0x12,0xd3,0x57,0x3e,0x34,0xd3,0x5f,0x3e,0x56,0xd3,0x67,0x76};
    std::copy(std::begin(code),std::end(code),m->main.rom.begin());
    bool sawActive=false;
    for(unsigned tick=0;tick<1000;++tick){
        m->run(1);
        if(m->stagingWritePending && m->stagingWritePort==0x4f){sawActive=true;require(m->registerStaging.address.known==0,"no latch before rising strobe");}
    }
    require(sawActive && m->registerStaging.address.value==0x25 && m->registerStaging.data.value==0x563412,"real CPU OUT deassertion captures all latches");
    for(auto count:m->registerStaging.writes)require(count==1,"held OUT writes exactly once");
    require(m->unmappedIo==0 && m->commands==1,"mapped CPU staging and high-byte command count");
    const auto committed=m->channelTimers.controllerTiming.registerValue(0x25);
    require(committed.known==0xffffff && committed.value==0x563412,"real Z80 OUT reaches IC92 and SRAM through half-edge scheduler");
    m->output(m->scanner,0x50,0);require(m->registerStaging.data.value==0x563412,"scanner cannot change CPU staging");
    for(unsigned p=0x68;p<0x70;++p){m->output(m->main,p,p);require(m->pages[p&3]==(p&15),"PA2 bank-register aliases");}
    std::cout<<"PASS: CPU address/24-bit staging, all port aliases, falling-selection/rising-strobe capture, IC118 address ownership, 64x24 SRAM unknown-state kernel and all729 partial address reads; IC92/RFWR per-trajectory writes verified; ALU absent\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
