#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include "level_model.h"
namespace eii::analog {
// Units here match the service panel/export, not SI. Defaults preserve0.54.
#define EII_FILTER_PARAMS(X) \
 X(FcRin,0,"FC input R","kOhm",1,100,22.6,true) \
 X(FcRbias,0,"FC bias R","kOhm",10,1000,124,true) \
 X(FcRshunt,0,"FC shunt R","kOhm",.1,10,1,true) \
 X(FcSupply,0,"FC bias supply","V",-20,0,-15,false) \
 X(FcCap,0,"FC smoothing C","uF",0,20,3.3,false) \
 X(Qres,0,"Q input R","kOhm",1,200,22,true) \
 X(Qemitter,0,"Q emitter offset","V",0,1.2,.6,false) \
 X(Qscale,0,"Q current scale","x",0,2,1,false) \
 X(Qmax,0,"Q current ceiling","uA",0,300,300,false) \
 X(Qtau,0,"Q current smoothing","ms",0,100,0,false) \
 X(C1,1,"C1","nF",.1,100,20,true) \
 X(C2,1,"C2","nF",.1,100,3.9,true) \
 X(C3,1,"C3","nF",.1,100,20,true) \
 X(C4,1,"C4","nF",.1,100,1.8,true) \
 X(Gm0,1,"gm reference","uS",1,500,72.188,true) \
 X(Vtef,1,"FC exponential scale","mV",10,60,27.844,false) \
 X(FcBias,1,"FC chip bias","mV",-20,20,.982,false) \
 X(Qonset,1,"Q normalization current","uA",50,300,150,false) \
 X(W1,1,"Q feedback W1","x",0,6,2.10216,false) \
 X(W8,1,"Q feedback W8","x",0,8,3.10216,false) \
 X(GaLimit,1,"Ga soft limit","V",.1,5,1.2,true) \
 X(G1Limit,1,"OTA G1 soft limit","V",.02,1,.145,true) \
 X(G2Limit,1,"OTA G2 soft limit","V",.02,1,.26,true) \
 X(TLimit,1,"Output soft limit","V",1,15,7,false) \
 X(Nonlinear,1,"Nonlinear OTAs","0/1",0,1,1,false) \
 X(Offsets,2,"Enable chip offsets","0/1",0,1,0,false) \
 X(Vos1,2,"VOS G1","mV",-30,30,4,false) \
 X(Vos2,2,"VOS G2","mV",-30,30,-11,false) \
 X(Vos3,2,"VOS G3","mV",-30,30,6,false) \
 X(Vos4,2,"VOS G4","mV",-30,30,-9,false) \
 X(Ios1,2,"IOS N1","nA",-100,100,12,false) \
 X(Ios2,2,"IOS N2","nA",-100,100,-9,false) \
 X(Ios3,2,"IOS N3","nA",-100,100,15,false) \
 X(Ios4,2,"IOS N4","nA",-100,100,-11,false) \
 X(VcaGain,3,"VCA gain","A/V/A",1,20,8.17,false) \
 X(VcaRange,3,"VCA soft limit","V",.02,1,.211,true) \
 X(I16,3,"VCA pin16 current","uA",0,500,0,false) \
 X(I15,3,"VCA pin15 current","uA",0,500,500,false) \
 X(Expo,3,"VCA EXPO voltage","V",-.25,.25,0,false) \
 X(VcaFloor,3,"EXPO floor","x",0,.05,.001,false) \
 X(VcaVt,3,"EXPO thermal scale","mV",10,60,25.85,false) \
 X(VcaOffset,3,"VCA output DC offset","uA",-20,20,0,false) \
 X(InputGain,3,"Pre-filter input trim","x",.01,4,1,true) \
 X(OutputGain,3,"Post-VCA trim","x",.01,4,1,true) \
 X(MixGain,3,"Host master trim","x",.01,4,1,true) \
 X(ReferenceTau,3,"Reference-route DAC RC","ms",.01,20,2.35,true) \
 X(CouplingTau,3,"VCA coupling time","ms",.1,100,10.47,true) \
 X(VcaFollow,3,"Bypass board RC: linear ACV (lab)","0/1",0,1,0,false) \
 X(FcOverride,4,"Override FC pin","0/1",0,1,0,false) \
 X(FcManual,4,"Manual FC pin","mV",-130,130,0,false) \
 X(QOverride,4,"Override Q current","0/1",0,1,0,false) \
 X(QManual,4,"Manual Q current","uA",0,300,0,false) \
 X(AcvOverride,4,"Override amplitude CV","0/1",0,1,0,false) \
 X(AcvManual,4,"Manual amplitude CV","V",0,5,0,false) \
 X(NativeOutDb,5,"Post-chain level trim","dB",-24,36,0,false) \
 X(NativeRref,5,"DAC reference total R","kOhm",2,40,9.4,true) \
 X(NativeRefTau,5,"DAC reference RC time","ms",.01,20,2.35,true) \
 X(NativeRivMinus,5,"I/V feedback R17","kOhm",.5,5,2.49,true) \
 X(NativeRivPlus,5,"I/V positive R8","kOhm",.5,5,2.49,true) \
 X(NativeCiv,5,"I/V feedback C187","pF",22,470,220,true) \
 X(NativeRsample,5,"Deglitch series R26","Ohm",50,1000,200,true) \
 X(NativeRfeedback,5,"Deglitch feedback R34","kOhm",1,50,10,true) \
 X(NativeRon,5,"13202 switch Ron (typ)","Ohm",10,500,150,true) \
 X(NativeCsample,5,"Sample capacitor C74","pF",100,2200,470,true) \
 X(NativeChold,5,"Feedback capacitor C83","pF",100,2200,470,true) \
 X(S3528Override,6,"Override firmware code (lab)","0/1",0,1,0,false) \
 X(S3528Code,6,"S3528 test control code","code",0,63,35,false) \
 X(CemDrive,7,"CEM input drive","x",.01,16,1,true) \
 X(CemOutputGain,7,"CEM output makeup","x",.0625,16,1,true) \
 X(CemQMakeup,7,"Q passband makeup","%",0,100,0,false) \
 X(CemQScale,7,"CEM resonance scale","x",0,2,1,false) \
 X(CemFcScale,7,"CEM cutoff scale","x",.125,8,1,true) \
 X(CemFc1Cv,7,"CEM Fc1 CV (3-pole)","V",0,4.96,4.96,false) \
 X(Ssm2DieTemp,8,"Die temperature (datasheet typ = 25)","degC",0,85,25,false) \
 X(Ssm2Noise,8,"Chip noise (84 dB S/N typ)","0/1",0,1,1,false) \
 X(Ssm2Offsets,8,"Device offsets / feedthrough","0/1",0,1,0,false) \
 X(Ssm2OtaAtten,8,"OTA input attenuation (THD fit)","x",.1,1,.433,true) \
 X(Ssm2QAtten,8,"G_Q input attenuation (150 uA onset)","x",.02,.5,.10396,true) \
 X(Ssm2VcaAtten,8,"VCA input attenuation (4085 umho)","x",.1,1,.41982,true) \
 X(Ssm2Tail,8,"OTA tail current at V5=0","nA",1000,100000,12603.62,true) \
 X(Ssm2Expo,8,"Expo sensitivity at 25 C","mV/oct",17,22,19.3,false) \
 X(Ssm2Sink,8,"Output buffer sink current","uA",200,3000,750,true) \
 X(Ssm2Swing,8,"Retired: common TL084 swing (level_model.h)","V",5,15,13.5,false)
enum class FilterParam {
#define X(id,...) id,
 EII_FILTER_PARAMS(X)
#undef X
 Count
};
inline constexpr unsigned baseFilterParameterCount=unsigned(FilterParam::CemDrive);
inline constexpr unsigned cemFilterParameterCount=unsigned(FilterParam::Ssm2DieTemp); // schema 3 (CEM) size
inline constexpr unsigned legacyFilterParameterCount=unsigned(FilterParam::NativeOutDb);
struct FilterParameter {const char* key;unsigned group;const char* label;const char* unit;double lo,hi,initial;bool logarithmic;};
inline constexpr std::array<FilterParameter,unsigned(FilterParam::Count)> filterParameters{{
#define X(id,g,label,unit,lo,hi,initial,logarithmic) {#id,g,label,unit,lo,hi,initial,logarithmic},
 EII_FILTER_PARAMS(X)
#undef X
}};
#undef EII_FILTER_PARAMS
struct FilterCalibration {
 std::array<double,filterParameters.size()> values=[] {std::array<double,filterParameters.size()> a{};for(unsigned i=0;i<a.size();++i)a[i]=filterParameters[i].initial;return a;}();
 double operator[](FilterParam p)const{return values[unsigned(p)];}
 double& operator[](FilterParam p){return values[unsigned(p)];}
 bool operator==(const FilterCalibration&)const=default;
 void validate()const {
  for(unsigned i=0;i<values.size();++i){auto& d=filterParameters[i];if(!std::isfinite(values[i])||values[i]<d.lo||values[i]>d.hi||(std::string(d.unit)=="0/1"&&values[i]!=0&&values[i]!=1)||(std::string(d.unit)=="code"&&std::floor(values[i])!=values[i]))throw std::invalid_argument(std::string("Invalid calibration: ")+d.key);}
  if((*this)[FilterParam::I16]+(*this)[FilterParam::I15]>500.0000001)throw std::invalid_argument("VCA total control current must be <=500uA");
 }
 void resetGroupDefaults(unsigned group){for(unsigned i=0;i<values.size();++i)if(filterParameters[i].group==group)values[i]=filterParameters[i].initial;}
 void resetNativeDefaults(){resetGroupDefaults(5);}
 std::string serialize(bool legacy=false)const {
  std::ostringstream out;out.imbue(std::locale::classic());out<<"EII_FILTER_CALIBRATION="<<(legacy?1:5)<<"\n"<<std::setprecision(17);
  for(unsigned i=0;i<(legacy?legacyFilterParameterCount:values.size());++i)out<<filterParameters[i].key<<'='<<values[i]<<'\n';
  return out.str();
 }
 static FilterCalibration parse(const std::string& text) {
  if(text.size()>32768)throw std::invalid_argument("Calibration file too large");
  std::istringstream in(text);in.imbue(std::locale::classic());std::string line;std::getline(in,line);if(!line.empty()&&line.back()=='\r')line.pop_back();
  const bool legacy=line=="EII_FILTER_CALIBRATION=1";
  const bool cem=line=="EII_FILTER_CALIBRATION=3",extended=line=="EII_FILTER_CALIBRATION=4"||line=="EII_FILTER_CALIBRATION=5";
  const unsigned expected=legacy?legacyFilterParameterCount:extended?filterParameters.size():cem?cemFilterParameterCount:baseFilterParameterCount;
  if(!legacy&&!extended&&!cem&&line!="EII_FILTER_CALIBRATION=2")throw std::invalid_argument("Unsupported calibration schema");
  FilterCalibration r;std::array<bool,filterParameters.size()> seen{};
  while(std::getline(in,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;auto eq=line.find('=');if(eq==std::string::npos)throw std::invalid_argument("Expected key=value");
   unsigned k=0;for(;k<filterParameters.size();++k)if(line.substr(0,eq)==filterParameters[k].key)break;
   if(k==seen.size()||seen[k]||k>=expected)throw std::invalid_argument("Unknown or duplicate calibration key");
   std::istringstream v(line.substr(eq+1));v.imbue(std::locale::classic());if(!(v>>r.values[k]))throw std::invalid_argument("Invalid number");v>>std::ws;if(!v.eof())throw std::invalid_argument("Trailing calibration data");seen[k]=true;
  }
  const auto end=seen.begin()+expected;
  if(std::find(seen.begin(),end,false)!=end)throw std::invalid_argument("Incomplete calibration profile");
  r.validate();return r;
 }
 // Schema 5 = written with the common level model (level_model.h).
 static bool preLevelModel(const std::string& text){return !text.starts_with("EII_FILTER_CALIBRATION=5");}
 FilterCalibration importProfile(const std::string& text)const{
  auto next=parse(text);
  if(text.starts_with("EII_FILTER_CALIBRATION=1"))
   for(unsigned i=legacyFilterParameterCount;i<values.size();++i)next.values[i]=values[i];
  if(text.starts_with("EII_FILTER_CALIBRATION=2"))
   for(unsigned i=baseFilterParameterCount;i<values.size();++i)next.values[i]=values[i];
  if(text.starts_with("EII_FILTER_CALIBRATION=3"))
   for(unsigned i=cemFilterParameterCount;i<values.size();++i)next.values[i]=values[i];
  return next;
 }
};
// Profiles written before the common level model: a native trim the user set
// (non-zero NativeOutDb) keeps its loudness and is lowered by the level-model
// gain; untouched (0 dB) trims follow the new hardware level (level_model.h).
inline void migrateLevelModel(FilterCalibration& c){
 auto& v=c[FilterParam::NativeOutDb];if(v!=0)v=std::max(filterParameters[unsigned(FilterParam::NativeOutDb)].lo,v-levelModelMigrationDb);
}
// Fast VCF keeps the two linear OTA sections plus bounded Ga feedback. Chip
// OTA/output saturation and offset knobs have no effect and must not be shown.
inline bool legacyQParameterSupported(unsigned index){
    const auto p=FilterParam(index);
    return index<filterParameters.size()&&filterParameters[index].group!=2&&filterParameters[index].group!=7&&filterParameters[index].group!=8&&
        p!=FilterParam::G1Limit&&p!=FilterParam::G2Limit&&p!=FilterParam::TLimit&&
        p!=FilterParam::Nonlinear&&p!=FilterParam::Offsets&&p!=FilterParam::VcaRange;
}

// Vendor SSM2045 models: everything except the CEM and model-2 groups.
inline bool ssmParameterSupported(unsigned index){return index<filterParameters.size()&&filterParameters[index].group!=7&&filterParameters[index].group!=8;}
// SSM2045 model 2 (docs/SSM2045_MODEL2.md): board networks, EII caps, gains,
// overrides, native front-end, S3528 and its own chip group. The vendor-chip
// law/limit/offset knobs and the vendor VCA/crossfade knobs do not apply.
inline bool ssm2ParameterSupported(unsigned index){
 if(index>=filterParameters.size())return false;
 using P=FilterParam;const auto p=P(index);const auto group=filterParameters[index].group;
 if(group==2||group==7)return false;
 if(group==1)return p==P::C1||p==P::C2||p==P::C3||p==P::C4;
 if(group==3)return p==P::InputGain||p==P::OutputGain||p==P::MixGain||p==P::ReferenceTau||p==P::CouplingTau;
 if(p==P::Qemitter||p==P::Ssm2Swing)return false; // own PNP emitter law; swing is common now
 return true;
}
inline bool cemParameterSupported(unsigned index){
 if(index>=filterParameters.size())return false;
 const auto group=filterParameters[index].group;
 return group==0||group==3||group==4||group==5||group==6||group==7;
}
}
