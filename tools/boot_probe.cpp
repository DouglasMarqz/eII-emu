#include "machine.h"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
int main(int argc,char** argv) {
    if(argc<3) {std::cerr<<"usage: eii_probe main.bin scanner.bin [cycles=4000000] [trace.csv] [ic135.bin ic137.bin] [monitor-command]\n";return 2;}
    try {
        auto m=std::make_unique<eii::Machine>();
        m->load(argv[1],argv[2],argc>5?argv[5]:"",argc>6?argv[6]:"");
        std::ofstream trace;
        unsigned count=0;
        if(argc>4) {
            trace.open(argv[4]);if(!trace) throw std::runtime_error("Cannot create trace");
            trace<<"cpu,write,port,data,pc\n";
            m->trace=[&](bool scan,bool wr,std::uint16_t p,std::uint8_t v,std::uint16_t pc){
                if(count++<20000) trace<<(scan?"scanner":"main")<<","<<wr<<","<<std::hex<<unsigned(p)<<","<<unsigned(v)<<","<<pc<<"\n";
            };
        }
        auto cycles=argc>3?std::stoull(argv[3]):4000000;
        if(argc>7) {
            while(cycles && (m->serialTx.empty() || m->serialTx.back()!='>')) {
                auto warm=std::min<std::uint64_t>(40000,cycles);m->run(warm);cycles-=warm;
            }
            if(!cycles) throw std::runtime_error("Diagnostic monitor was not ready within the requested cycle budget");
            for(auto c:std::string(argv[7])+"\r") m->serialRx.push_back(std::uint8_t(c));
        }
        m->run(cycles);
        std::cout<<m->report();
        // Successful bounded execution is not a successful OS boot.
        return 0;
    } catch(const std::exception& e) {std::cerr<<"ERROR: "<<e.what()<<"\n";return 1;}
}
