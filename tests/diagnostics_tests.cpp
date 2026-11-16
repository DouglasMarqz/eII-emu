#include "machine.h"
#include <iostream>
int main(){
    auto m=std::make_unique<eii::Machine>();
    const unsigned char code[]={0x3e,0x80,0xd3,0x78,0x32,0x00,0x21,0x76};
    std::copy(std::begin(code),std::end(code),m->main.rom.begin());
    m->scanner.rom[0]=0x76;unsigned traces=0;
    m->trace=[&](auto...){++traces;};m->run(200);
    if(m->memory[0x2100]!=0x80 || !m->controlVoltage.dacKnown || m->controlVoltage.dacCode!=0x80)return 1;
    if(bool(traces)!=eii::diagnosticsEnabled || bool(m->main.fetched[0])!=eii::diagnosticsEnabled ||
       bool(m->main.outs[0x78])!=eii::diagnosticsEnabled)return 2;
    std::cout<<"PASS: diagnostics="<<eii::diagnosticsEnabled<<"; actual Z80 memory/DAC side effects preserved\n";
}
