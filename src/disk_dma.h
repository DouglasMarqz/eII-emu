#pragma once
#include "z80.h"
#include <array>
#include <cstdint>

namespace eii {
// Z80DMA sequential byte-mode and continuous memory transfer subset. Register follow bytes and
// byte-count/address state are separate from bus cycles. No atomic block copy.
// Bus owner supplies BUSACK and waits; one tick is one4MHz system clock.
class DiskDma {
public:
    std::array<std::uint8_t,7> wr{};
    std::array<std::uint8_t,2> timing{};
    std::array<std::uint16_t,2> start{},address{};
    std::uint16_t length=0,count=0;
    std::uint8_t interruptControl=0,vector=0;
    bool enabled=false,irqEnabled=false,pending=false,servicing=false,granted=false;
    bool loaded=false,endOfBlock=false,forced=false;
    bool floppyIo=false;
    std::uint8_t mask=0,match=0;bool matchFound=false;
    bool hdIo=false; // optional HD-installed extension; default floppy behavior retained
    std::uint64_t writes=0,unsupported=0,grants=0,transfers=0,acks=0,holdClocks=0;
    void write(std::uint8_t value) noexcept {
        ++writes;
        if(next_<followCount_){set(follow_[next_++],value);return;}
        next_=followCount_=0;
        if((value&0x83)==0x83) { // WR6 commands disable bus requesting except87
            if(value!=0x87)enabled=false;
            switch(value) {
            case 0xc3:matchFound=false;irqEnabled=false;pending=servicing=false;forced=false;timing={};wr[5]&=~0x30;loaded=false;break;
            case 0xc7:timing[0]=0;break;
            case 0xcb:timing[1]=0;break;
            case 0xcf:matchFound=false;address[source()]=start[source()];destinationLoad_=true;count=0;remaining_=length?unsigned(length)+1:65537;forced=false;loaded=true;break;
            case 0xd3:count=0;remaining_=length?unsigned(length)+1:65537;forced=false;loaded=true;break;
            case 0x87:enabled=true;break;
            case 0xbf:if(!hdIo)++unsupported;break; // next CPU read selects status
            case 0x83:break;
            case 0xab:irqEnabled=true;break;
            case 0xaf:irqEnabled=false;break;
            case 0xa3:irqEnabled=false;pending=servicing=false;forced=false;break;
            case 0x8b:pending=false;endOfBlock=false;break;
            case 0xb3:forced=true;break;
            default:++unsupported;break;
            }
        } else if(!(value&0x80)&&(value&3)) {
            knownWr_|=1;wr[0]=value;if(value&8)add(AL);if(value&16)add(AH);if(value&32)add(LL);if(value&64)add(LH);
        } else if((value&0x87)==4) {knownWr_|=2;wr[1]=value;if(value&64)add(AT);}
        else if((value&0x87)==0) {knownWr_|=4;wr[2]=value;if(value&64)add(BT);}
        else if((value&0x83)==0x80) {wr[3]=value;if(value&8)add(MASK);if(value&16)add(MATCH);irqEnabled=value&32;if(value&64)enabled=true;}
        else if((value&0x83)==0x81) {knownWr_|=16;wr[4]=value;if(value&4)add(BL);if(value&8)add(BH);if(value&16)add(IC);}
        else if((value&0xc7)==0x82){knownWr_|=32;wr[5]=value;}
        else ++unsupported;
    }
    // Unsupported search, burst, continuous I/O, restart, pulse/Ready interrupts, early timing
    // and fixed destination counter quirks never silently transfer data.
    bool supported() const noexcept {
        return (knownWr_&0x37)==0x37&&knownLength_==3&&(knownStart_[destination()]&(((hdIo||floppyIo)&&isIo(destination()))?1:3))==(((hdIo||floppyIo)&&isIo(destination()))?1:3)&&
            (knownStart_[source()]&(isIo(source())?1:3))==(isIo(source())?1:3)&&
            ((wr[0]&3)==1||(floppyIo&&(wr[0]&3)==2))&&(transferMode()==0 || (transferMode()==1&&((hdIo||floppyIo)||(!isIo(0)&&!isIo(1)))))&&!(wr[5]&32)&&!(wr[3]&4)&&
            !(interruptControl&0xed)&&!(timing[0]&0xfc)&&!(timing[1]&0xfc)&&
            (timing[0]&3)!=3&&(timing[1]&3)!=3&&(((wr[1+destination()]>>4)&3)<2 || ((hdIo||floppyIo)&&isIo(destination())&&((wr[1+destination()]>>4)&3)==2));
    }
    bool request(bool readyN) const noexcept {
        return enabled&&loaded&&remaining_&&!granted&&supported()&&(forced||((wr[5]&8)?readyN:!readyN));
    }
    void clockReady(bool readyN) noexcept {readyHold_=granted&&!release_&&!destinationCycle_&&phase_==0&&transferMode()==1&&!forced&&!((wr[5]&8)?readyN:!readyN);}
    std::uint8_t status(bool readyN)const noexcept{return std::uint8_t((grants?1:0)|(((wr[5]&8)?readyN:!readyN)?0:2)|(pending?0:8)|(matchFound?0:16)|(endOfBlock?0:32));}
    void grant() noexcept {granted=true;release_=false;destinationCycle_=false;phase_=0;++grants;}
    std::uint64_t pins() const noexcept {
        if(!granted||release_)return 0;
        const auto port=destinationCycle_?destination():source();
        std::uint64_t p=address[port];
        if(destinationCycle_)Z80_SET_DATA(p,data_);
        if(phase_&&!readyHold_)p|=(isIo(port)?Z80_IORQ:Z80_MREQ)|(destinationCycle_?Z80_WR:Z80_RD);
        return p;
    }
    void tick(bool wait,std::uint8_t readData) noexcept {
        if(!granted)return;
        if(readyHold_){++holdClocks;return;}
        if(release_){granted=false;return;}
        if(wait&&phase_){++holdClocks;return;}
        const auto port=destinationCycle_?destination():source();
        if(++phase_<4u-(timing[port]&3u))return;
        phase_=0;
        if(!destinationCycle_) {
            data_=readData;increment(source());
            if((wr[0]&3)==2){matchFound|=((data_^match)&std::uint8_t(~mask))==0;finishByte();return;}
            const auto dst=destination();
            if(destinationLoad_){address[dst]=start[dst];destinationLoad_=false;}
            else increment(dst);
            destinationCycle_=true;
        } else {
            finishByte();
        }
    }
    bool requestLine() const noexcept {return pending&&irqEnabled&&!servicing;}
    std::uint64_t daisy(std::uint64_t pins) noexcept {
        if((pins&Z80_RETI)&&servicing){servicing=false;pins&=~Z80_RETI;}
        if(!(pins&Z80_IEIO))return pins;
        if(servicing){pins&=~Z80_IEIO;return pins;}
        if(requestLine()) {
            if((pins&(Z80_M1|Z80_IORQ))==(Z80_M1|Z80_IORQ)){
                Z80_SET_DATA(pins,vector);pending=false;servicing=true;++acks;
            }
            if(!(pins&Z80_RETI))pins&=~Z80_IEIO;
        }
        return pins;
    }
private:
    void finishByte()noexcept{
        ++transfers;
        if(--remaining_){++count;destinationCycle_=false;release_=transferMode()!=1;}
        else{release_=true;enabled=false;endOfBlock=true;if(interruptControl&2)pending=true;}
    }
    enum Field {AL,AH,LL,LH,BL,BH,AT,BT,IC,IV,PC,MASK,MATCH};
    std::array<Field,8> follow_{};unsigned next_=0,followCount_=0,remaining_=0,phase_=0;
    bool readyHold_=false;
    bool destinationCycle_=false,destinationLoad_=true,release_=false;
    std::uint8_t data_=0;
    std::uint8_t knownWr_=0,knownLength_=0;
    std::array<std::uint8_t,2> knownStart_{};
    void add(Field f) noexcept {follow_[followCount_++]=f;}
    void set(Field f,std::uint8_t v) noexcept {
        switch(f){
        case AL:knownStart_[0]|=1;start[0]=std::uint16_t((start[0]&0xff00)|v);break;
        case AH:knownStart_[0]|=2;start[0]=std::uint16_t((start[0]&255)|(unsigned(v)<<8));break;
        case BL:knownStart_[1]|=1;start[1]=std::uint16_t((start[1]&0xff00)|v);break;
        case BH:knownStart_[1]|=2;start[1]=std::uint16_t((start[1]&255)|(unsigned(v)<<8));break;
        case LL:knownLength_|=1;length=std::uint16_t((length&0xff00)|v);break;
        case LH:knownLength_|=2;length=std::uint16_t((length&255)|(unsigned(v)<<8));break;
        case AT:timing[0]=v;break;case BT:timing[1]=v;break;
        case IC:interruptControl=v;if(v&8)add(PC);if(v&16)add(IV);break;
        case IV:vector=v;break;case MASK:mask=v;break;case MATCH:match=v;break;default:++unsupported;break;
        }
    }
    unsigned source() const noexcept {return (wr[0]&4)?0:1;}
    unsigned transferMode() const noexcept {return (wr[4]>>5)&3;}
    unsigned destination() const noexcept {return source()^1;}
    bool isIo(unsigned p) const noexcept {return wr[p+1]&8;}
    void increment(unsigned p) noexcept {
        const auto mode=(wr[p+1]>>4)&3;
        if(mode<2)address[p]=std::uint16_t(address[p]+(mode?1:-1));
    }
};
}

