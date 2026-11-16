#include "machine.h"
#include <iostream>
#include <memory>

// A bounded, sparse trace: omit port-B polling and panel-selection traffic so
// the first inter-CPU transaction is not hidden by the general trace limit.
int main(int argc,char** argv) {
    if(argc<3 || argc>4) {
        std::cerr<<"usage: pio_probe main.bin scanner.bin [cycles=40000000]\n";
        return 2;
    }
    try {
        auto m=std::make_unique<eii::Machine>();
        m->load(argv[1],argv[2]);
        std::uint64_t matched=0;
        std::cout<<"tick,cpu,write,port,data,pc,main_a_mode,main_a_ready,main_a_pending,scan_a_mode,scan_a_ready,scan_a_pending,main_b_ready,main_b_pending,scan_b_ready,scan_b_pending,main_to_scan,scan_to_main\n";
        m->trace=[&](bool scan,bool wr,std::uint16_t port,std::uint8_t data,std::uint16_t pc) {
            const auto p=std::uint8_t(port);
            const bool selected=scan?((p&0x70)==0x50):((p&0xf8)==0x30);
            if(!selected || (p&3)==2) return;
            if(matched++>=4096) return;
            const auto& a=m->main.pio[0];const auto& b=m->scanner.pio[0];
            std::cout<<std::dec<<m->ticks<<","<<(scan?"scanner":"main")<<","<<wr<<","<<std::hex
                <<unsigned(port)<<","<<unsigned(data)<<","<<pc<<std::dec<<","<<unsigned(a.mode)<<","<<a.ready<<","<<a.pending
                <<","<<unsigned(b.mode)<<","<<b.ready<<","<<b.pending
                <<","<<m->main.pio[1].ready<<","<<m->main.pio[1].pending
                <<","<<m->scanner.pio[1].ready<<","<<m->scanner.pio[1].pending
                <<","<<m->mainToScanTransfers<<","<<m->scanToMainTransfers<<"\n";
        };
        m->run(argc>3?std::stoull(argv[3]):40000000);
        std::cerr<<"PIO A/control events="<<matched<<" emitted="<<std::min<std::uint64_t>(matched,4096)
            <<" omitted="<<(matched>4096?matched-4096:0)<<"\n"<<m->report();
    } catch(const std::exception& e) {std::cerr<<"ERROR: "<<e.what()<<"\n";return 1;}
}
