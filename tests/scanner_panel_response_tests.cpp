#include "vst3/scanner_panel_response.h"
#include "vst3/plugin_state.h"
#include <fstream>
#include <iostream>
#include <vector>
using namespace eii::vst;
void check(bool b,const char*s){if(!b)throw std::runtime_error(s);}
int main(int argc,char**argv)try{
 check(!parseSettings("EIIVST3 1\n").fastPanel,"old projects keep original scanner response");
 PluginSettings s;s.fastPanel=true;check(parseSettings(serializeSettings(s))==s,"fast option full state roundtrip");
 s.fastPanel=false;check(parseSettings(serializeSettings(s))==s,"original option full state roundtrip");
 std::vector<std::uint8_t> unknown(8192,0);auto original=unknown;
 check(!applyFastScannerPanel(unknown,false)&&unknown==original,"disabled option leaves any firmware untouched");
 bool rejected=false;try{applyFastScannerPanel(unknown,true);}catch(...){rejected=true;}
 check(rejected&&unknown==original,"unsupported ROM rejects without mutation");
 unknown.resize(12);rejected=false;try{applyFastScannerPanel(unknown,true);}catch(...){rejected=true;}check(rejected,"wrong size rejects");
 if(argc>1){std::ifstream f(argv[1],std::ios::binary);check(bool(f),"ownerROM fixture missing");std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(f),{}};const auto old=bytes;
  check(applyFastScannerPanel(bytes,true),"known ownerROM fast patch accepted");unsigned changed=0;for(unsigned i=0;i<bytes.size();++i)changed+=bytes[i]!=old[i];check(changed==1&&bytes[0xc1e]==0x18,"exactly one intended opcode changes");
  const auto fast=bytes;check(applyFastScannerPanel(bytes,true)&&bytes==fast,"fast patch idempotent");
  auto corrupt=old;corrupt[100]^=1;rejected=false;try{applyFastScannerPanel(corrupt,true);}catch(...){rejected=true;}check(rejected&&corrupt[0xc1e]==0x30,"whole-ROM guard rejects altered firmware");
  std::cout<<"PASS known ownerROM one-byte/idempotence/whole-image guard; source file never written\n";
 }
 std::cout<<"PASS original default, projectstate, disabled/unsupported/size guards\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
