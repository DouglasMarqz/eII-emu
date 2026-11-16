// Reproducible whole-controller comparison on a real seeded boot.
//
// Runs the reference ControllerTiming and the concrete production engine from
// the same DRAM/controller power-on seeds and compares the complete observable
// output: report, all64 register cells, memory/known/scanner hashes, PC and DRAM
// counters. Assets are supplied by the caller and never committed.
#include "machine.h"
#include "floppy_hfe.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
std::uint64_t fnv(const auto& data) {
    std::uint64_t h = 14695981039346656037ull;
    for (auto b : data) { h ^= b; h *= 1099511628211ull; }
    return h;
}
struct Result {
    std::string report;
    std::array<eii::Known24,64> registers{};
    std::uint64_t memory=0,known=0,scanner=0,pc=0;
    std::uint64_t dramReads=0,dramWrites=0,dramWait=0;
    bool invalid=false;
    double seconds=0;
};
Result run(const std::string& romFolder,const std::string& image,std::uint64_t ticks,
           unsigned phase,std::uint32_t dramSeed,std::uint32_t controllerSeed,bool production,bool normal) {
    auto m=std::make_unique<eii::Machine>();
    m->channelTimers.controllerTiming=eii::ControllerTiming(std::uint8_t(phase));
    m->channelTimers.enableControllerTiming();
    m->channelTimers.controllerTiming.enableRegisterWrites();
    const std::filesystem::path folder=romFolder;
    m->load(folder/"main.bin",folder/"scanner.bin",folder/"ic135.bin",folder/"ic137.bin");
    m->initializeDramPowerOn(dramSeed);
    if(production)m->enableProductionController(true);
    m->initializeControllerPowerOn(controllerSeed);
    if(normal)m->enableNormalRuntime();
    auto bytes=eii::readFile(image);
    if(bytes.size()>=8&&std::equal(bytes.begin(),bytes.begin()+8,"HXCPICFE"))
        bytes=eii::HfeImage(std::move(bytes)).extractEmuii();
    m->floppy.attach(0,37,400000);
    m->floppy.drives[0]->insertSensorDisk(true);
    m->diskRead=std::make_unique<eii::DiskReadPath>(87,8);
    m->diskRead->mount(0,std::make_shared<const eii::EmuiiImage>(std::move(bytes)),eii::EmuiiGap::Hxc2026);
    Result out;
    const auto begin=std::chrono::steady_clock::now();
    m->run(ticks);
    out.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    out.report=m->report();
    for(unsigned a=0;a<64;++a)out.registers[a]=m->channelTimers.controllerTiming.registerValue(std::uint8_t(a));
    out.memory=fnv(m->memory);out.known=fnv(m->dramKnown);out.scanner=fnv(m->scannerRam);
    out.pc=m->main.cpu.pc;
    out.dramReads=m->dramReadCycles;out.dramWrites=m->dramWriteCycles;out.dramWait=m->dramWaitClocks;
    out.invalid=m->channelTimers.controllerTiming.productionEngineInvalid();
    return out;
}
std::string usage() {
    return "usage: production_controller_probe ROMS IMAGE TICKS PHASE DRAM-SEED CONTROLLER-SEED reference|production|both [edge|normal]\n"
           "  ROMS must contain main.bin scanner.bin ic135.bin ic137.bin; IMAGE is an HFE or EMUIIFD.\n"
           "  Seeds accept decimal or0x hex. 'both' prints PASS only when the complete output agrees.\n"
           "  'normal' also enables the GUI's event/device/concrete runtime route on both engines.";
}
}
int main(int argc,char** argv) try {
    if(argc!=8 && argc!=9){std::cerr<<usage();return 2;}
    const std::uint64_t ticks=std::stoull(argv[3]);
    if(!ticks||ticks>1000000000)throw std::invalid_argument("ticks1..1billion");
    const unsigned phase=std::stoul(argv[4]);
    if(phase>15||argv[4][0]=='-')throw std::invalid_argument("phase0..15");
    const auto dramSeed=std::stoul(argv[5],nullptr,0);
    const auto controllerSeed=std::stoul(argv[6],nullptr,0);
    if(!dramSeed||dramSeed>UINT32_MAX||!controllerSeed||controllerSeed>UINT32_MAX)
        throw std::invalid_argument("nonzero32-bit seeds required");
    const std::string engine=argv[7];
    if(engine!="reference"&&engine!="production"&&engine!="both")throw std::invalid_argument("engine reference|production|both");
    const std::string route=argc==9?argv[8]:"edge";
    if(route!="edge"&&route!="normal")throw std::invalid_argument("route edge|normal");
    const bool normal=route=="normal";
    Result reference,production;
    if(engine!="production")reference=run(argv[1],argv[2],ticks,phase,std::uint32_t(dramSeed),std::uint32_t(controllerSeed),false,normal);
    if(engine!="reference")production=run(argv[1],argv[2],ticks,phase,std::uint32_t(dramSeed),std::uint32_t(controllerSeed),true,normal);
    const Result& out=engine=="reference"?reference:production;
    if(engine!="both") {
        std::cerr<<std::setprecision(9)<<"BENCH ticks="<<ticks<<" wall="<<out.seconds
                 <<" realtime="<<double(ticks)/4000000/out.seconds
                 <<" controller="<<engine<<" invalid="<<out.invalid<<"\n";
        std::cout<<out.report;
        for(unsigned a=0;a<64;++a)std::cout<<"RF "<<a<<" "<<out.registers[a].value<<" "<<out.registers[a].known<<"\n";
        std::cout<<"STATE_FNV memory="<<out.memory<<" known="<<out.known<<" scanner="<<out.scanner<<"\n";
        std::cout<<"FINAL pc="<<std::hex<<out.pc<<std::dec<<" dramR/W/WAIT="<<out.dramReads<<"/"<<out.dramWrites<<"/"<<out.dramWait<<"\n";
        return 0;
    }
    bool equal=reference.report==production.report && reference.registers==production.registers &&
        reference.memory==production.memory && reference.known==production.known &&
        reference.scanner==production.scanner && reference.pc==production.pc &&
        reference.dramReads==production.dramReads && reference.dramWrites==production.dramWrites &&
        reference.dramWait==production.dramWait;
    std::cerr<<std::setprecision(9)
             <<"BENCH reference wall="<<reference.seconds<<" realtime="<<double(ticks)/4000000/reference.seconds<<"\n"
             <<"BENCH production wall="<<production.seconds<<" realtime="<<double(ticks)/4000000/production.seconds<<"\n"
             <<"CONTROLLER_COMPARE "<<(equal?"EQUAL":"DIFFERENT")
             <<" production_invalid="<<production.invalid<<"\n";
    if(!equal) {
        if(reference.report!=production.report)std::cerr<<"report differs\n";
        if(reference.registers!=production.registers)std::cerr<<"register file differs\n";
        if(reference.memory!=production.memory||reference.known!=production.known||reference.scanner!=production.scanner)
            std::cerr<<"memory/known/scanner hash differs\n";
        if(reference.pc!=production.pc)std::cerr<<"PC differs\n";
        if(reference.dramReads!=production.dramReads||reference.dramWrites!=production.dramWrites||reference.dramWait!=production.dramWait)
            std::cerr<<"DRAM counters differ\n";
        return 1;
    }
    if(production.invalid) {
        std::cerr<<"FAIL: production engine left the fully known set\n";
        return 1;
    }
    std::cout<<"PASS: production controller equals the reference over "<<ticks
             <<" CPU ticks (controller phase "<<phase<<", DRAM seed "<<dramSeed
             <<", controller seed "<<controllerSeed<<")\n";
    return 0;
} catch(const std::exception& e) {
    std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;
}
