#pragma once
#include "controller_timing_types.h"
#include <array>
#include <bitset>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace eii {
struct AdcVoltage {double volts=0;bool known=false;};
// ADC0809 nominal 5V/0V pin model, not a measured silicon timing model.
// 64 clocks: SAR decisions at7,15,...63; output at63, EOC at64.
// The internal decision spacing and one-clock EOC-low delay are chosen
// nominal schedules within the published timing, not specified gate phases.
class Adc0809 {
public:
    std::array<AdcVoltage,8> inputs{};
    KnownByte channel{},result{};
    LogicLevel eoc{};
    std::uint64_t starts=0,completed=0,aborted=0,clocks=0;
    void setInput(unsigned index,AdcVoltage value) {
        if(index>=8 || (value.known && (!std::isfinite(value.volts)||value.volts<0||value.volts>5)))
            throw std::invalid_argument("ADC0809 pin requires channel0..7 and finite0..5V");
        inputs[index]=value;
    }
    void drive(bool ale,bool start,bool oe,KnownByte address) noexcept {
        if(ale&&!ale_)channel={std::uint8_t(address.value&7),std::uint8_t(address.known&7)};
        if(start&&!start_) {
            if(converting_)++aborted;
            ++starts;converting_=false;armed_=true;busyPending_=true;
            sar_.reset();sar_.set(0);elapsed_=0;
        }
        if(!start&&start_&&armed_) {converting_=true;armed_=false;elapsed_=0;}
        ale_=ale;start_=start;oe_=oe;
    }
    bool converting() const noexcept {return converting_;}
    KnownByte bus() const noexcept {return oe_?result:KnownByte{};}
    bool outputEnabled() const noexcept {return oe_;}
    void clock() noexcept {
        ++clocks;
        if(busyPending_) {eoc={true,false};busyPending_=false;}
        if(!converting_)return;
        ++elapsed_;
        if(elapsed_>=7 && (elapsed_-7)%8==0 && elapsed_<=63) {
            const unsigned bit=7-(elapsed_-7)/8;
            std::bitset<256> next;
            for(unsigned prefix=0;prefix<256;++prefix)if(sar_[prefix]) {
                const unsigned trial=prefix|(1u<<bit);
                // TI transfer: first transition +0.5LSB, then1LSB steps.
                const double threshold=(double(trial)-0.5)*(5.0/256.0);
                bool canLow=false,canHigh=false;
                for(unsigned ch=0;ch<8;++ch)if(!((ch^channel.value)&channel.known)) {
                    const auto v=inputs[ch];
                    canLow|=!v.known||v.volts<threshold;
                    canHigh|=!v.known||v.volts>=threshold;
                }
                if(canLow)next.set(prefix);
                if(canHigh)next.set(trial);
            }
            sar_=next;
            if(elapsed_==63) {
                bool first=true;KnownByte out{};
                for(unsigned code=0;code<256;++code)if(sar_[code]) {
                    if(first){out={std::uint8_t(code),255};first=false;}
                    else out.known&=std::uint8_t(~(out.value^code));
                }
                result=out;
            }
        }
        if(elapsed_==64) {converting_=false;eoc={true,true};++completed;}
    }
private:
    bool ale_=false,start_=false,oe_=false,armed_=false,converting_=false,busyPending_=false;
    unsigned elapsed_=0;
    std::bitset<256> sar_;
};

