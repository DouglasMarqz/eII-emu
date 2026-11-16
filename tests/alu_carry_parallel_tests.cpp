#include "controller_alu.h"
#include <iostream>
#include <random>
#include <stdexcept>

// Frozen pre-optimization independent-bit recurrence. Compare raw value
// bits as well as masks, including representative bits marked unknown.
static eii::Known24 serial(eii::Known24 b,unsigned addend) {
    if(!addend)return {b.value&0xfffff,(b.known&0xfffff)|0x800000};
    eii::Known24 out{};eii::LogicLevel carry{true,false};
    for(unsigned bit=0;bit<20;++bit) {
        const eii::LogicLevel x{bool(b.known&(1u<<bit)),bool(b.value&(1u<<bit))};
        const bool a=addend&(1u<<bit);
        const auto sum=eii::xorLevel(x,carry);
        if(sum.high!=a)out.value|=1u<<bit;
        if(sum.known)out.known|=1u<<bit;
        carry=a?eii::logicOr(x,carry):eii::logicAnd(x,carry);
    }
    if(carry.known)out.known|=0x800000;
    if(carry.high!=bool(addend&0x100000))out.value|=0x800000;
    return out;
}
int main() try {
    std::uint64_t cases=0;
    const auto compare=[&](eii::Known24 b,unsigned addend) {
        const auto expected=serial(b,addend),actual=eii::addAluConstant(b,addend);
        if(actual.value!=expected.value || actual.known!=expected.known)
            throw std::runtime_error("parallel carry differs from serial gate recurrence");
        ++cases;
    };
    for(unsigned high:{0u,0xfff00u})for(unsigned pattern=0;pattern<6561;++pattern) {
        unsigned digits=pattern; eii::Known24 b{high,0xfff00};
        for(unsigned bit=0;bit<8;++bit) {
            const auto d=digits%3;digits/=3;
            if(d)b.known|=1u<<bit;
            if(d==2)b.value|=1u<<bit;
        }
        for(unsigned control=0;control<8;++control)
            compare(b,((control&2)?0xffcff:0)+((control&4)?0x300:0)+(control&1));
    }
    std::mt19937 rng(0xe112036c);
    for(unsigned i=0;i<300000;++i) {
        const eii::Known24 b{std::uint32_t(rng()),std::uint32_t(rng())};
        compare(b,rng()%0x100001);
    }
    std::cout<<"PASS:"<<cases<<" parallel/serial carry cases, exact raw values and masks\n";
} catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
