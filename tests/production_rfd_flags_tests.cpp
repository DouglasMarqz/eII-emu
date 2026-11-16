#include "production_bus.h"
#include <iostream>
#include <stdexcept>
int main(){
 for(unsigned word=0;word<0x1000000;++word){
  unsigned flags=0,pipeline=0;
  for(unsigned pin=0;pin<4;++pin)flags|=((word>>(23-pin))&1)<<pin;
  for(unsigned pin=0;pin<3;++pin)pipeline|=((word>>(22-pin))&1)<<pin;
  for(unsigned high:{0u,0xff000000u}){
   const auto x=word|high;
   if(eii::production::flagsFromBus(x)!=flags||eii::production::pipelineFlags(x)!=pipeline)throw std::runtime_error("RFD pin mapping differs");
  }
 }
 std::cout<<"PASS: all16777216RFD24bitwords, both0/FFupperbytes, exact4flag/3pipelinepins\n";
}
