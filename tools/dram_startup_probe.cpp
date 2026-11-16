// Explicit DRAM startup experiment on the normal firmware/disk/controller path.
// unknown keeps the verification default; a seed selects a concrete initial
// state before any tick. No ROM/media patch, on-demand read substitution or
// sector injection. OS results require the documented WR3 D3 core from PR1.
#include "machine.h"
#include <deque>
#include <iostream>
#include <limits>
#include <memory>
#include <string>

namespace {
std::uint64_t number(const char* text) {
    const std::string value(text);
    if(value.empty()||value.find_first_not_of("0123456789")!=std::string::npos)
        throw std::invalid_argument("unsigned decimal required");
    return std::stoull(value);
}
void escaped(const std::string& value) {
    constexpr char hex[]="0123456789abcdef";
    for(unsigned char c:value) {
        if(c=='\n'||(c>=32&&c<127))std::cout<<char(c);
        else std::cout<<"\\x"<<hex[c>>4]<<hex[c&15];
    }
}
}
int main(int argc,char** argv) try {
    if(argc!=8&&argc!=9&&argc!=10) {
        std::cerr<<"usage: dram_startup_probe main scanner prom135 prom137 image ticks unknown|seed32 [all|0..15] [none|panel-filter]\n";
        return 2;
    }
    const auto budget=number(argv[6]);
    if(budget>1000000000)throw std::invalid_argument("experiment budget exceeds 1 billion ticks");
    const std::string power=argv[7];
    const auto seed=power=="unknown"?0:number(argv[7]);
    if(power!="unknown"&&(!seed||seed>std::numeric_limits<std::uint32_t>::max()))
        throw std::invalid_argument("nonzero32-bit seed required");
    const std::string phaseArgument=argc>=9?argv[8]:"all";
    const std::string panelPlan=argc==10?argv[9]:"none";
    if(panelPlan!="none"&&panelPlan!="panel-filter")throw std::invalid_argument("panel plan none or panel-filter required");
    const auto phase=phaseArgument=="all"?255:number(argv[8]);
    if(phaseArgument!="all"&&phase>=16)throw std::invalid_argument("controller0..15 or all required");
    auto m=std::make_unique<eii::Machine>();
    if(phase<16) {
        // Explicit initial-condition experiment, not a verified hardware phase.
        // All upper-control histories within this phase are still retained.
        m->channelTimers.controllerTiming=eii::ControllerTiming(std::uint8_t(phase));
        m->channelTimers.enableControllerTiming();
        m->channelTimers.controllerTiming.enableRegisterWrites();
    }
    m->load(argv[1],argv[2],argv[3],argv[4]);
    if(power!="unknown")m->initializeDramPowerOn(std::uint32_t(seed));
    std::cout<<std::unitbuf<<"CONTROLLER_PHASE "<<phaseArgument<<" trajectories="<<m->channelTimers.controllerTiming.trajectoryCount()<<"\n"<<"POWER_ON "<<power
             <<" range=2800..7ffff xorshift32=13,17,5 (explicit experiment, not measured hardware)\n";
    m->floppy.attach(0,37,400000);
    m->floppy.drives[0]->insertSensorDisk(true);
    m->diskRead=std::make_unique<eii::DiskReadPath>(87,8);
    const auto image=std::make_shared<const eii::EmuiiImage>(eii::readFile(argv[5]));
    m->diskRead->mount(0,image,eii::EmuiiGap::Hxc2026);
    unsigned panelStage=0,sliderReads=0;
    std::uint64_t panelTick=0;
    bool panelDisplayChanged=false;
    unsigned sioPointer=0,payloadCrcChecks=0,payloadCrcErrors=0;
    std::uint64_t payloadBusBytes=0,payloadRamBytes=0,romDiscards=0,headerTransfers=0;
    std::uint64_t busMismatches=0,ramMismatches=0,unexpectedTransfers=0;
    // Passive expected-data oracle only; these arrays never drive Machine.
    std::vector<std::uint8_t> expectedMemory(m->memory.size()),seen(m->memory.size()),cpuSinceDma(m->memory.size());
    std::uint64_t previousCpuWrites=0;
    m->trace=[&](bool scan,bool wr,std::uint16_t port,std::uint8_t value,std::uint16_t pc) {
        if(scan&&!wr&&(port&0x7e)==0x34&&panelStage>=3&&
           m->scannerControls.adc.channel==eii::KnownByte{4,7}&&value==128&&m->scannerControls.adc.result.known==255)
            ++sliderReads;
        if(!scan&&(port&0xfb)==0x39) {
            const auto oldPointer=sioPointer;
            sioPointer=m->diskSio.channels[0].pointer;
            if(!wr&&oldPointer==1&&m->diskDma.length==3583) {
                ++payloadCrcChecks;
                if(value&0x70)++payloadCrcErrors;
                std::cout<<"PAYLOAD_RR1 tick="<<m->ticks<<" value="<<unsigned(value)
                         <<" transfer="<<m->diskDma.transfers<<"\n";
            }
        }
        if(!scan&&wr&&(port&0xf8)==0x68)
            std::cout<<"PAGE tick="<<std::dec<<m->ticks<<" port="<<std::hex<<(port&255)
                     <<" data="<<unsigned(value)<<" pc="<<pc<<std::dec<<"\n";
    };
    std::deque<unsigned> pcs;
    std::uint64_t previousWrites=0,unknownTicks=0,blockedTicks=0;
    std::uint32_t blockedAddress=0;
    bool blockedRead=false;
    std::string lastLcd0,lastLcd1;
    std::uint32_t first=0,last=0,unknownAddress=0;
    bool stoppedUnknown=false,stoppedController=false;
    for(std::uint64_t i=0;i<budget;++i) {
        const auto pc=m->main.cpu.pc;
        const auto beforeTransfers=m->diskDma.transfers;
        const auto beforeCount=m->diskDma.count;
        const auto beforeLength=m->diskDma.length;
        m->run(1);
        if(m->diskDma.transfers!=beforeTransfers) {
            const auto pins=m->main.pins;
            if((pins&(Z80_MREQ|Z80_WR))!=(Z80_MREQ|Z80_WR))++unexpectedTransfers;
            else if(beforeLength==3583&&beforeCount<3584) {
                const auto& drive=*m->floppy.drives[0];
                const unsigned side=drive.sideN()?0:1;
                const auto expected=image->payload(drive.cylinder,side)[beforeCount];
                ++payloadBusBytes;
                if(Z80_GET_DATA(pins)!=expected)++busMismatches;
                const auto physical=m->physical(Z80_GET_ADDR(pins),true);
                if(physical>=0x2800) {
                    ++payloadRamBytes;
                    if(m->dramKnown[physical]!=255||m->memory[physical]!=expected)++ramMismatches;
                    expectedMemory[physical]=expected;seen[physical]=1;cpuSinceDma[physical]=0;
                } else if(physical<8192)++romDiscards;
                else ++unexpectedTransfers;
                if(beforeCount==0)std::cout<<"PAYLOAD_SOURCE tick="<<m->ticks
                    <<" id="<<drive.cylinder*2+side<<" destination="<<std::hex<<physical<<std::dec<<"\n";
            } else ++headerTransfers;
        }
        if(m->dramWriteCycles!=previousCpuWrites) {
            cpuSinceDma[m->dramBusAddress]=1;previousCpuWrites=m->dramWriteCycles;
        }
        if(m->main.cpu.pc!=pc) {
            pcs.push_back(pc);
            if(pcs.size()>80)pcs.pop_front();
        }
        if(m->dmaDramWrites!=previousWrites) {
            if(previousWrites%3584==0)first=m->dramBusAddress;
            last=m->dramBusAddress;
            previousWrites=m->dmaDramWrites;
            if(previousWrites%3584==0) {
                std::cout<<"DRAM_CHUNK tick="<<m->ticks<<" writes="<<previousWrites
                         <<" transfers="<<m->diskDma.transfers<<" first="<<std::hex<<first
                         <<" last="<<last<<std::dec<<" pages=";
                for(unsigned p=0;p<4;++p)std::cout<<(p?",":"")<<unsigned(m->pages[p]);
                std::cout<<"\n";
            }
        }
        if(m->dramBusActive&&!m->dramServiced&&(m->main.pins&Z80_RD)&&m->dramKnown[m->dramBusAddress]!=255) {
            if(!unknownTicks||unknownAddress!=m->dramBusAddress) {
                unknownAddress=m->dramBusAddress;unknownTicks=0;
            }
            if(++unknownTicks==8192) {stoppedUnknown=true;break;}
        } else unknownTicks=0;
        const auto& controller=m->channelTimers.controllerTiming;
        const bool read=bool(m->main.pins&Z80_RD);
        if(m->dramBusActive&&(!m->dramServiced||!controller.cpuDramReady())) {
            if(!blockedTicks||blockedAddress!=m->dramBusAddress||blockedRead!=read) {
                blockedTicks=0;blockedAddress=m->dramBusAddress;blockedRead=read;
            }
            if(++blockedTicks==8192) {stoppedController=true;break;}
        } else blockedTicks=0;
        if(m->ticks%100000==0) {
            const auto line0=m->lcd.line(0),line1=m->lcd.line(1);
            if(line0!=lastLcd0||line1!=lastLcd1) {
                std::cout<<"LCD tick="<<m->ticks<<" ["<<line0<<"] ["<<line1<<"]\n";
                lastLcd0=line0;lastLcd1=line1;
                if(panelStage&&(line0!="P01 NULL PRESET "||line1!="                "))panelDisplayChanged=true;
            }
            if(panelPlan=="panel-filter"&&!panelStage&&line0=="P01 NULL PRESET "&&!m->diskDma.enabled) {
                m->scannerControls.setButton(eii::ScannerButton::Filter,true);
                panelStage=1;panelTick=m->ticks;
                std::cout<<"PANEL tick="<<m->ticks<<" Filter=pressed (raw contact row33,column5)\n";
            }
        }
        if(panelStage==1&&m->ticks-panelTick>=800000) {
            m->scannerControls.setButton(eii::ScannerButton::Filter,false);
            panelStage=2;panelTick=m->ticks;
            std::cout<<"PANEL tick="<<m->ticks<<" Filter=released\n";
        } else if(panelStage==2&&m->ticks-panelTick>=800000) {
            m->scannerControls.setVoltage(eii::ScannerAnalogInput::SliderA,2.5);
            panelStage=3;panelTick=m->ticks;
            std::cout<<"PANEL tick="<<m->ticks<<" SliderA=2.5V (ADC IN4)\n";
        }
        if(m->ticks%2000000==0)
            std::cerr<<"progress "<<m->ticks<<" dma="<<m->diskDma.transfers
                     <<" PC="<<std::hex<<m->main.cpu.pc<<std::dec
                     <<" reads="<<m->dramReadCycles<<"\n";
    }
    std::cout<<"END tick="<<m->ticks<<" PC="<<std::hex<<m->main.cpu.pc
             <<" bus="<<m->dramBusAddress<<" backingMask="<<unsigned(m->dramKnown[m->dramBusAddress])
             <<" reason="<<(stoppedUnknown?"unknown-backing-read":stoppedController?"controller-or-read-consensus-stall":"budget")<<"\nRECENT_PC_CHANGES";
    for(auto pc:pcs)std::cout<<" "<<pc;
    std::cout<<"\nKNOWN_RANGES";
    bool in=false;
    for(std::size_t a=0;a<m->memory.size();++a) {
        const bool known=m->dramKnown[a]==255;
        if(known&&!in)std::cout<<" "<<a<<"-";
        if(!known&&in)std::cout<<a-1;
        in=known;
    }
    if(in)std::cout<<m->memory.size()-1;
    std::uint64_t unique=0,finalChecked=0,finalMismatches=0,cpuModified=0;
    for(std::size_t a=0;a<seen.size();++a)if(seen[a]) {
        ++unique;
        if(cpuSinceDma[a]){++cpuModified;continue;}
        ++finalChecked;
        if(m->dramKnown[a]!=255||m->memory[a]!=expectedMemory[a])++finalMismatches;
    }
    std::cout<<"\n"<<std::dec<<"PAYLOAD_AUDIT busBytes="<<payloadBusBytes
             <<" ramBytes="<<payloadRamBytes<<" romDiscards="<<romDiscards
             <<" headerTransfers="<<headerTransfers<<" busMismatches="<<busMismatches
             <<" ramMismatches="<<ramMismatches<<" unexpected="<<unexpectedTransfers
             <<" crcChecks="<<payloadCrcChecks<<" crcErrors="<<payloadCrcErrors
             <<" unique="<<unique<<" finalChecked="<<finalChecked
             <<" finalMismatches="<<finalMismatches<<" cpuModified="<<cpuModified<<"\n";
    std::cout<<"PANEL_AUDIT plan="<<panelPlan<<" stage="<<panelStage
             <<" displayChanged="<<panelDisplayChanged<<" slider128Reads="<<sliderReads<<"\n";
    escaped(m->report()); // keep arbitrary serial-capture bytes text-safe
    return (busMismatches||ramMismatches||unexpectedTransfers||finalMismatches||payloadCrcErrors)?1:0; // Data audit, not a completed-boot assertion.
} catch(const std::exception& e) {
    std::cerr<<"ERROR: "<<e.what()<<"\n";
    return 1;
}
