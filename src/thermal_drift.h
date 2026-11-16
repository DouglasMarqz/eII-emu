#pragma once
// ESTIMATED analog drift (off by default; not measured on an EII). Slow die
// temperature wander of the voice filter chips, applied only through the
// exponential converter: I = Iref*exp(Vpin/Vt) with Vt = kT/q, so a die at
// T0+dT acts as the pin voltage scaled by T0/(T0+dT). Nothing else in the
// signal path changes. dT is 1/f-like: five Ornstein-Uhlenbeck sources with
// time constants 8..2048 s (equal variance, a 1/f approximation between them),
// one set common to the board plus a smaller independent set per voice.
// Bounded to +/-2 K. Fixed seed: renders repeat. Control rate: 10 ms (a step is < 0.01 cent).
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace eii::analog {
class ThermalDrift {
public:
    static constexpr double commonKelvinRms=.3,voiceKelvinRms=.1,limitKelvin=2,updateSeconds=.01;
    static constexpr std::array<double,5> tau{8,32,128,512,2048};
    explicit ThermalDrift(std::uint64_t seed=0x45494944726966ull):rng_(seed){
        for(unsigned k=0;k<tau.size();++k){decay_[k]=std::exp(-updateSeconds/tau[k]);gain_[k]=std::sqrt(1-decay_[k]*decay_[k]);}
        for(auto& x:common_)x=gauss();for(auto& v:voice_)for(auto& x:v)x=gauss(); // stationary start
    }
    // Advance one control step; scale[ch] = T0/(T0+dT) for the FC pin.
    void step(double dieCelsius,std::array<double,8>& scale){
        double c=0;for(unsigned k=0;k<tau.size();++k){common_[k]=common_[k]*decay_[k]+gain_[k]*gauss();c+=common_[k];}
        const double t0=dieCelsius+273.15,norm=1/std::sqrt(double(tau.size()));
        for(unsigned ch=0;ch<8;++ch){double v=0;for(unsigned k=0;k<tau.size();++k){voice_[ch][k]=voice_[ch][k]*decay_[k]+gain_[k]*gauss();v+=voice_[ch][k];}
            const double dt=std::clamp((c*commonKelvinRms+v*voiceKelvinRms)*norm,-limitKelvin,limitKelvin);
            kelvin_[ch]=dt;scale[ch]=t0/(t0+dt);}
    }
    const std::array<double,8>& kelvin()const noexcept{return kelvin_;}
private:
    std::uint64_t rng_;std::array<double,tau.size()> decay_{},gain_{},common_{};std::array<std::array<double,tau.size()>,8> voice_{};std::array<double,8> kelvin_{};
    double uniform(){rng_^=rng_>>12;rng_^=rng_<<25;rng_^=rng_>>27;return ((rng_*0x2545F4914F6CDD1Dull)>>11)*(1.0/9007199254740992.0);}
    double gauss(){const double u=std::max(uniform(),1e-300),v=uniform();return std::sqrt(-2*std::log(u))*std::cos(6.283185307179586*v);}
};
}
