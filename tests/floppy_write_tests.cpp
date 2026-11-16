#include "disk_sio.h"
#include "disk_dma.h"
#include "floppy_hfe.h"
#include "hdd_image_file.h"
#include "direct_image_file.h"
#include "floppy_image_backing.h"
#include <iostream>
#include <chrono>
static void check(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
int main()try{
 std::array<std::uint8_t,8> wr{};wr[4]=0x10;wr[5]=0x6f;wr[6]=0xfa;wr[7]=0x96;
 eii::SioSyncTransmitter tx;tx.resetCrc();tx.resetUnderrun();
 std::vector<std::uint8_t> payload(3584);for(unsigned i=0;i<payload.size();++i)payload[i]=std::uint8_t(i*37+(i>>8));
 std::vector<bool> bits;unsigned sent=0;for(unsigned i=0;i<payload.size()*8+16;++i){if(tx.empty&&sent<payload.size())tx.write(payload[sent++]);bits.push_back(tx.clock(wr));}
 for(unsigned i=0;i<payload.size()*8;++i)check(bits[i]==bool((payload[i/8]>>(i%8))&1),"actual TX serialized payload");
 auto crc=eii::emuiiCrc(payload);for(unsigned i=0;i<16;++i)check(bits[payload.size()*8+i]==bool((crc>>i)&1),"TX generated CRC bits");
 check(tx.crcFrames==1&&tx.overwrites==0,"CRC underrun and holding register");
 eii::DiskSio sio;auto out=[&](unsigned reg,unsigned v){sio.write(0x39,reg);sio.write(0x39,v);};out(4,0x10);out(5,0x6f);out(1,0xc0);check(!sio.readyN(),"TX Ready when holding empty");sio.write(0x38,3);check(sio.readyN(),"TX Ready waits for holding byte");sio.txClock();check(!sio.readyN(),"TX Ready reasserts on shift load");
 eii::DiskDma dma;dma.floppyIo=true;for(auto v:{0xc3,0x7a,0x00,0x20,0x07,0x00,0x14,0x28,0x80,0x95,0x38,0x12,0xfc,0x92,0xcf,0xab,0x87})dma.write(v);
 check(dma.supported(),"source-only search supported for floppy verify");
 for(unsigned i=0;i<200&&!dma.endOfBlock;++i){if(dma.request(false))dma.grant();auto p=dma.pins();check(!(p&Z80_WR),"search must never write destination");dma.tick(false,42);}
 check(dma.endOfBlock&&dma.transfers==8&&!dma.matchFound,"search counts actual reads to EOB");
 // Synthetic HFEv1:80cylinders,2 sides, nominal FM with two samples per cell.
 std::vector<std::uint8_t> initial(eii::EmuiiImage::imageBytes,0xa5);eii::EmuiiImage image(initial);
 constexpr unsigned sideBytes=15625,trackStorage=31744;
 std::vector<std::uint8_t> hfe(1024+80*trackStorage);std::copy_n("HXCPICFE",8,hfe.begin());hfe[9]=80;hfe[10]=2;hfe[11]=3;hfe[12]=156;hfe[16]=0x0b;hfe[18]=1;hfe[22]=hfe[24]=255;
 for(unsigned c=0;c<80;++c){const unsigned offset=1024+c*trackStorage,block=offset/512,len=sideBytes*2;hfe[512+c*4]=block&255;hfe[513+c*4]=block>>8;hfe[514+c*4]=len&255;hfe[515+c*4]=len>>8;
  for(unsigned side=0;side<2;++side){auto track=image.format(c,side,eii::EmuiiGap::Hxc2026);for(unsigned cell=0;cell<track.transitions.size();++cell)if(track.transitions[cell]){const unsigned bit=cell*2,byte=bit/8;hfe[offset+(byte/256)*512+side*256+byte%256]|=1<<(bit%8);}}}
 eii::HfeImage container(hfe);check(container.extractEmuii()==initial,"synthetic HFE extraction");container.replaceSector(37,1,payload);
 auto extracted=container.extractEmuii();std::copy(payload.begin(),payload.end(),initial.begin()+(37*2+1)*3584);check(extracted==initial,"HFE patched data and CRC; all neighboring sectors retained");
 const auto changed=container.bytes();for(unsigned i=0;i<hfe.size();++i)if(hfe[i]!=changed[i])check(i>=1024+37*trackStorage&&i<1024+38*trackStorage&&((i-(1024+37*trackStorage))/256)%2==1,"HFE header/gaps/other-side storage retained");
 namespace fs=std::filesystem;const auto dir=fs::temp_directory_path()/("eii-rw-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));check(fs::create_directory(dir),"own temp dir");struct Cleanup{fs::path p;~Cleanup(){std::error_code e;fs::remove_all(p,e);}}clean{dir};auto path=dir/"direct.emuiifd";
 const auto hfePath=dir/"working.hfe";eii::host::saveNewImage(hfePath,hfe);
 {
  eii::host::FloppyImageBacking copy(hfePath,false);copy.image->replacePayload(37,1,payload);check(!copy.trackWritten(37,1,payload)&&copy.dirty,"protected backing dirty");
  std::ifstream in(hfePath,std::ios::binary);std::vector<std::uint8_t> same((std::istreambuf_iterator<char>(in)),{});check(same==hfe,"protected original unchanged");in.close();
  copy.saveCopy(dir/"copy.hfe");check(!copy.dirty,"explicit protected copy save");
 }
 {
  eii::host::FloppyImageBacking live(hfePath,true);live.image->replacePayload(37,1,payload);check(live.trackWritten(37,1,payload)&&!live.dirty&&live.direct,"direct backing persisted");
  std::ifstream in(hfePath,std::ios::binary);std::vector<std::uint8_t> persisted((std::istreambuf_iterator<char>(in)),{});check(eii::HfeImage(persisted).extractEmuii()==initial,"direct HFE retains format and actual payload");
 }
 eii::host::saveNewImage(path,initial);{
  eii::host::DirectImageFile lease(path,initial);bool rejected=false;try{eii::host::DirectImageFile second(path,initial);}catch(...){rejected=true;}check(rejected,"duplicate direct writer rejected");
  auto updated=initial;updated[100]^=1;updated[updated.size()-2]^=2;lease.persist(updated);std::ifstream f(path,std::ios::binary);std::vector<std::uint8_t> disk((std::istreambuf_iterator<char>(f)),{});check(disk==updated,"direct changes persisted and neighbors retained");f.close();
  // POSIX advisory locks let another writer truncate; the lease must then refuse.
  // Windows denies the competing open itself (docs/media-write-policy.md).
  bool denied=false;try{fs::resize_file(path,10);}catch(const fs::filesystem_error&){denied=true;}
  rejected=false;if(!denied)try{lease.persist(initial);}catch(...){rejected=true;}check(denied||rejected,"external extent change denied by the lease or rejects further writes");
 }
 std::cout<<"PASS SIO TX serialization/CRC/Ready, DMA search source-only, HFE in-format CRC patch and neighbors, direct writer lease/persistence/failure\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
