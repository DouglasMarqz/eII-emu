#pragma once
// Functional, firmware-facing EII HD experiment. Not an AE343 gate netlist or
// an execution model of the Adaptec MCU. Explicit installation only.
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <functional>
#include <span>
#include <stdexcept>
#include <vector>
namespace eii {
class HardDisk {
public:
 enum Phase {Free,Command,DataIn,DataOut,Status,Message};
 Phase phase=Free;
 std::vector<std::uint8_t> image;
 std::vector<std::vector<std::uint8_t>> commands;
 std::uint64_t bytesRead=0,bytesWritten=0,invalidAccess=0;
 unsigned sectors=18,heads=4,cylinders=612;
 std::vector<std::uint8_t> lastMode,defectList;
 unsigned blockSize=512;
 std::function<void(std::span<const std::uint8_t>)> persistImage;
 bool writable=true,dirty=false,dmaArmed=false;
 explicit HardDisk(std::vector<std::uint8_t> bytes):image(std::move(bytes)){
  if(image.empty()||image.size()%512)throw std::invalid_argument("HDD image must contain complete512-byte sectors");
 }
 bool dataRequest(std::uint64_t t)const{return (phase==DataIn||phase==DataOut)&&t>=readyAt_;}
 void save(const std::string& path)const{std::ofstream f(path,std::ios::binary|std::ios::trunc);if(!f)throw std::runtime_error("HDD output open");f.write(reinterpret_cast<const char*>(image.data()),image.size());if(!f)throw std::runtime_error("HDD output write");}
 std::uint8_t read(unsigned p,std::uint64_t t){
  if(p==0x3d){if(phase==Free)return 0;unsigned s=2;if(t>=readyAt_)s|=1;if(phase==Command||phase==Status||phase==Message)s|=8;if(phase==DataIn||phase==Status||phase==Message)s|=16;return std::uint8_t(s);}
  if(p==0x3f){dmaArmed=false;return 255;} // diagnostic latch hypothesis, not electrically verified
  if(p!=0x3c||phase==Free||t<readyAt_){++invalidAccess;return 255;}
  if(phase==DataIn){auto v=data_.at(pos_++);++bytesRead;next(pos_==data_.size()?Status:DataIn,t);return v;}
  if(phase==Status){next(Message,t);return status_;}
  if(phase==Message){next(Free,t);return 0;}
  ++invalidAccess;return 255;
 }
 void write(unsigned p,std::uint8_t v,std::uint64_t t){
  if(p==0x3d){next(Free,t);return;} // startup reset hypothesis, isolated behind optional device
  if(p==0x3f){dmaArmed=true;return;}
  if(p==0x3e){if(v==0){cdb_.clear();data_.clear();pos_=0;status_=0;next(Command,t);}return;}
  if(p!=0x3c)return;
  if(phase==Free)return; // target selection bitmap
  if(t<readyAt_){++invalidAccess;return;}
  if(phase==Command){cdb_.push_back(v);const unsigned n=(cdb_[0]>>5)==0?6:10;if(cdb_.size()==n)execute(t);else next(Command,t);return;}
  if(phase==DataOut){data_.at(pos_++)=v;++bytesWritten;if(pos_==data_.size()){finishWrite(t);}else next(DataOut,t);return;}
  ++invalidAccess;
 }
private:
 std::vector<std::uint8_t> cdb_,data_,buffer_=std::vector<std::uint8_t>(1024);
 unsigned pos_=0,lba_=0;std::uint8_t status_=0,sense_=0;std::uint64_t readyAt_=0;
 void next(Phase p,std::uint64_t t){phase=p;readyAt_=t+2;}
 void fail(std::uint8_t s,std::uint64_t t){sense_=s;status_=2;next(Status,t);}
 void input(std::vector<std::uint8_t> b,std::uint64_t t){data_=std::move(b);pos_=0;next(data_.empty()?Status:DataIn,t);}
 void output(unsigned n,std::uint64_t t){data_.assign(n,0);pos_=0;next(n?DataOut:Status,t);}
 void execute(std::uint64_t t){
  if(commands.size()==256)commands.erase(commands.begin());commands.push_back(cdb_);pos_=0;status_=0;const auto op=cdb_[0];
  if(op==3){input({sense_,0,0,0},t);sense_=0;return;}
  sense_=0;
  const bool ext=cdb_.size()==10;
  lba_=ext?(unsigned(cdb_[2])<<24)|(unsigned(cdb_[3])<<16)|(unsigned(cdb_[4])<<8)|cdb_[5]:(unsigned(cdb_[1]&31)<<16)|(unsigned(cdb_[2])<<8)|cdb_[3];
  const unsigned count=ext?(unsigned(cdb_[7])<<8)|cdb_[8]:(cdb_[4]?cdb_[4]:256);
  if(op==8||op==0x0a||op==0x28||op==0x2a||op==0x2e||op==0x2f){
   if(lba_>=image.size()/512||count>image.size()/512-lba_){fail(0x21,t);return;}
   if(op==8||op==0x28){input({image.begin()+lba_*512,image.begin()+(lba_+count)*512},t);return;}
   if(op==0x2f){next(Status,t);return;}
   if(!writable){fail(0x27,t);return;}output(count*512,t);return;
  }
  switch(op){
  case 0:case 1:case 0xb:case 0x10:case 0x1b: next(Status,t);break;
  case 4: if(!writable){fail(0x27,t);break;}if((cdb_[1]&0x1d)==0x1c){output(4,t);break;}if(cdb_[1]&0x1d){fail(0x24,t);break;}std::fill(image.begin(),image.end(),(cdb_[1]&2)?cdb_[2]:0x6c);dirty=true;persist(t);next(Status,t);break;
  case 0x13:output(1024,t);break;
  case 0x14:input(buffer_,t);break;
  case 0x15:output(cdb_[4],t);break;
  case 0x1a:fail(0x20,t);break;
  case 0x1c:fail(0x20,t);break;
  case 0x1d:fail(0x20,t);break;
  case 0x25:{unsigned n=unsigned(image.size()/512-1);input({std::uint8_t(n>>24),std::uint8_t(n>>16),std::uint8_t(n>>8),std::uint8_t(n),0,0,2,0},t);break;}
  default:fail(0x20,t);break;
  }
 }
 void persist(std::uint64_t t){if(persistImage)try{persistImage(image);dirty=false;}catch(...){fail(0x0c,t);throw;}}
 void finishWrite(std::uint64_t t){
  switch(cdb_[0]){
  case 0x0a:case 0x2a:case 0x2e:std::copy(data_.begin(),data_.end(),image.begin()+lba_*512);dirty=true;break;
  case 0x13:buffer_=data_;break;
  case 4: {const unsigned n=data_.size()>=4?(unsigned(data_[2])<<8)|data_[3]:0;
   if(data_[0]||data_[1]||(n&7)||n>65528){fail(0x24,t);return;}
   if(data_.size()==4&&n){data_.resize(4+n);next(DataOut,t);return;}
   unsigned prevC=0,prevH=0,prevB=0;
   for(unsigned i=4;i<data_.size();i+=8){unsigned c=(unsigned(data_[i])<<16)|(unsigned(data_[i+1])<<8)|data_[i+2],h=data_[i+3],b=(unsigned(data_[i+4])<<24)|(unsigned(data_[i+5])<<16)|(unsigned(data_[i+6])<<8)|data_[i+7];
    if(!c||c>=cylinders||h>=heads||(i>4&&(c<prevC||(c==prevC&&(h<prevH||(h==prevH&&b<prevB)))))){fail(0x24,t);return;}prevC=c;prevH=h;prevB=b;}
   defectList=data_; // logical-sector backing: retain list, no physical flux/defect simulation
   std::fill(image.begin(),image.end(),(cdb_[1]&2)?cdb_[2]:0x6c);dirty=true;break;}
  case 0x15:lastMode=data_;if(data_.size()>=16){cylinders=(unsigned(data_[13])<<8)|data_[14];heads=data_[15];blockSize=(unsigned(data_[9])<<16)|(unsigned(data_[10])<<8)|data_[11];}if(blockSize!=512){fail(0x24,t);return;}break;
  default:break;
  }if(dirty)persist(t);next(Status,t);
 }
};
}
