#include "controller_timing.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
static void load(std::array<std::uint8_t,512>& dst,const char* path){std::ifstream f(path,std::ios::binary);if(!f.read((char*)dst.data(),512)||f.peek()!=std::char_traits<char>::eof())throw std::runtime_error("PROM must contain512 bytes");}
int main(int argc,char** argv){try{
    if(argc!=3){std::cerr<<"usage: dram_probe ic135.bin ic137.bin\n";return 2;}
    eii::PromLatch image;load(image.upper,argv[1]);load(image.lower,argv[2]);
    unsigned totalWriteClocks=0,totalReadClocks=0;
    for(bool unknownRequests:{false,true})for(unsigned phase=0;phase<16;++phase){
        std::vector<std::uint8_t> bytes(0x80000),known(0x80000);
        eii::ControllerTiming c(phase);c.loadProms(image);c.attachCpuDram({&bytes,&known});c.reset(false);
        std::array<eii::LogicLevel,8> requests;requests.fill({!unknownRequests,false});
        auto clock=[&](){c.fallingClock();c.risingClock(requests);c.cpuClock();};
        for(unsigned i=0;i<200;++i)clock();
        const unsigned address=(phase%8)*65536+0x5234;const std::uint8_t value=0x51+phase;
        c.driveCpuBus({true,false},{true,true});c.driveDramCpu(true,address,{value,255},false);
        bool wrote=false,writeReleased=false;
        for(unsigned n=1;n<200;++n){clock();std::uint32_t a;eii::KnownByte data;
            if(!wrote && c.cpuDramWrite(a,data)){if(a!=address||data.value!=value)throw std::runtime_error("write address/data mismatch");bytes[a]=data.value;known[a]=data.known;wrote=true;}
            if(wrote && c.observedWait.known&&c.observedWait.high){totalWriteClocks+=n;writeReleased=true;break;}
        }
        if(!wrote || !writeReleased)throw std::runtime_error("write did not converge across request histories");
        c.driveCpuBus({true,true},{true,true});c.driveDramCpu(false,address,{0,255},true);
        for(unsigned i=0;i<20;++i)clock();
        c.driveCpuBus({true,false},{true,false});c.driveDramCpu(true,address,{0,255},true);
        bool read=false;
        for(unsigned n=1;n<200;++n){clock();auto data=c.cpuDramReadData();
            if(c.cpuDramReady()&&c.observedWait.known&&c.observedWait.high&&data.known==255){if(data.value!=value)throw std::runtime_error("IC103 read mismatch");read=true;totalReadClocks+=n;break;}
        }
        if(!read)throw std::runtime_error("IC103 read did not converge across request histories");
    }
    std::cout<<"PASS: supplied PROM CPU write/read timing and IC103 across16 explicit initial phases/all8 banks with quiet AND unknown CHRQ; total source clocks write="<<totalWriteClocks<<" read="<<totalReadClocks<<"\n";
    std::cout<<"Fixture covers both known quiet and unknown CHRQ inputs; not full Machine startup/keyboard/sample arbitration. No firmware patched or embedded.\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
