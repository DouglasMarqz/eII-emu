#include "production_controller.h"
#include "reference/production_controller_bus_037.h"
#include <random>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace eii::production;
int main(){
 for(unsigned x=0;x<256;++x){const auto expected=((x&1)<<4)|((x&2)<<2)|(x&4)|((x&8)>>2)|((x&16)>>4);if(reverseCapturedFlags(x)!=expected)throw std::runtime_error("flag permutation differs");}
 std::mt19937 rng(0x20261024);
 eii::PromLatch image;for(unsigned i=0;i<512;++i){image.lower[i]=std::uint8_t(rng());image.upper[i]=std::uint8_t(rng());}
 auto tables=std::make_unique<eii::ControllerProms>();tables->upper.load(image.upper);tables->lower.load(image.lower);tables->certifyQuietUpperTail();
 unsigned tested=0,floating=0,contention=0,valid=0;
 for(unsigned control=0;control<65536;++control)for(unsigned variant=0;variant<8;++variant){
  auto a=std::make_unique<BusBaselineController>();auto b=std::make_unique<Controller>();
  a->proms=b->proms=tables.get();a->control=b->control=std::uint16_t(control);
  a->phase=b->phase=std::uint8_t(rng()&15);a->channel=b->channel=std::uint8_t(rng()%9);
  a->shift=b->shift=std::uint8_t(rng());a->captured=b->captured=std::uint8_t(rng()&31);
  a->lowAperture=b->lowAperture=variant&1;
  a->staging.address=b->staging.address=std::uint8_t(rng()&63);
  a->staging.data=b->staging.data=(variant&2)?0x123456:rng();
  a->aluOutput.output=b->aluOutput.output=(variant&2)?0x123456:rng();
  for(unsigned cell=0;cell<64;++cell){const auto v=(variant&2)?0x123456:rng();a->registers.write(cell,v);b->registers.write(cell,v);}
  std::array<bool,8> req{};for(auto& r:req)r=rng()&1;
  a->rising(req,0);b->rising(req,0);
  if(a->invalidCode!=b->invalidCode||a->invalid!=b->invalid||a->lastRfd!=b->lastRfd||a->control!=b->control||a->phase!=b->phase||a->channel!=b->channel||a->captured!=b->captured||a->data.low20!=b->data.low20||a->data.flags!=b->data.flags||a->upper.upper!=b->upper.upper||a->registerWrites!=b->registerWrites)throw std::runtime_error("controller transition differs");
  for(unsigned cell=0;cell<64;++cell)if(a->registers.read(cell)!=b->registers.read(cell))throw std::runtime_error("register difference");
  ++tested;floating+=b->invalidCode==5;contention+=b->invalidCode==6;valid+=b->valid();
 }
 std::cout<<"PASS baseline/candidate "<<tested<<" transitions, floating="<<floating<<" contention="<<contention<<" valid="<<valid<<"\n";
 if(!floating||!contention||!valid)return 1;
}
