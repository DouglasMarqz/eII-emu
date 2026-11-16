// Rebuild a firmware-written HDD image from its original + .EII banks; must match byte-exact.
// usage: eii_hdd_check ORIGINAL.img SAVED.img slot:file.eii ...
#include "eii_bank_format.h"
#include <cstdio>
#include <fstream>
#include <iterator>
static std::vector<std::uint8_t> rd(const char* p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){
    auto img=rd(argv[1]);const auto want=rd(argv[2]);
    for(int i=3;i<argc;++i){std::string a=argv[i];const auto c=a.find(':');const unsigned k=std::stoul(a.substr(0,c));const auto e=rd(a.substr(c+1).c_str());
        std::printf("slot %u free before: %d\n",k,int(eii::bank::hddSlotFree(img,k)));eii::bank::hddWriteSlot(img,k,e);
        const auto back=eii::bank::eiiFromHdd(std::span(img).subspan(eii::bank::hddDataBase+eii::bank::hddSlotBytes*k,eii::bank::hddSlotBytes));
        std::printf("slot %u '%s' roundtrip %s\n",k,eii::bank::hddSlotName(img,k).c_str(),back==e?"exact":"DIFF");}
    std::size_t diff=0;for(std::size_t i=0;i<img.size()&&i<want.size();++i)diff+=img[i]!=want[i];
    std::printf("image size %zu/%zu, differing bytes vs firmware-saved image: %zu\n",img.size(),want.size(),diff);
    return diff?1:0;
}
