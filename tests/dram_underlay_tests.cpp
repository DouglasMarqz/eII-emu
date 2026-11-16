#include "machine.h"
#include <iostream>
#include <memory>
static void require(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
static void sample(eii::Machine& m,unsigned address,bool expectKnown) {
    auto view=m.dramBacking();
    eii::DramCpuPort ref;
    eii::production::DramPort con;
    con.memoryKnown=m.dramKnown.data();
    con.underlay=view.underlay;con.underlayKnown=view.underlayKnown;
    con.underlaySize=view.underlaySize;
    const auto rise=[&](unsigned c) {
        ref.rising({std::uint16_t(c),65535},{address,0xfffff},view);
        con.rising(c,address,m.memory.data());
    };
    const auto fall=[&](unsigned c) {
        ref.falling({std::uint16_t(c),65535},{true,false},view);
        con.falling(c,false,m.memory.data());
    };
    rise(0x80);fall(0x80);rise(0x80);fall(0x80);rise(0x80);
    ref.settle({0x8080,65535},view);con.settle(0x8080,m.memory.data());
    fall(0x8080);rise(0x8080);
    ref.settle({0x9880,65535},view);con.settle(0x9880,m.memory.data());
    fall(0x9880);rise(0x9880);
    fall(0x9980);rise(0x9980); // actual IC40 sample latch clock
    const auto value=view.read(address);
    require(con.memoryFault==!expectKnown,"CAS guard reflects physical DRAM knownness");
    require(con.mdoKnown==expectKnown,"production sample CAS knownness");
    require(ref.channelLatch.known==(expectKnown?255:0),"reference sample CAS knownness");
    if(expectKnown)
        require(con.mdoForTestValue()==value.value && ref.channelLatch.value==value.value,
                "reference/production CAS return physical DRAM rather than CPU overlay");
    else
        require(con.memoryFaultAddress==address && !con.memoryFaultCpu,
                "unknown physical sample read retains exact first-fault address/owner");
}
int main() try {
    auto m=std::make_unique<eii::Machine>(0);
    require(m->dramBacking().read(0x2000).known==0,"unseeded hidden DRAM remains unknown");
    sample(*m,0x2000,false);
    m->main.rom[0]=0xa5;m->memory[0x2000]=0x5a;
    m->initializeDramPowerOn(1);
    require(m->readMemory(false,0)==0xa5 && m->readMemory(false,0x2000)==0x5a,
            "concrete DRAM startup does not replace CPU ROM or local SRAM");
    const auto hidden=m->dramUnderlay;
    for(unsigned a:{0u,0x1fffu,0x2000u,0x27ffu,0x2800u,0x7ffffu})sample(*m,a,true);
    const auto physical=m->dramBacking().read(0x2000).value;
    m->writeMemory(false,0x2000,std::uint8_t(physical^255));
    require(m->readMemory(false,0x2000)==std::uint8_t(physical^255) &&
            m->dramBacking().read(0x2000).value==physical,
            "CPU SRAM write cannot alter sample-addressed DRAM underlay");
    sample(*m,0x2000,true);
    m->initializeDramPowerOn(1);
    require(m->dramUnderlay==hidden,"underlay startup seed is reproducible");
    m->dramUnderlayKnown[0x2000]=0;
    sample(*m,0x2000,false);
    std::cout << "PASS: CPU ROM/SRAM isolation; physical bank0 underlay CAS boundaries; reference/production agreement; seeded replay; unknown sample-CAS guard retained\n";
} catch(const std::exception& e) {std::cerr << "FAIL: " << e.what() << '\n';return 1;}
