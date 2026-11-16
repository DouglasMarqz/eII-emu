#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace eii {
// SA465 reference mechanics, not a factory identification of E-mu ZM302.
// CPU-clock time base4MHz. Explicit initial cylinder/rotor phase are test
// conditions, never an inferred power-up home position. No disk data yet.
class Sa465Drive {
public:
    static constexpr std::uint64_t period=800000,pulseWidth=8000;
    unsigned cylinder;
    bool zeroPhaseA=true,positionValid=true,disk=false,writeProtected=true;
    std::uint64_t steps=0,outSteps=0,inSteps=0,indexPulses=0,earlySteps=0,timingViolations=0;
    explicit Sa465Drive(unsigned initialCylinder,std::uint64_t initialPhase)
        :cylinder(initialCylinder),rotor_(initialPhase),phase_(initialPhase) {
        if(initialCylinder>=80||initialPhase>=period)throw std::invalid_argument("SA465 initial cylinder0..79 and phase0..799999 required");
    }
    void insertSensorDisk(bool protect=true) noexcept {disk=true;writeProtected=protect;readyPulses_=0;}
    void eject() noexcept {disk=false;readyPulses_=0;}
    void advance(std::uint64_t now) noexcept {
        const auto old=now_;now_=now;
        const auto end=motorN_?std::min(now,stopAt_):now;
        if(spinning_&&end>old) {
            const auto oldRotor=rotor_;rotor_+=end-old;
            // Crossings are sparse mechanical events. Between them the
            // exact rotor phase advances without repeated integer division.
            const bool crossing=rotor_>=nextBoundary_;
            if(crossing) {nextBoundary_=(rotor_/period+1)*period;phase_=rotor_%period;}
            else phase_+=end-old;
            if(crossing && disk && end>=stableAt_) {
                const auto begin=std::max(old+1,stableAt_);
                if(begin<=end) {
                    const auto first=oldRotor+(begin-old);
                    const auto n=rotor_/period-(first?((first-1)/period):0);
                    indexPulses+=n;readyPulses_=std::min<std::uint64_t>(2,readyPulses_+n);
                }
            }
        }
        if(motorN_&&now>=stopAt_)spinning_=false;
    }
    void drive(bool selectN,bool motorN,bool stepN,bool directionN,bool sideN,bool writeGateN,std::uint64_t now) noexcept {
        advance(now);
        if(!motorN&&motorN_) {
            if(!spinning_){spinning_=true;stableAt_=now+2000000;readyPulses_=0;}
            stopAt_=0;
        } else if(motorN&&!motorN_)stopAt_=now+12000000; //3-second electronic stop delay
        if(directionN!=directionN_){if(haveStep_&&now-lastStep_<4)++timingViolations;directionAt_=now;}
        if(sideN!=sideN_)sideUntil_=now+400; //100us side-select-to-read reference guard
        if(!stepN&&stepN_) {pulseAt_=now;pulseArmed_=!selectN&&writeGateN;}
        if(stepN&&!stepN_&&pulseArmed_&&!selectN&&writeGateN) {
            if(now<400000)++earlySteps; // explicit100ms power-up guard, not specified failure physics
            else if(now-pulseAt_<4||now-directionAt_<4||(haveStep_&&now-lastStep_<12000))++timingViolations;
            else {
                haveStep_=true;lastStep_=now;headUntil_=now+60000;++steps;
                if(directionN) {++outSteps;if(cylinder){--cylinder;zeroPhaseA=true;}else zeroPhaseA=!zeroPhaseA;}
                else {++inSteps;if(!cylinder&&!zeroPhaseA)zeroPhaseA=true;else if(cylinder<79)++cylinder;else positionValid=false;}
            }
        }
        if(stepN)pulseArmed_=false;
        selectN_=selectN;motorN_=motorN;stepN_=stepN;directionN_=directionN;sideN_=sideN;writeGateN_=writeGateN;
    }
    bool trackZeroN() const noexcept {return selectN_||!positionValid||now_<400000||cylinder||!zeroPhaseA;}
    bool indexN() const noexcept {
        if(selectN_)return true;
        if(!disk)return false; // manufacturer warns: empty drive can hold INDEX active
        return !spinning_||now_<stableAt_||!readyPulses_||phase_>=pulseWidth;
    }
    bool readyN() const noexcept {return selectN_||!disk||!spinning_||now_<stableAt_||readyPulses_<2;}
    bool protectN() const noexcept {return selectN_||!disk||!writeProtected;}
    bool dataSettled() const noexcept {return now_>=400000&&now_>=headUntil_&&now_>=sideUntil_;}
    bool readGateAllowed() const noexcept {return selected()&&disk&&!readyN()&&dataSettled()&&positionValid&&writeGateN_;}
    bool spinning() const noexcept {return spinning_;}
    bool selected() const noexcept {return !selectN_;}
    bool writeGateN()const noexcept{return writeGateN_;}
    bool sideN() const noexcept {return sideN_;}
    std::uint64_t phase() const noexcept {return phase_;}
private:
    std::uint64_t now_=0,rotor_=0,phase_=0,nextBoundary_=period,stableAt_=0,stopAt_=0,pulseAt_=0,directionAt_=0,lastStep_=0,headUntil_=0,sideUntil_=0,readyPulses_=0;
    bool selectN_=true,motorN_=true,stepN_=true,directionN_=true,sideN_=true,writeGateN_=true;
    bool spinning_=false,pulseArmed_=false,haveStep_=false;
};
class FloppyBus {
public:
    void enableEventTiming(bool enable=true) noexcept {eventTiming_=enable;controlValid_=false;}
    std::array<std::optional<Sa465Drive>,2> drives;
    void attach(unsigned drive,unsigned cylinder,std::uint64_t phase) {
        if(drive>=2)throw std::invalid_argument("EII drive0 or1 required");
        drives[drive].emplace(cylinder,phase);controlValid_=false;
    }
    void drive(std::uint8_t pb,bool sideN,bool writeGateN,std::uint64_t now) noexcept {
        if(eventTiming_) {
            const auto control=std::uint16_t(pb)|(std::uint16_t(sideN)<<8)|(std::uint16_t(writeGateN)<<9);
            const unsigned installed=unsigned(bool(drives[0]))|(unsigned(bool(drives[1]))<<1);
            if(controlValid_ && control==lastControl_ && installed==lastInstalled_) {advance(now);return;}
            controlValid_=true;lastControl_=control;lastInstalled_=installed;
        }
        // Sheet7-14: B4=DSEL0, B3=DSEL1, B2=STEP, B1=DIR.
        // Noninverting7407 buffers; common MOTOR_N=B4 AND B3.
        for(unsigned i=0;i<2;++i)if(drives[i])drives[i]->drive(bool(pb&(i?8:16)),(pb&24)==24,pb&4,pb&2,sideN,writeGateN,now);
    }
    void advance(std::uint64_t now) noexcept {for(auto& d:drives)if(d)d->advance(now);}
    bool trackZeroN() const noexcept {for(const auto& d:drives)if(d&&!d->trackZeroN())return false;return true;}
    bool indexN() const noexcept {for(const auto& d:drives)if(d&&!d->indexN())return false;return true;}
    bool readyN() const noexcept {for(const auto& d:drives)if(d&&!d->readyN())return false;return true;}
    bool protectN() const noexcept {for(const auto& d:drives)if(d&&!d->protectN())return false;return true;}
private:
    bool eventTiming_=false,controlValid_=false;
    std::uint16_t lastControl_=0;
    unsigned lastInstalled_=0;
};
}

