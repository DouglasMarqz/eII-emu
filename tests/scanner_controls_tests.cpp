#include "machine.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void pulse(eii::Adc0809& a,unsigned channel) {
    a.drive(true,true,false,{std::uint8_t(channel),7});a.drive(false,false,false,{});
}
static void clocks(eii::Adc0809& a,unsigned count){while(count--)a.clock();}
static void code(std::vector<std::uint8_t>& p,std::initializer_list<std::uint8_t> bytes){p.insert(p.end(),bytes);}
int main(){try{
    eii::Adc0809 a;
    require(!a.result.known&&!a.eoc.known&&!a.channel.known,"ADC power-up latches and EOC remain unknown");
    clocks(a,100);require(!a.result.known&&!a.eoc.known,"clock without START cannot initialize ADC");
    // Independent ideal transfer at every code and every half-LSB boundary.
    unsigned transfers=0;
    for(unsigned channel=0;channel<8;++channel)for(unsigned sample=0;sample<256;++sample) {
        a.setInput(channel,{sample*5.0/256,true});pulse(a,channel);clocks(a,62);
        const auto old=a.result;
        require(a.converting()&&a.eoc.known&&!a.eoc.high,"conversion is busy before last decision");
        clocks(a,1);require(a.result.known==255&&a.result.value==sample,"all8 channels/all256 ideal quantizer codes");
        require(!a.eoc.high&&a.converting(),"new output is available one ADC clock before EOC");
        a.drive(false,false,true,{});require(a.bus()==a.result,"OE exposes stored result");
        a.drive(false,false,false,{});require(!a.bus().known&&!a.outputEnabled(),"OE low is high impedance, not stale driven byte");
        clocks(a,1);require(a.eoc.high&&!a.converting(),"64th ADC clock completes conversion");
        require(old.known==0||old.known==255,"old output remains a held completed byte");++transfers;
    }
    for(unsigned transition=1;transition<256;++transition) {
        const double boundary=(transition-0.5)*5/256;
        for(int side:{-1,0,1}) {
            a.setInput(0,{boundary+side*1e-8,true});pulse(a,0);clocks(a,64);
            require(a.result.value==(side<0?transition-1:transition),"half-LSB transition, below/on/above threshold");
        }
    }
    a.setInput(0,{5,true});pulse(a,0);clocks(a,64);require(a.result.value==255,"reference rail saturates to255");
    for(auto bad:{-0.01,5.01,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
        bool rejected=false;try{a.setInput(0,{bad,true});}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"invalid/nonfinite pin voltages rejected");
    }
    bool badChannel=false;try{a.setInput(8,{0,true});}catch(const std::invalid_argument&){badChannel=true;}
    require(badChannel,"invalid ADC channel rejected");
    // Address latches only on ALE rise, while START high holds SAR reset.
    a.setInput(2,{1.25,true});a.setInput(7,{5,true});
    a.drive(true,true,false,{2,7});a.drive(true,true,false,{7,7});clocks(a,100);
    require(!a.converting()&&a.channel.value==2&&!a.eoc.high,"held START cannot run or relatch address");
    a.drive(false,false,false,{});clocks(a,64);require(a.result.value==64,"ALE rise selects address, not START release data");
    // Interrupted conversions must not publish the abandoned result or EOC.
    pulse(a,7);clocks(a,40);const auto held=a.result;const auto count=a.completed;const auto aborted=a.aborted;
    pulse(a,2);clocks(a,62);require(a.result==held&&a.completed==count&&a.aborted==aborted+1,"new START aborts old conversion and retains old output");
    clocks(a,2);require(a.result.value==64&&a.completed==count+1,"replacement conversion alone completes");
    // No fabricated sample/hold: a late input step changes remaining decisions.
    a.setInput(0,{0,true});pulse(a,0);clocks(a,7);a.setInput(0,{5,true});clocks(a,57);
    require(a.result.value==127,"SAR observes live input at each decision, not a start snapshot");
    a.setInput(0,{});pulse(a,0);clocks(a,64);require(!a.result.known&&a.eoc.high,"unknown voltage completes with unknown data, not128");
    for(unsigned i=0;i<8;++i)a.setInput(i,{2.5,true});
    a.drive(true,true,false,{});a.drive(false,false,false,{});clocks(a,64);
    require(a.channel.known==0&&a.result.known==255&&a.result.value==128,"unknown address can still yield consensus data across all inputs");
    // Clock-to-start phase does not change constant voltage results. Source
    // phase is a simulation convention, independently exercise all8 offsets.
    for(unsigned phase=0;phase<8;++phase) {
        eii::ScannerControls s;s.setVoltage(eii::ScannerAnalogInput::SliderD,3.75);
        for(unsigned n=0;n<phase;++n)s.advanceCpuTick();
        s.driveBus(true,0xb6,true,{7,255});s.driveBus(false,0,false,{});
        for(unsigned n=0;n<512;++n)s.advanceCpuTick();
        require(s.adc.completed==1&&s.adc.result.value==192,"all8 CPU/source alignments finish by512 clocks");
    }
    // Independent OR-gate/decimal-decoder reference vs ALL PIO-B bytes.
    eii::ScannerControls s;
    for(unsigned pb=0;pb<256;++pb) {
        const bool b0=pb&1,b1=pb&2,b2=pb&4,b3=pb&8,b5=pb&32;
        const unsigned upper=unsigned(b0)+2*unsigned(b1)+4*unsigned(b2||b3)+8*unsigned(b5||b3);
        const unsigned lower=unsigned(b0)+2*unsigned(b1)+4*unsigned(b2||!b3)+8*unsigned(b5||!b3);
        int row=-1;
        if(upper<10)row=upper<8?int(upper):int(upper+24);
        if(lower<10){require(row<0,"decoder banks cannot select simultaneous rows");row=lower<8?int(lower+8):int(lower+32);}
        require(eii::ScannerControls::selectedRow(pb)==row,"all256 selector bytes match discrete OR/LS42 logic");
        require(s.columns(pb)==255,"all released contacts read pull-up255");
        for(unsigned r=0;r<42;++r)if(eii::ScannerControls::validRow(r))for(unsigned col=0;col<8;++col) {
            s.setContact(r,col,true);
            require(s.columns(pb)==(row==int(r)?std::uint8_t(255^(1u<<col)):255),"all20 physical rows/eight columns selected without cross-row leakage");
            s.setContact(r,col,false);
        }
    }
    s.setContact(2,1,true);s.setContact(2,6,true);s.setContact(3,1,true);
    require(s.columns(2)==0xbd&&s.columns(3)==0xfd,"multiple diode-isolated contacts and row isolation");
    bool invalidContact=false;try{s.setContact(16,0,true);}catch(const std::invalid_argument&){invalidContact=true;}
    require(invalidContact,"unwired matrix row rejected");
    eii::ScannerControls named;
    named.setButton(eii::ScannerButton::Select,true);
    named.setButton(eii::ScannerButton::Digit0,true);
    named.setButton(eii::ScannerButton::Filter,true);
    require(named.columns(0x28)==0xfe,"SELECT uses CN3 pin8/IC3Y8 row40 despite panel SBUS42 label");
    require(named.columns(0x29)==0xfe&&named.columns(0x21)==0xdf,"named ZERO and FILTER follow physical panel row/column wires");
    // Service sheet7-4: the five keypad contacts in each row descend from
    // SCON0 to4 (not4 to0); the three far-right contacts use SCON0..2.
    const std::array<eii::ScannerButton,10> digits{
        eii::ScannerButton::Digit0,eii::ScannerButton::Digit1,eii::ScannerButton::Digit2,
        eii::ScannerButton::Digit3,eii::ScannerButton::Digit4,eii::ScannerButton::Digit5,
        eii::ScannerButton::Digit6,eii::ScannerButton::Digit7,eii::ScannerButton::Digit8,eii::ScannerButton::Digit9};
    const unsigned rowPins[]={0x29,0x20,0x20,0x20,0x20,0x20,0x29,0x29,0x29,0x29};
    const unsigned columnBits[]={0,4,3,2,1,0,4,3,2,1};
    for(unsigned digit=0;digit<10;++digit){eii::ScannerControls panel;panel.setButton(digits[digit],true);
        require(panel.columns(rowPins[digit])==std::uint8_t(~(1u<<columnBits[digit])),"each named digit reaches its schematic SCON wire");
        panel.setButton(digits[digit],false);require(panel.columns(rowPins[digit])==255,"digit releases to pull-up");}
    for(unsigned i=0;i<3;++i){eii::ScannerControls panel;panel.setButton(eii::ScannerButton(unsigned(eii::ScannerButton::Transpose)+i),true);
        require(panel.columns(0x21)==std::uint8_t(~(1u<<i)),"transpose/tune/dynamic follow SCON0/1/2");}
    named.setButton(eii::ScannerButton::Select,false);require(named.columns(0x28)==255,"named button release reaches pull-up");
    for(unsigned i=0;i<8;++i){eii::ScannerControls panel;panel.setButton(eii::ScannerButton(i),true);
        require(panel.columns(0x28)==std::uint8_t(~(1u<<i)),"transport controls follow SCON0..7 on service7-4");}
    for(unsigned i=0;i<9;++i){
        eii::ScannerControls panel;panel.setButton(eii::ScannerButton(unsigned(eii::ScannerButton::PresetDefinition)+i),true);
        const unsigned pb[]={0x29,0x20,0x21};
        for(unsigned row=0;row<3;++row)
            require(panel.columns(pb[row])==(row==i/3?std::uint8_t(~(1u<<(5+i%3))):255),
                    "right module button reaches its own contact with no adjacent row leakage");
    }
    // LED latches clock on decoder RELEASE. No invented power-up LED states.
    require(!s.leds[0].known&&!s.leds[1].known&&!s.leds[2].known,"LED latch power-up unknown");
    for(unsigned port:{0x30u,0x31u,0xb0u,0xb1u,0x32u,0xb3u,0x3au,0xbbu}) {
        const unsigned index=(port&14)==0?2:(port&14)==2?1:0;
        const auto before=s.leds[index];s.driveBus(true,port,true,{0xa5,255});
        require(s.leds[index]==before,"active low LED strobe does not capture early");
        s.driveBus(false,0,false,{});
        require(s.leds[index].known==(index<2?63:255)&&s.leds[index].value==(index<2?0x25:0xa5),"LED release/aliases/width");
    }
    s.driveBus(true,0xb0,false,{});s.driveBus(false,0,false,{});require(!s.leds[2].known,"read of LED strobe captures unknown bus, not invented CPU data");
    // Real scanner-Z80 OUT/IN instructions, all canonical ADC aliases,
    // sustained bus strobes, matrix through PIO-B mode3 direction and LEDs.
    auto m=std::make_unique<eii::Machine>();m->main.rom[0]=0x76;
    for(unsigned ch=0;ch<8;++ch)m->scannerControls.adc.setInput(ch,{double(16+ch*24)*5/256,true});
    std::vector<std::uint8_t> program;
    for(unsigned ch=0;ch<8;++ch) {
        code(program,{0x3e,std::uint8_t(ch),0xd3,std::uint8_t(0x36|((ch&1)?128:0)|((ch&2)?1:0))});
        for(unsigned n=0;n<135;++n)program.push_back(0); //540 CPU clocks >64 ADC clocks
        code(program,{0xdb,std::uint8_t(0x34|((ch&1)?128:0)|((ch&2)?1:0)),0x32,std::uint8_t(ch),0x80});
    }
    code(program,{0x3e,0xcf,0xd3,0xdf,0x3e,0x10,0xd3,0xdf}); // B mode3, PB4 input
    code(program,{0x3e,0x29,0xd3,0xde,0xdb,0xb8,0x32,0x10,0x80}); // row41
    code(program,{0x3e,0x20,0xd3,0xde,0xdb,0x39,0x32,0x11,0x80}); // row32
    code(program,{0x3e,0x15,0xd3,0xb0,0x3e,0x2a,0xd3,0xb2,0x3e,0x81,0xd3,0xba,0x76});
    m->scannerControls.setContact(41,3,true);m->scannerControls.setContact(32,6,true);
    std::copy(program.begin(),program.end(),m->scanner.rom.begin());m->run(8000);
    for(unsigned ch=0;ch<8;++ch)require(m->scannerRam[ch]==16+ch*24,"real Z80 writes select/read all8 physical analog inputs through aliases");
    require(m->scannerControls.adc.starts==8&&m->scannerControls.adc.completed==8&&!m->scannerControls.adc.aborted,"one START per real held bus pulse, completion per conversion");
    require(m->scannerRam[0x10]==0xf7&&m->scannerRam[0x11]==0xbf,"real scanner firmware bus reads matrix using PIO direction and PB selector");
    require(m->scannerControls.leds[0]==eii::KnownByte{0x01,63}&&m->scannerControls.leds[1]==eii::KnownByte{0x2a,63}&&m->scannerControls.leds[2]==eii::KnownByte{0x15,255},"real OUT release captures all three panel LED latches");
    {
        using B=eii::ScannerButton;
        struct Pin {B button;unsigned latch,bit;};
        const Pin expected[]={{B::Play,0,0},{B::Record,0,1},{B::Stop,0,2},
            {B::Transpose,0,3},{B::Tune,0,4},{B::DynamicAllocation,0,5},
            {B::Setup,1,0},{B::ExternalClock,1,1},{B::Edit,1,2},{B::Define,1,3},
            {B::PresetDefinition,1,5},{B::Sample,2,7},{B::Disk,2,6},
            {B::VoiceDefinition,2,5},{B::Special,2,4},{B::VcaLfo,2,3},
            {B::Filter,2,2},{B::RealTimeControl,2,1},{B::Enter,2,0}};
        for(const auto pin:expected){
            eii::ScannerControls panel;
            for(unsigned port:{0xbau,0xb2u,0xb0u}){panel.driveBus(true,port,true,{255,255});panel.driveBus(false,0,false,{});}
            auto& values=panel.leds;
            const auto actual=eii::ScannerControls::ledPin(pin.button);
            require(actual.latch==int(pin.latch)&&actual.bit==pin.bit,"named LED follows IC2/IC3/IC1 and connector pin");
            require(eii::ScannerControls::ledState(values,pin.button)==eii::LogicLevel{true,false},"high LED cathode is off");
            const auto port=std::uint8_t(pin.latch==0?0xba:pin.latch==1?0xb2:0xb0);
            panel.driveBus(true,port,true,{std::uint8_t(~(1u<<pin.bit)),255});panel.driveBus(false,0,false,{});
            require(eii::ScannerControls::ledState(values,pin.button)==eii::LogicLevel{true,true},"low LED cathode is on");
            for(const auto other:expected)if(other.button!=pin.button)
                require(!eii::ScannerControls::ledState(values,other.button).high,"LEDs are independently routed");
            values[pin.latch].known&=std::uint8_t(~(1u<<pin.bit));
            require(!eii::ScannerControls::ledState(values,pin.button).known,"unknown latch bit stays unknown");
        }
        require(eii::ScannerControls::ledPin(B::Select).latch<0&&eii::ScannerControls::ledPin(B::Digit9).latch<0,
                "unwired buttons do not invent an LED");
    }
    require(m->scannerControls.unknownCpuReads==0&&m->scannerControls.overlappingBusCycles==0,"no unresolved ADC read or overlapping decoder in normal scanner program");
    // ADC EOC drives scanner CTC3, with real IM2 ACK/RETI. DI deliberately
    // holds the completed conversion's interrupt pending before EI.
    auto irq=std::make_unique<eii::Machine>();irq->main.rom[0]=0x76;
    irq->scannerControls.setVoltage(eii::ScannerAnalogInput::SliderA,3.75);
    std::vector<std::uint8_t> polling={0xf3,0x31,0xf0,0x87,0x3e,0x80,0xed,0x47,0xed,0x5e,
        0x3e,0xe0,0xd3,0xec,0x3e,0xd7,0xd3,0xef,0x3e,1,0xd3,0xef,
        0x3e,4,0xd3,0xb6,0x06,0xff,0x10,0xfe,0xfb,0x76};
    const unsigned halt=unsigned(polling.size()-1);code(polling,{0xc3,std::uint8_t(halt),0});
    std::copy(polling.begin(),polling.end(),irq->scanner.rom.begin());
    const std::uint8_t isr[]={0xf5,0xdb,0xb4,0x32,0x30,0x80,0xf1,0xfb,0xed,0x4d};
    std::copy(std::begin(isr),std::end(isr),irq->scanner.rom.begin()+0x100);
    irq->scannerRam[0xe6]=0;irq->scannerRam[0xe7]=1;
    irq->run(1000);
    require(irq->scannerControls.adc.completed==1&&(irq->scanner.ctc.chn[3].int_state&Z80CTC_INT_REQUESTED),"real ADC EOC requests CTC3 interrupt while DI");
    require(!irq->scanner.fetched[0x100],"DI blocks ADC interrupt entry");
    irq->run(4000);
    require((!eii::diagnosticsEnabled || irq->scanner.fetched[0x100]==1)&&irq->scannerRam[0x30]==192,"EOC to CTC3/IM2 ISR reads actual selected voltage exactly once");
    require(!(irq->scanner.ctc.chn[3].int_state&(Z80CTC_INT_REQUESTED|Z80CTC_INT_SERVICED)),"RETI clears ADC CTC service without periodic fake interrupts");
    std::cout<<"PASS: ADC0809 eight-channel/"<<transfers<<" code conversions,765 half-LSB edge probes, START/ALE/OE/abort/live SAR/data-before-EOC; all256 LS42 selectors x160 contacts, LED release; real Z80 all8 ADC aliases, PIO-B matrix and LED latches; nominal timing, no physical calibration\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
