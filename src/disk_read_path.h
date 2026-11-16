#pragma once
#include "floppy_image.h"
#include "floppy_mechanics.h"
#include "floppy_read_separator.h"
#include "disk_sio.h"
#include <functional>
#include <memory>

namespace eii {
// Opt-in nominal read/write media. Source time20MHz, mechanism time4MHz.
// Explicit width/idle/profile; preserve free-running rotor and separator
// history across selection/side/seek. No sector-to-memory or byte injection.
class DiskReadPath {
public:
    explicit DiskReadPath(std::uint64_t monostableEdges,unsigned readPulseEdges,std::uint64_t initialCpuTick=0)
        :separator(monostableEdges),pulseWidth_(readPulseEdges) {
        if(!pulseWidth_||pulseWidth_>=64)throw std::invalid_argument("read pulse width must be1..63 source edges");
        if(initialCpuTick>UINT64_MAX/5)throw std::invalid_argument("source epoch exceeds range");
        sourceEdges=writeSource_=initialCpuTick*5;
    }
    void mount(unsigned drive,std::shared_ptr<const EmuiiImage> image,EmuiiGap gap) {
        if(drive>=2||!image)throw std::invalid_argument("installed image and drive0..1 required");
        if(gap!=EmuiiGap::Esynthesist2010&&gap!=EmuiiGap::Hxc2026)throw std::invalid_argument("explicit FM profile required");
        media_[drive]=Media{};media_[drive].image=std::move(image);media_[drive].gap=gap;dirty[drive]=false;hasWriter_=bool(media_[0].writable)||bool(media_[1].writable);
    }
    void mountWritable(unsigned drive,std::shared_ptr<EmuiiImage> image,EmuiiGap gap){
        mount(drive,image,gap);media_[drive].writable=std::move(image);hasWriter_=true;
    }
    std::shared_ptr<const EmuiiImage> image(unsigned drive)const{return media_.at(drive).image;}
    bool writing()const noexcept{return media_[0].writing||media_[1].writing;}
    std::array<bool,2> dirty{};
    std::uint64_t writeFrames=0,writeErrors=0,blockedWrites=0;
    // true = persisted directly; false = working-copy change only. Exceptions
    // stop the owner before firmware verification can proceed on a host failure.
    std::function<bool(unsigned,unsigned,unsigned,std::span<const std::uint8_t>)> persistTrack;
    void eject(unsigned drive){if(drive>=2)throw std::out_of_range("drive0..1");media_[drive]={};dirty[drive]=false;hasWriter_=bool(media_[0].writable)||bool(media_[1].writable);}
    bool mounted(unsigned drive) const noexcept {return drive<2&&bool(media_[drive].image);}
    void enableEventTiming(bool enable=true) noexcept {eventTiming_=enable;}
    // Quiescent read/write path: no drive can gate data, the LS221 has no
    // pending expiry, the read line is idle and the write path is idle (the
    // same condition advanceWrites() treats as transition-free). Per-boundary
    // calls then change only the time epochs; skipQuiet() advances them.
    bool quiescent(const FloppyBus& bus,const DiskSio& sio) const noexcept {
        if(observeReceive||!readN_||!separator.idle())return false;
        for(const auto& d:bus.drives)if(d&&d->selected())return false;
        return !hasWriter_||(sio.writeGateN()&&!writing()&&!(sio.channels[0].wr[5]&8)&&sio.channels[0].tx.line);
    }
    void skipQuiet(std::uint64_t ticks) {
        if(ticks>UINT64_MAX/5)throw std::invalid_argument("source time exceeds range");
        const auto end=ticks*5;if(end<sourceEdges)throw std::invalid_argument("disk source time must be monotone");
        separator.advance(end,[](std::uint64_t,bool){});sourceEdges=end;writeSource_=end;
    }
    FloppyReadSeparator separator;
    std::uint64_t sourceEdges=0,outputPulses=0,selectedEdges=0;
    // Optional passive laboratory observation, after each actual RX clock.
    // No synthetic clocks/characters, writes or alternative receiver policy.
    std::function<void(std::uint64_t,bool,const DiskSio&)> observeReceive;
    void advanceCpuBoundary(std::uint64_t ticks,const FloppyBus& bus,DiskSio& sio) {
        if(ticks>UINT64_MAX/5)throw std::invalid_argument("source time exceeds range");
        const auto end=ticks*5;
        advanceWrites(end,bus,sio);
        if(end<sourceEdges)throw std::invalid_argument("disk source time must be monotone");
        if(end-sourceEdges>5)throw std::invalid_argument("advance every CPU boundary; cannot replay old drive state");
        std::array<bool,2> active{};
        for(unsigned i=0;i<2;++i)if(media_[i].image&&bus.drives[i]&&bus.drives[i]->readGateAllowed()) {
            const auto& d=*bus.drives[i];const unsigned id=d.cylinder*2+(d.sideN()?0:1);
            if(media_[i].id!=id||media_[i].cachedRevision!=media_[i].image->revision){media_[i].cachedRevision=media_[i].image->revision;media_[i].track=media_[i].image->format(d.cylinder,id&1,media_[i].gap);media_[i].id=id;}
            active[i]=true;
        }
        const auto sample=[&](std::uint64_t edge,bool data){sio.rxClock(data);if(observeReceive)observeReceive(edge,data,sio);};
        // No selected medium: only let an already-fired monostable expire.
        if(!active[0]&&!active[1]&&readN_) {separator.advance(end,sample);sourceEdges=end;return;}
        // One selected drive and a pulse/gap at least five source edges wide
        // can have at most one line transition inside this CPU interval.
        // Process that event at its exact original edge; advance the separator
        // directly between events. Passive per-clock laboratory observers keep
        // the original path, as do overlapping drives and narrow pulse tests.
        if(eventTiming_ && !observeReceive && pulseWidth_>=5 && pulseWidth_<=59 && active[0]!=active[1]) {
            const auto clocks=end-sourceEdges;if(!clocks)return;
            const unsigned drive=active[0]?0:1;
            const auto phaseEnd=bus.drives[drive]->phase()*5;
            const auto phaseFirst=(phaseEnd+EmuiiFmTrack::revolutionEdges-(clocks-1))%EmuiiFmTrack::revolutionEdges;
            const auto readAt=[&](std::uint64_t phase) {
                return !(phase%64<pulseWidth_ && media_[drive].track.transitions[unsigned(phase/64)]);
            };
            const bool first=readAt(phaseFirst),last=readAt(phaseEnd);
            const auto line=[&](std::uint64_t edge,bool level) {
                if(!readN_ && level){separator.pulse(edge,sample);++outputPulses;}
                else separator.advance(edge,sample);
                readN_=level;
            };
            if(readN_!=first)line(sourceEdges+1,first);
            if(first!=last) {
                const auto edge=first?end-phaseEnd%64:sourceEdges+1+(pulseWidth_-phaseFirst%64);
                line(edge,last);
            }
            separator.advance(end,sample);selectedEdges+=clocks;sourceEdges=end;return;
        }
        while(sourceEdges<end) {
            ++sourceEdges;bool nextReadN=true;
            for(unsigned i=0;i<2;++i)if(active[i]) {
                ++selectedEdges;
                const auto phase=(bus.drives[i]->phase()*5+EmuiiFmTrack::revolutionEdges-(end-sourceEdges)%EmuiiFmTrack::revolutionEdges)%EmuiiFmTrack::revolutionEdges;
                const auto slot=unsigned(phase/64);
                if(phase%64<pulseWidth_&&media_[i].track.transitions[slot])nextReadN=false;
            }
            if(!readN_&&nextReadN){separator.pulse(sourceEdges,sample);++outputPulses;}
            else separator.advance(sourceEdges,sample);
            readN_=nextReadN;
        }
    }
private:
    struct Media {
        std::shared_ptr<const EmuiiImage> image;
        std::shared_ptr<EmuiiImage> writable;
        bool writing=false,valid=false,firstClock=false;
        unsigned writeCylinder=0,writeSide=0;
        std::vector<std::uint8_t> written;
        std::uint64_t cachedRevision=0,unsupportedStart=0;
        EmuiiGap gap=EmuiiGap::Esynthesist2010;
        EmuiiFmTrack track;
        unsigned id=160;
    };
    std::array<Media,2> media_{};
    std::uint64_t writeSource_=0;
    bool hasWriter_=false;
    [[noreturn]] void writeFailure(Media& m){++writeErrors;m.written.clear();throw std::runtime_error("Floppy write blocked, incomplete or CRC-invalid; no payload sector committed.");}
    void finishWrite(unsigned drive){
        auto& m=media_[drive];m.writing=false;
        if(!m.valid||!m.writable){writeFailure(m);}
        const unsigned first=m.firstClock?0:1;
        if(m.written.size()<=first+2*8*(EmuiiImage::sectorBytes+6)){writeFailure(m);}
        const unsigned bits=unsigned((m.written.size()-first)/2);
        for(unsigned i=first;i+1<m.written.size();i+=2)if(m.written[i]!=1){writeFailure(m);}
        const auto byte=[&](unsigned p){std::uint8_t b=0;for(unsigned k=0;k<8;++k)b|=m.written[first+2*(p+k)+1]<<k;return b;};
        std::array<std::uint8_t,EmuiiImage::sectorBytes+2> data{};
        for(unsigned p=32;p+16+data.size()*8<=bits;++p){
            if(byte(p)!=0xfa||byte(p+8)!=0x96)continue;
            if(byte(p-8)||byte(p-16)||byte(p-24)||byte(p-32))continue;
            for(unsigned i=0;i<data.size();++i)data[i]=byte(p+16+i*8);
            if(emuiiCrc(data))continue;
            m.writable->replacePayload(m.writeCylinder,m.writeSide,std::span(data).first(EmuiiImage::sectorBytes));
            dirty[drive]=true;++writeFrames;m.written.clear();
            if(persistTrack&&persistTrack(drive,m.writeCylinder,m.writeSide,std::span(data).first(EmuiiImage::sectorBytes)))dirty[drive]=false;
            return;
        }
        writeFailure(m);
    }
    void advanceWrites(std::uint64_t end,const FloppyBus& bus,DiskSio& sio){
        // A disabled transmitter with an idle write gate has no functional
        // transitions. Keep the absolute clock epoch: enabling it later still
        // resumes on the free-running IC63 edge, never on the mount boundary.
        // Preserve one final clock when TXDA has not yet returned to marking.
        if(!hasWriter_ || (sio.writeGateN()&&!writing()&&
           !(sio.channels[0].wr[5]&8)&&sio.channels[0].tx.line)){
            writeSource_=end;return;
        }
        for(unsigned i=0;i<2;++i){auto& m=media_[i];const auto& drive=bus.drives[i];
            const bool gate=drive&&drive->selected()&&!drive->writeGateN();
            if(m.writing&&!gate)finishWrite(i);
            if(gate&&!m.writing){m.writing=true;m.writeCylinder=drive->cylinder;m.writeSide=drive->sideN()?0:1;
                m.valid=bool(m.writable)&&!drive->writeProtected&&!drive->readyN()&&drive->dataSettled()&&drive->positionValid;
                m.written.clear();m.unsupportedStart=sio.channels[0].tx.unsupported;if(!m.valid)++blockedWrites;
            }
            if(m.writing&&drive&&(drive->cylinder!=m.writeCylinder||(drive->sideN()?0u:1u)!=m.writeSide||drive->writeProtected||sio.channels[0].tx.unsupported!=m.unsupportedStart))m.valid=false;
        }
        while(writeSource_<end){const auto base=writeSource_/128*128,phase=writeSource_%128;
            const auto next=base+(phase<48?48:phase<112?112:128);if(next>end)break;writeSource_=next;
            if(next%128==0){sio.txClock();continue;}
            const bool clock=next%128==48;
            //7-15: IC63 divides2.5MHz by16 for TXCA. IC129/7407 produce
            // clock/data pulses at counter6/14: FM slots at48/112 source edges.
            for(auto& m:media_)if(m.writing&&m.valid){if(m.written.empty())m.firstClock=clock;
                if(m.written.size()>=250000)m.valid=false;else m.written.push_back(clock||sio.channels[0].tx.line);}
        }
        writeSource_=end;
    }
    bool eventTiming_=false;
    unsigned pulseWidth_;
    bool readN_=true;
};
}

