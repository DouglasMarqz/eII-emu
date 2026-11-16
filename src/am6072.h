#pragma once
#include <array>
#include <cmath>
#include <stdexcept>
#include "controller_timing_types.h"

namespace eii::analog {
struct KnownSignal {
    double value=0;
    bool known=false;
};
struct PlaybackInputs {
    std::array<KnownByte,8> codes{};
    std::array<KnownSignal,8> amplitudeCv{};
};
// AMD 1979 Linear/Interface, printed3-55..58, Table3. Raw SB/chord/step
// pins, not complemented G.711 transport bytes. Decode mode only.
class Am6072 {
public:
    struct DecodePins {KnownSignal iodPlusSink{},iodMinusSink{};};
    static constexpr unsigned magnitudeUnits(std::uint8_t code) noexcept {
        const unsigned chord=(code>>4)&7,step=code&15;
        return ((2*step+33)<<chord)-33;
    }
    static constexpr int signedUnits(std::uint8_t code) noexcept {
        const int n=int(magnitudeUnits(code));return (code&128)?n:-n;
    }
    static double signedDecodeCurrent(std::uint8_t code,double referenceAmps) {
        if(!std::isfinite(referenceAmps)||referenceAmps<0||referenceAmps>1e-3)
            throw std::invalid_argument("Am6072 reference current outside ideal model range");
        // Table3: 8031 units = 2007.75uA at Iref528uA. Thus 1/2112.
        // Positive here means SB=1, not the direction of current at a pin.
        return signedUnits(code)*(referenceAmps/2112.0);
    }
    static KnownSignal decodeCurrent(KnownByte code,KnownSignal referenceAmps) {
        if(referenceAmps.known) {
            signedDecodeCurrent(0,referenceAmps.value); // validate even at zero code
            if(referenceAmps.value==0)return {0,true};
        }
        bool first=true;int units=0;
        if(code.known==255){units=signedUnits(code.value);first=false;} // fully known: the loop's single member
        else for(unsigned c=0;c<256;++c)if(!((c^code.value)&code.known)) {
            const int n=signedUnits(std::uint8_t(c));
            if(first){units=n;first=false;}else if(n!=units)return {};
        }
        if(units==0)return {0,true}; // either sign's exact decode zero
        if(!referenceAmps.known)return {};
        return {units*(referenceAmps.value/2112.0),true};
    }
    static DecodePins decodePins(KnownByte code,KnownSignal referenceAmps) {
        if(referenceAmps.known) {
            signedDecodeCurrent(0,referenceAmps.value);
            if(referenceAmps.value==0)return {{0,true},{0,true}};
        }
        bool first=true,plusEqual=true,minusEqual=true;unsigned plus=0,minus=0;
        for(unsigned c=0;c<256;++c)if(!((c^code.value)&code.known)) {
            const unsigned magnitude=magnitudeUnits(std::uint8_t(c));
            const unsigned p=(c&128)?magnitude:0,m=(c&128)?0:magnitude;
            if(first){plus=p;minus=m;first=false;}
            else {plusEqual&=p==plus;minusEqual&=m==minus;}
        }
        const auto result=[&](unsigned n,bool equal)->KnownSignal {
            if(!equal)return {};
            if(n==0)return {0,true};
            if(!referenceAmps.known)return {};
            return {n*(referenceAmps.value/2112.0),true};
        };
        return {result(plus,plusEqual),result(minus,minusEqual)};
    }
    // Encode mode, E/D=1 (AMD 1979 Linear and Interface Data Book pp.3-55/56):
    // current flows into IOE+/IOE- per SB and an internal half step of the
    // current chord is added (full scale +32uA, chord0 step0 +/-0.25uA at
    // Iref 528uA). EII 7-44 ties IOE+/- to IOD+/-, so the LF356 sees
    // (magnitude + 2^chord) units of Iref/2112 with the decode sign.
    static constexpr unsigned encodeMagnitudeUnits(std::uint8_t code) noexcept {
        return magnitudeUnits(code)+(1u<<((code>>4)&7));
    }
    static constexpr int signedEncodeUnits(std::uint8_t code) noexcept {
        const int n=int(encodeMagnitudeUnits(code));return (code&128)?n:-n;
    }
    // EII 7-44 +IDAC.V for either Am6072 mode; settled LF356 (C186 220pF x
    // R18 2.49k = 0.55us is far below the 3us SAR step). Unknown mode merges.
    static KnownSignal eiiSteadyVoltsMode(KnownByte code,KnownSignal amplitudeCv,LogicLevel encode) {
        if(encode.known&&!encode.high)return eiiSteadyVolts(code,amplitudeCv);
        if(amplitudeCv.known && (!std::isfinite(amplitudeCv.value)||amplitudeCv.value<0||amplitudeCv.value>5))
            throw std::invalid_argument("EII amplitude CV outside0..5V ideal model");
        bool first=true;int units=0;
        if(code.known==255){units=signedEncodeUnits(code.value);first=false;}
        else for(unsigned c=0;c<256;++c)if(!((c^code.value)&code.known)) {
            const int n=signedEncodeUnits(std::uint8_t(c));
            if(first){units=n;first=false;}else if(n!=units)return {};
        }
        if(!amplitudeCv.known)return {};
        const KnownSignal enc{-2490.0*units*(amplitudeCv.value/9400.0/2112.0),true};
        if(encode.known)return enc;
        const auto dec=eiiSteadyVolts(code,amplitudeCv);
        return (dec.known&&dec.value==enc.value)?enc:KnownSignal{};
    }
    static KnownSignal eiiSteadyVolts(KnownByte code,KnownSignal amplitudeCv) {
        if(amplitudeCv.known && (!std::isfinite(amplitudeCv.value)||amplitudeCv.value<0||amplitudeCv.value>5))
            throw std::invalid_argument("EII amplitude CV outside0..5V ideal model");
        const auto current=decodeCurrent(code,{amplitudeCv.value/9400.0,amplitudeCv.known});
        // EII7-45: IOD-17 -> LF356 minus, IOD+16 -> plus. Opposite
        // polarity to AMD Figure1. Matched R17/R8=2.49k nominal.
        return {-2490.0*current.value,current.known};
    }
};
}
