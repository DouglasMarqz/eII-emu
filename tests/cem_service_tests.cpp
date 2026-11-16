#include "vst3/plugin_state.h"
#include "filter_service_profiles.h"
#include <fstream>
#include <iostream>
#include <bit>
using namespace eii::analog;using P=FilterParam;
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
std::string read(const char* p){std::ifstream f(std::filesystem::path(__FILE__).parent_path()/"fixtures/cem-v018"/p);check(bool(f),"fixture missing");return {(std::istreambuf_iterator<char>(f)),{}};}
int main()try{
 const auto old=eii::vst::parseSettings(read("old-state.txt"));
 check(old.calibration[P::W1]==2.1234567890123&&old.legacyCalibration[P::W1]==3.234567890123&&old.eiiiCalibration[P::OutputGain]==2.1,"old state profile values preserved");
 check(old.eiiiCalibration[P::CemDrive]==1&&old.eiiiCalibration[P::CemQMakeup]==0,"old state neutral CEM defaults");
 auto c=old.eiiiCalibration;c[P::CemDrive]=3;c[P::NativeOutDb]=17.25;
 check(c.importProfile(read("old-profile-v2.txt"))[P::CemDrive]==3,"V2 import preserves new CEM knobs");
 const auto v1=c.importProfile(read("old-profile-v1.txt"));check(v1[P::CemDrive]==3&&v1[P::NativeOutDb]==17.25,"V1 native/CEM import preservation");
 check(FilterCalibration::parse(c.serialize())==c,"V3 full roundtrip");
 bool rejected=false;auto incomplete=c.serialize();incomplete.erase(incomplete.rfind("CemFc1Cv="));try{FilterCalibration::parse(incomplete);}catch(...){rejected=true;}check(rejected,"V3 partial rejection");
 auto oldGlobal=eii::host::FilterServiceProfiles::parse(read("old-global.txt"));check(!oldGlobal.hasEiii&&oldGlobal.ssm[P::W1]==old.calibration[P::W1],"old global preserved without fake CEM section");
 eii::host::FilterServiceProfiles g{old.calibration,old.legacyCalibration,c};check(eii::host::FilterServiceProfiles::parse(g.serialize())==g,"three-model global roundtrip");
 auto newer=old;newer.eiiiCalibration=c;check(eii::vst::parseSettings(eii::vst::serializeSettings(newer))==newer,"new project full exact roundtrip");
 for(unsigned rate:{96000u,192000u}){
  EiiiCustomFilter original(rate),makeup(rate),resonant(rate),boosted(rate);FilterCalibration flat,boost;boost[P::CemQMakeup]=100;
  double a=0,b=0;
  for(unsigned n=0;n<24000;++n){const double x=.02*std::sin(n*.035);
   const double y0=original.process(x,-.03,0,flat),y1=makeup.process(x,-.03,0,boost);
   check(std::bit_cast<std::uint64_t>(y0)==std::bit_cast<std::uint64_t>(y1),"Q makeup must be BIT-neutral at zero Q");
   const double y2=resonant.process(x,-.03,100e-6,flat),y3=boosted.process(x,-.03,100e-6,boost);
   check(std::isfinite(y2)&&std::isfinite(y3),"finite resonant makeup");if(n>12000){a+=y2*y2;b+=y3*y3;}
  }check(b>a*2,"Q makeup raises resonant passband");std::cout<<"PASS rate="<<rate<<" zero-Q exact; resonant makeup ratio="<<std::sqrt(b/a)<<'\n';
 }
 for(unsigned i=0;i<filterParameters.size();++i){if(filterParameters[i].group==7){check(cemParameterSupported(i),"CEM controls supported");check(!legacyQParameterSupported(i),"CEM controls hidden in Legacy");}}
 std::cout<<"PASS old/new project/profile/global migration, independent CEM settings and zero-Q-safe makeup\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
