#pragma once
#include "filter_calibration.h"
namespace eii::analog {
// Fast Legacy VCA: original small-signal OTA current law. The EII adapter
// connects only VCA2; VCA1 input is zero. No tanh audio saturation. EXPO split
// and current calibration retain the same nominal units as the SSM model.
class LegacyQLinearVca {
public:
    void calibrate(const FilterCalibration& c){
        c.validate();using P=FilterParam;
        const double floor=c[P::VcaFloor];
        const double w1=std::clamp(floor+(1-2*floor)*.5*(1-std::tanh(c[P::Expo]/(2*c[P::VcaVt]*1e-3))),floor,1-floor);
        gain_=c[P::VcaGain]*2*(1-w1)*c[P::I15]*1e-6;
        offset_=c[P::VcaOffset]*1e-6;
    }
    double process(double input,double controlScale=1)const noexcept{return gain_*controlScale*input+offset_;}
private:double gain_=4085e-6,offset_=0;
};
}
