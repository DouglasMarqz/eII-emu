#pragma once
#include "controller_timing.h"
#include <stdexcept>
#include <source_location>
static void check(bool b,std::source_location at=std::source_location::current()){if(!b)throw std::runtime_error(std::string("controller concrete/reference mismatch at ")+at.file_name()+":"+std::to_string(at.line()));}
namespace eii {
struct ControllerRuntimeTestAccess {
    static void compare(const ControllerTiming& a,ControllerTiming& b) {
        b.materializeVoiceState();
        check(a.count_==b.count_ && a.lowAperture_==b.lowAperture_ && a.risingEdges==b.risingEdges);
        for(unsigned i=0;i<a.count_;++i) {
            const auto& x=a.paths_[i];const auto& y=b.paths_[i];
            check(x.phase==y.phase && x.channel==y.channel && x.shift==y.shift &&
                  x.captured==y.captured && x.pipeline==y.pipeline && x.cpu==y.cpu &&
                  x.control.value==y.control.value && x.control.known==y.control.known &&
                  x.acknowledgeEnable==y.acknowledgeEnable && x.alu==y.alu &&
                  x.sampledRfd==y.sampledRfd && x.sameUcd4Sample==y.sameUcd4Sample &&
                  x.data.low20==y.data.low20 && x.data.flags==y.data.flags &&
                  x.data.ucd4b==y.data.ucd4b && x.data.ucd9==y.data.ucd9 &&
                  x.writePort.pending==y.writePort.pending && x.writePort.flag==y.writePort.flag &&
                  x.writePort.mpwenN==y.writePort.mpwenN && x.registerWrites==y.registerWrites);
            for(unsigned j=0;j<64;++j)check(x.registers.read({std::uint8_t(j),63})==y.registers.read({std::uint8_t(j),63}));
            check(x.upper.branches.size()==y.upper.branches.size());
            for(unsigned j=0;j<x.upper.branches.size();++j) {
                const auto& u=x.upper.branches[j];const auto& v=y.upper.branches[j];
                check(u.sameState(v) && u.dac.codes==v.dac.codes &&
                      u.dram.reads==v.dram.reads && u.dram.writes==v.dram.writes &&
                      u.dram.readCaptures==v.dram.readCaptures && u.dram.sampleReads==v.dram.sampleReads);
            }
        }
    }
};
}
