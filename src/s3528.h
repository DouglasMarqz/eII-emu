#pragma once
#include "am6072.h"
#include "channel_requests.h"
#include "s3528_curve_coefficients.h"

namespace eii::analog {
// AMI1984 second edition pp4.156..165. CE is an analog three-level pin.
class S3528Control {
public:
    enum class Enable {HoldVdd,LoadGround,TransparentVss,Unknown};
    static constexpr std::array<unsigned,64> divisors={
        2048,895,447,298,224,179,149,128,112,99,188,358,90,87,85,78,
        89,81,74,69,64,60,56,53,50,47,61,58,52,46,44,40,
        45,43,41,39,37,36,34,33,32,31,38,35,22,20,18,16,
        30,29,28,27,26,15,25,24,14,23,12,10,9,6,5,4};
    void drive(Enable ce,KnownByte data) noexcept {
        data=bits(data);
        if(ce==Enable::Unknown){latched_={};active_={};}
        else if(ce==Enable::LoadGround)latched_=data; // active ROM holds old word
        else if(ce==Enable::TransparentVss)active_=data;
        else if(previous_==Enable::LoadGround)active_=latched_;
        else if(previous_!=Enable::HoldVdd)active_={}; // unspecified transition
        previous_=ce;
    }
    // Board -CSWF: ground while selected, VDD after the write strobe.
    void completedWrite(std::uint8_t data) noexcept {
        drive(Enable::LoadGround,{data,255});drive(Enable::HoldVdd,{63,255});++writes;
    }
    KnownByte activeCode() const noexcept {return active_;}
    KnownSignal cutoff(double masterHz=5e6) const {
        if(!std::isfinite(masterHz)||masterHz<=0)throw std::invalid_argument("Invalid S3528 clock");
        unsigned n=0;
        for(unsigned code=0;code<64;++code)if(!((code^active_.value)&active_.known)) {
            if(!n)n=divisors[code];else if(n!=divisors[code])return {};
        }
        return {masterHz/(40*n),true};
    }
    std::uint64_t writes=0;
private:
    static KnownByte bits(KnownByte b) noexcept {return {std::uint8_t(b.value&63),std::uint8_t(b.known&63)};}
    KnownByte latched_{},active_{};
    Enable previous_=Enable::HoldVdd;
};
// Integer 5MHz-master half-edge divider for laboratory timing. Divider phase
// at power-up/program changes is unspecified by the source: retain unknown
// rather than invent a hardware reset. Alignment is an explicit experiment.
class S3528Clock {
public:
    void alignForExperiment(KnownByte code,unsigned halfEdgePhase=0) {
        divisor_=decode(code);
        if(!divisor_||halfEdgePhase>=divisor_)throw std::invalid_argument("Invalid S3528 experiment phase/code");
        phase_=halfEdgePhase;known_=true;
    }
    bool observeCode(KnownByte code) noexcept {
        const unsigned n=decode(code);
        if(!known_||!n||n!=divisor_)known_=false;
        return known_;
    }
    LogicLevel masterHalfEdge(KnownByte code) noexcept {
        if(!observeCode(code))return {};
        const unsigned n=divisor_;
        if(++phase_==n){phase_=0;return {true,true};}
        return {true,false};
    }
    bool phaseKnown() const noexcept {return known_;}
private:
    static unsigned decode(KnownByte code) noexcept {
        if((code.known&63)!=63)return 0;
        return S3528Control::divisors[code.value&63];
    }
    unsigned divisor_=0,phase_=0;
    bool known_=false;
};
enum class S3528Profile {Ami1993TypicalFit,GenericElliptic};
inline constexpr const char* s3528ProfileName(S3528Profile profile) noexcept {
    return profile==S3528Profile::Ami1993TypicalFit?"ami1993_typical_curve_fit":"generic_elliptic";
}
// Explicit NOMINAL approximation; neither profile recovers chip internals.
// Input samples at80fc, pair average/core at40fc. New default is a stable
// seven-pole/two-notch fit to AMI1993 amplitude AND group-delay curves.
// Generic elliptic remains selectable for comparison. No hardware calibration.
class S3528NominalResponse {
public:
    S3528NominalResponse():S3528NominalResponse(S3528Profile::Ami1993TypicalFit) {}
    explicit S3528NominalResponse(S3528Profile profile):profile_(profile) {}
    S3528Profile profile() const noexcept {return profile_;}
    void resetKnownZero() noexcept {state_={};first_={0,true};output_={0,true};known_=true;pending_=false;}
    KnownSignal output() const noexcept {return output_;}
    void invalidate() noexcept {known_=false;output_={};pending_=false;first_={};}
    KnownSignal sampleAtTwiceFilterClock(KnownSignal input) {
        if(input.known&&!std::isfinite(input.value))throw std::invalid_argument("Invalid S3528 input");
        if(!input.known)known_=false;
        if(!pending_){first_=input;pending_=true;return output_;}
        pending_=false;
        if(!known_||!first_.known||!input.known){output_={};return output_;}
        double x=(first_.value+input.value)*0.5;
        for(unsigned i=0;i<4;++i) {
            const auto& c=(profile_==S3528Profile::Ami1993TypicalFit?s3528Ami1993Sos:genericSos_)[i];auto& z=state_[i];
            const double y=c[0]*x+z[0];z[0]=c[1]*x-c[3]*y+z[1];z[1]=c[2]*x-c[4]*y;x=y;
        }
        output_={x,true};return output_;
    }
private:
    // scipy1.17 signal.ellip(7,0.1,51,0.05,output='sos'), a0=1 omitted.
    static constexpr double genericSos_[4][5]={
        {0.001292291320081437,0.001292291320081437,0,-0.9214374628410316,0},
        {1,-1.8804886500479394,0.9999999999999998,-1.8775540279806335,0.8899425161328525},
        {1,-1.9524335714820789,1,-1.9293286599678718,0.9506729600224979},
        {1,-1.9636438637493863,1,-1.9615078076392984,0.9872213620409969}};
    S3528Profile profile_;
    std::array<std::array<double,2>,4> state_{};
    KnownSignal first_{},output_{};
    bool known_=false,pending_=false;
};
}
