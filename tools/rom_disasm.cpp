#define CHIPS_UTIL_IMPL
#include "z80dasm.h"
#include "machine.h"
#include <cstdio>
struct Input {std::vector<std::uint8_t> bytes;std::size_t pos=0;};
int main(int argc,char** argv) {
    if(argc!=4)return 2;
    try {
        Input in{eii::readFile(argv[1]),std::stoul(argv[2],nullptr,0)};
        auto end=std::stoul(argv[3],nullptr,0);
        while(in.pos<end && in.pos<in.bytes.size()) {
            std::printf("%04zx: ",in.pos);
            z80dasm_op(std::uint16_t(in.pos),[](void* p){auto& i=*static_cast<Input*>(p);return i.pos<i.bytes.size()?i.bytes[i.pos++]:std::uint8_t(0);},
                [](char c,void*){std::putchar(c);},&in);
            std::putchar('\n');
        }
    } catch(const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
