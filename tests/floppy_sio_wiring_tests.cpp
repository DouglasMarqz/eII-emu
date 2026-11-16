#include "machine.h"
#include <iostream>
#include <memory>
#include <stdexcept>
int main()try {
    auto m=std::make_unique<eii::Machine>(0);
    m->floppy.attach(0,0,400000);
    m->floppy.drives[0]->insertSensorDisk(false);
    m->main.pio[1].mode=0;
    m->main.pio[1].output=0x0c;
    m->run(4000000);
    const auto& drive=*m->floppy.drives[0];
    if(drive.readyN()||!drive.indexN())throw std::runtime_error("fixture must be ready between index pulses");
    if((m->diskSio.channels[0].live&0x28)!=8)throw std::runtime_error("sheet7-15 READY must drive DCD, INDEX must drive CTS");
    std::cout<<"PASS physical INDEX->CTSA / READY->DCDA wiring with writable disk\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