enum class ScannerAnalogInput : unsigned {External,Envelope,LeftWheel,RightWheel,SliderA,SliderB,SliderC,SliderD};
enum class ScannerButton : unsigned {
    Select,Stop,Record,Play,Define,Edit,ExternalClock,Setup,
    Digit0,Digit9,Digit8,Digit7,Digit6,Digit5,Digit4,Digit3,Digit2,Digit1,
    Transpose,Tune,DynamicAllocation,PresetDefinition,Sample,Disk,
    VoiceDefinition,Special,VcaLfo,Filter,RealTimeControl,Enter
};
class ScannerControls {
public:
    struct LedPin {int latch=-1;unsigned bit=0;};
    // Service7-2/7-3: cathodes are active low. Preset Definition is
    // IC3 QF (left latch1 bit5), routed through CN1 pin3, not IC1.
    static constexpr LedPin ledPin(ScannerButton b) noexcept {
        switch(b){
        case ScannerButton::Play:return {0,0};case ScannerButton::Record:return {0,1};
        case ScannerButton::Stop:return {0,2};case ScannerButton::Transpose:return {0,3};
        case ScannerButton::Tune:return {0,4};case ScannerButton::DynamicAllocation:return {0,5};
        case ScannerButton::Setup:return {1,0};case ScannerButton::ExternalClock:return {1,1};
        case ScannerButton::Edit:return {1,2};case ScannerButton::Define:return {1,3};
        case ScannerButton::PresetDefinition:return {1,5};
        case ScannerButton::Sample:return {2,7};case ScannerButton::Disk:return {2,6};
        case ScannerButton::VoiceDefinition:return {2,5};case ScannerButton::Special:return {2,4};
        case ScannerButton::VcaLfo:return {2,3};case ScannerButton::Filter:return {2,2};
        case ScannerButton::RealTimeControl:return {2,1};case ScannerButton::Enter:return {2,0};
        default:return {};
        }
    }
    static LogicLevel ledState(const std::array<KnownByte,3>& values,ScannerButton b) noexcept {
        const auto p=ledPin(b);if(p.latch<0)return {};
        const auto mask=std::uint8_t(1u<<p.bit);const auto v=values[unsigned(p.latch)];
        return {bool(v.known&mask),!(v.value&mask)};
    }
    Adc0809 adc;
    // UI/external-source initial positions, NOT internal latch reset values.
    // Wheels/sliders can be changed through setVoltage; unknown pins allowed.
    ScannerControls() {
        for(unsigned ch=0;ch<8;++ch)adc.setInput(ch,{0,true});
        adc.setInput(unsigned(ScannerAnalogInput::LeftWheel),{2.5,true});
    }
    void setVoltage(ScannerAnalogInput input,double volts) {adc.setInput(unsigned(input),{volts,true});}
    static bool validRow(unsigned row) noexcept {return row<16||row==32||row==33||row==40||row==41;}
    void setContact(unsigned sbus,unsigned scon,bool pressed) {
        if(!validRow(sbus)||scon>=8)throw std::invalid_argument("Invalid scanner SBUS/SCON contact");
        if(pressed)contacts_[sbus]|=1u<<scon;else contacts_[sbus]&=std::uint8_t(~(1u<<scon));
    }
    void setButton(ScannerButton button,bool pressed) {
        // Service7-2/7-4/7-8 connector wiring. Left-panel sheet's SBUS42
        // label at CN3 pin8 conflicts with digital sheet's SBUS40; pin8
        // is IC3 Y8 (row40), not an additional decoded row42.
        // Service7-4: each group runs toward increasing SCON, including
        // Select..Setup. Do not infer switch columns from LED bit numbers.
        static constexpr unsigned contacts[][2]={
            {40,0},{40,1},{40,2},{40,3},{40,4},{40,5},{40,6},{40,7},
            {41,0},{41,1},{41,2},{41,3},{41,4},{32,0},{32,1},{32,2},{32,3},{32,4},
            {33,0},{33,1},{33,2},{41,5},{41,6},{41,7},
            {32,5},{32,6},{32,7},{33,5},{33,6},{33,7}};
        const unsigned index=unsigned(button);
        if(index>=30)throw std::invalid_argument("Invalid scanner panel button");
        setContact(contacts[index][0],contacts[index][1],pressed);
    }
    // IC26/25 OR/inverter gates feed TWO decimal LS42 decoders. PB4 is RX;
    // PB6/7 are gain outputs. Neither changes the selected matrix row.
    static int selectedRow(std::uint8_t pb) noexcept {
        const unsigned address=(pb&7)|((pb&32)?8:0);
        if(address>=10)return -1;
        const unsigned bank=(pb&8)?8:0;
        return int(address<8?address+bank:32+(address-8)+bank);
    }
    std::uint8_t columns(std::uint8_t pb) const noexcept {
        const int row=selectedRow(pb);
        return row<0?255:std::uint8_t(~contacts_[unsigned(row)]);
    }
    // Raw IC47 output selection: A6=0, /IORQ active, /RD or /WR active;
    // A3..1 selects output. A7,A5,A4,A0 do not enter IC47's address.
    // Some aliases overlap CTC/PIO; CPU bus contention there is unmodeled.
    void driveBus(bool active,std::uint8_t port,bool write,KnownByte data) noexcept {
        const int next=active&&!(port&64)?int((port>>1)&7):-1;
        if(selected_>=0 && selected_!=next && (selected_==0||selected_==1||selected_==5)) {
            // IC47 Y0 -> CN3 pin28 -> IC1 right (8 bits);
            // Y1 -> pin27 -> IC3 left1; Y5 -> pin26 -> IC2 left0.
            const unsigned index=selected_==0?2:selected_==5?0:1;
            leds[index]=pending_;
            if(index<2){leds[index].value&=63;leds[index].known&=63;}
            ++ledWrites;
        }
        adc.drive(next==3,next==3,next==2,write?data:KnownByte{});
        if(next==0||next==1||next==5)pending_=write?data:KnownByte{};
        selected_=next;
    }
    // 500kHz source = one ADC cycle per8 CPU clocks. Phase is a nominal
    // simulation-origin choice, not a measured power-up divider phase.
    void advanceCpuTick() noexcept {if(++clockPhase_==8){clockPhase_=0;adc.clock();}}
    std::array<KnownByte,3> leds{}; // left0,left1 six bits; right eight, active low
    std::uint64_t ledWrites=0,unknownCpuReads=0,overlappingBusCycles=0;
private:
    std::array<std::uint8_t,42> contacts_{}; // verified diode-isolated contacts, released initially
    unsigned clockPhase_=0;
    int selected_=-1;
    KnownByte pending_{};
};
}
