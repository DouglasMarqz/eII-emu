#include "floppy_hfe.h"
#include <filesystem>
#include <fstream>
#include <iostream>
std::vector<std::uint8_t> load(const char* path) {
    std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("cannot open HFE");
    return {std::istreambuf_iterator<char>(f),{}};
}
int main(int argc,char** argv)try {
    if(argc!=2&&argc!=3){std::cerr<<"usage: hfe_probe input.hfe [complete-output.emuiifd]\n";return 2;}
    const eii::HfeImage image(load(argv[1]));
    unsigned validated=0;
    for(unsigned c=0;c<image.tracks();++c)for(unsigned s=0;s<2;++s) {
        const auto sector=image.sector(c,s);++validated;
        std::cout<<"SECTOR id="<<c*2+s<<" phase="<<sector.samplePhase<<" header="<<sector.headerCell<<" data="<<sector.dataCell<<" CRC=ok\n";
    }
    if(argc==3) {
        if(std::filesystem::exists(argv[2])&&std::filesystem::equivalent(argv[1],argv[2]))throw std::invalid_argument("output must differ from input");
        const auto data=image.extractEmuii(); // Validate everything before opening output.
        std::ofstream f(argv[2],std::ios::binary);if(!f)throw std::runtime_error("cannot open output");
        f.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));
        if(!f)throw std::runtime_error("output write failed");
        std::cout<<"EXTRACT bytes="<<data.size()<<" (payload import; original timing/gaps not replayed)\n";
    }
    std::cout<<"PASS sectors="<<validated<<" tracks="<<image.tracks()<<" bitrate="<<image.bitrate()<<"\n";return 0;
}catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<"\n";return 1;}
