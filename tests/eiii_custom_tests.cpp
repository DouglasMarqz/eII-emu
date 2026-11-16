#include "vst3/plugin_state.h"
#include <iostream>
#include <bit>
using namespace eii::analog;
void require(bool v,const char* text){if(!v)throw std::runtime_error(text);}
int main()try{
 eii::vst::PluginSettings settings;settings.audioMode=RuntimeAudioMode::EiiiCustom;
 settings.eiiiCalibration[FilterParam::W1]=1.23456789012345;
 require(eii::vst::parseSettings(eii::vst::serializeSettings(settings))==settings,"custom model/profile project roundtrip");
 require(eii::vst::activeCalibration(settings)==settings.eiiiCalibration,"custom profile selection");
 require(workerAudioMode(RuntimeAudioMode::EiiiCustom),"custom uses DSP owner");
 for(unsigned factor:{2u,4u}){
  PlaybackVoiceBank bank(factor,false,true);FilterCalibration c;bank.calibrate(c);
  double energy=0;for(unsigned n=0;n<24000;++n){
   PlaybackVoiceBank::Input in;in.channel0Playback=true;
   for(unsigned ch=0;ch<8;++ch){in.playback.codes[ch]={std::uint8_t(n%109<54?0x3f:0xbf),255};in.playback.amplitudeCv[ch]={2.5,true};in.frequency[ch]={1.5,true};in.resonance[ch]={1.5,true};}
   const double y=bank.process(in);require(std::isfinite(y),"finite custom eightvoice output");energy+=y*y;
   if(n==12000)bank.reset();
  }
  require(bank.solverStats().samples==0,"SSM solver must not run alongside custom filter");require(energy>1e-9,"custom filter audible");std::cout<<"PASS custom8voice rate="<<48000*factor<<" energy="<<energy<<'\n';
 }
 for(unsigned rate:{96000u,192000u}){
  EiiiCustomFilter filter(rate);FilterCalibration c;
  double energy=0;for(unsigned n=0;n<16000;++n){
   const double fc=(n<8000?-0.08:0.01),q=(n<4000?0:n<12000?100e-6:200e-6);
   const double x=filter.process(.05*std::sin(n*.1),fc,q,c);require(std::isfinite(x),"finite FC/Q sweep");energy+=x*x;
  }require(energy>1e-9,"FC/Q sweep output");
 }
 std::cout<<"PASS custom model serialization, independent profile, FC/Q sweep/reset; EIII VCF only, EII VCA retained\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
