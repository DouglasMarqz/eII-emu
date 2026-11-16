#pragma once
#include "filter_calibration.h"
#include "../vendor/eiii-cem/eiii_cem3387_v2.h"
namespace eii::analog {
// Custom CV bridge, not a claim of a physical EII/EIII circuit match.
class EiiiCustomFilter {
public:
 explicit EiiiCustomFilter(double rate){
  eiii::cem_v2_options options;options.noise=false;
  filter_.prepare(params_,rate,options,0xE111C338u);
  filter_.set_volts(eiii::CV_FC1,params_.dac_full_scale_v);
  filter_.set_volts(eiii::CV_FC2,params_.dac_full_scale_v);
  filter_.set_volts(eiii::CV_Q,params_.q_v_none);
  filter_.cv().reset_to_targets();filter_.reset();
 }
 double process(double input,double fcPin,double qCurrent,const FilterCalibration& c){
  using P=FilterParam;constexpr double pi=3.14159265358979323846;
  const double gm=c[P::Gm0]*1e-6*std::exp(std::clamp(-(fcPin-c[P::FcBias]*1e-3)/(c[P::Vtef]*1e-3),-700.0,700.0));
  const double hz=std::max(0.01,gm/(2*pi*std::sqrt(c[P::C1]*1e-9*10*c[P::C2]*1e-9))*c[P::CemFcScale]);
  const double resistance=1/(2*pi*hz*std::sqrt(params_.c3a*params_.c3b));
  const double ratio=std::max(1e-12,resistance/params_.fc2_r0-1);
  const double fc=std::clamp(params_.vr()-params_.fc_vt/params_.fc_k*std::log(ratio),0.0,params_.dac_full_scale_v);
  const double q=std::clamp(params_.q_v_none-(params_.q_v_none-params_.q_v_osc)*(qCurrent*c[P::CemQScale])/(c[P::Qonset]*1e-6),0.0,params_.q_v_none);
  filter_.set_volts(eiii::CV_FC1,c[P::CemFc1Cv]);filter_.set_volts(eiii::CV_FC2,fc);filter_.set_volts(eiii::CV_Q,q);
  double unusedLeft,unusedRight;filter_.process_volts(input*c[P::CemDrive],unusedLeft,unusedRight);
  return filter_.vcf_out()*c[P::CemOutputGain]*(1.0+c[P::CemQMakeup]*0.01*filter_.loop_k()); // EII VCA remains downstream; never add EIII VCA output.
 }
 void reset(){filter_.reset();}
private:
 eiii::cem_params params_;eiii::cem3387_v2_voice filter_;
};
}
