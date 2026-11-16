#pragma once
// Concrete (production) upper controller path.
//
// Mirrors the single-branch fast path of ControllerUpperPaths: with a known
// pipeline, request and capture there is no branch enumeration, no consensus
// merge and no DAC merge. The signal flow, PROM address and event order are
// identical to the reference.
#include <cstdint>
#include "controller_prom.h"
#include "production_dac.h"
#include "production_dram.h"
#include "production_request.h"

namespace eii::production {
class UpperPath {
public:
    std::uint8_t upper=0;    // upper PROM latch: bits 7..6 feedback, 5..0 output
    std::uint8_t pipeline=0; // IC152 pipeline bits
    bool request=false,ack=false,delayed9=false;
    RequestHandshake cpu;
    DramPort dram;
    DacRegisters dac;

    static std::uint16_t word(std::uint8_t upperLatches,std::uint16_t lower) noexcept {
        return std::uint16_t((unsigned(upperLatches)<<8)|(lower&0xff));
    }
    void driveCpu(bool inactive) noexcept {cpu.drive(inactive);}
    void cpuClock() noexcept {cpu.cpuClock();}
    void reset() noexcept {
        request=false;
        dac.drive(0,ack,dram.channelLatch,dram.channelKnown);
    }
    void driveDram(bool active,std::uint32_t address,std::uint8_t data,bool read) noexcept {
        dram.cpuBus(active,address,data,read);
    }
    void falling(std::uint16_t lower,const std::uint8_t* memory) noexcept {
        dram.falling(word(upper,lower),delayed9,memory);
    }
    // Same order as the reference fast path: DRAM rising with the old word,
    // PROM lookup, ack/delayed9 from the old latch, pipeline from the old CPU
    // request, completion clock, DRAM settle with the new word, DAC drive.
    void rising(const PromConsensus& prom,unsigned phase,unsigned channel,
                bool capture,bool input,bool resetHeld,bool readInactive,
                std::uint16_t lower,std::uint32_t sum,const std::uint8_t* memory) noexcept {
        const std::uint8_t oldUpper=upper;
        const bool oldRequest=request;
        const bool oldCpuRequest=cpu.requestToPipeline();
        const unsigned q=pipeline&3;
        dram.rising(word(upper,lower),sum,memory);
        const unsigned address=((oldUpper&192)<<1)|((phase&7)<<4)|
            ((q&1)<<3)|((q&2)<<1)|((channel&8)?2:0)|unsigned(oldRequest);
        upper=prom.lookupKnown(address);
        ack=(oldUpper&1)!=0;
        delayed9=(oldUpper&2)!=0;
        pipeline=std::uint8_t(unsigned(oldCpuRequest)|(unsigned(readInactive)<<1));
        cpu.completionClock((upper&2)!=0);
        dram.settle(word(upper,lower),memory);
        const unsigned nextChannel=resetHeld?0:phase==15?((channel&8)?0:channel+1):channel;
        dac.drive(std::uint8_t(nextChannel),ack,dram.channelLatch,dram.channelKnown);
        request=resetHeld?false:capture?input:oldRequest;
    }
};
}
