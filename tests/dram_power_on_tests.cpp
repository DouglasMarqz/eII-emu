#include "machine.h"
#include <iostream>
#include <memory>
#include <stdexcept>
namespace {void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}}
int main()try {
    auto m=std::make_unique<eii::Machine>();
    require(m->dramKnown[0x98c0]==0,"default DRAM stays unknown");
    m->memory[0x2000]=0x5a;m->main.rom[0]=0xa5;
    m->initializeDramPowerOn(1);
    require(m->main.rom[0]==0xa5&&m->memory[0x2000]==0x5a&&m->dramKnown[0x2000]==0,"ROM/SRAM overlays untouched");
    require(m->memory[0x2800]==33&&m->memory[0x2801]==1&&m->memory[0x98c0]==229,"independent seed1 reference bytes");
    require(m->ticks==0&&m->dramWriteCycles==0&&m->dmaDramWrites==0,"initial state is not a CPU/DMA write");
    for(std::size_t a=0x2800;a<m->memory.size();++a)require(m->dramKnown[a]==255,"one concrete DRAM realization across full modeled range");
    const auto first=m->memory;
    m->initializeDramPowerOn(1);require(m->memory==first,"seed replay reproducible");
    m->initializeDramPowerOn(305419896);require(m->memory[0x98c0]==142&&m->memory!=first,"second independent startup realization");
    const auto second=m->memory;bool rejected=false;
    try{m->initializeDramPowerOn(0);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected&&m->memory==second,"zero seed rejected without mutation");
    m->run(1);rejected=false;
    try{m->initializeDramPowerOn(1);}catch(const std::logic_error&){rejected=true;}
    require(rejected&&m->memory==second,"cannot rewrite running memory as startup state");
    std::cout<<"PASS: unknown default, explicit seeded DRAM power-on, overlays, reproducibility, reference bytes, no bus writes, pre-first-tick guard\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
