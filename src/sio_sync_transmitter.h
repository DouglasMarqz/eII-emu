#pragma once
#include <array>
#include <cstdint>
namespace eii {
// Z80 SIO x1, 8-bit Bisync/CRC16 subset. One clock is a falling TXCA edge.
// WR5 CRC-enable is sampled when the holding byte enters the shift register.
class SioSyncTransmitter {
public:
 bool empty=true,line=true,underrun=true;
 std::uint16_t crc=0;
 std::uint64_t clocks=0,characters=0,crcFrames=0,overwrites=0,unsupported=0;
 void write(std::uint8_t v)noexcept{if(!empty)++overwrites;holding_=v;empty=false;}
 void resetCrc()noexcept{crc=0;}
 void resetUnderrun()noexcept{underrun=false;}
 bool allSent()const noexcept{return empty&&(!bits_||kind_==Sync);}
 bool clock(const std::array<std::uint8_t,8>& wr)noexcept{
  ++clocks;if(!(wr[5]&8))return line=true;
  if((wr[4]&0xfd)!=0x10||(wr[5]&0x64)!=0x64){++unsupported;return line=true;}
  if(!bits_){
   if(!empty){shift_=holding_;empty=true;bits_=8;kind_=Data;++characters;
    if(wr[5]&1){crc^=holding_;for(unsigned i=0;i<8;++i)crc=std::uint16_t((crc>>1)^((crc&1)?0xa001:0));}}
   else if(!underrun&&(wr[5]&1)){shift_=crc;bits_=16;kind_=Crc;underrun=true;++crcFrames;}
   else{shift_=wr[syncSecond_?7:6];syncSecond_=!syncSecond_;bits_=8;kind_=Sync;underrun=true;}
  }
  line=shift_&1;shift_>>=1;--bits_;return line;
 }
private:
 enum Kind{Data,Crc,Sync};Kind kind_=Sync;
 std::uint8_t holding_=0;std::uint16_t shift_=0;unsigned bits_=0;bool syncSecond_=false;
};
}
