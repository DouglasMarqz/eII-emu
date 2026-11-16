#include "floppy_hfe.h"
#include <algorithm>
#include <iostream>
#include <string>
using Bytes=std::vector<std::uint8_t>;
static void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
template<class F>static void rejects(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}require(caught,"malformed HFE accepted");}
// Independent MSB-first polynomial division, with input/output reflection.
static unsigned reverse(unsigned b,unsigned n){unsigned r=0;while(n--){r=(r<<1)|(b&1);b>>=1;}return r;}
static unsigned crc(const Bytes& b){unsigned r=0;for(auto v:b){r^=reverse(v,8)<<8;for(int i=0;i<8;++i){r<<=1;if(r&65536)r^=0x18005;}}return reverse(r,16);}
static Bytes payload(unsigned id){Bytes b(3584);for(unsigned i=0;i<b.size();++i)b[i]=std::uint8_t((i*73)^(i>>8)^id);return b;}
static void emit(Bytes& cells,const Bytes& bytes){for(auto v:bytes)for(unsigned k=0;k<8;++k){cells.push_back(1);cells.push_back((v>>k)&1);}}
static void checked(Bytes& cells,Bytes b){const auto c=crc(b);b.push_back(c&255);b.push_back(c>>8);emit(cells,b);}
static Bytes revolution(unsigned id,unsigned count=62500,bool duplicate=false){Bytes c;emit(c,{255,255,0,0,0xfa,0x96});checked(c,{std::uint8_t(id)});emit(c,{255,0,0xfa,0x96});checked(c,payload(id));if(duplicate){const auto frame=c;c.insert(c.end(),frame.begin(),frame.end());}c.resize(count,1);return c;}
static void word(Bytes& b,unsigned p,unsigned v){b[p]=v&255;b[p+1]=v>>8;}
static Bytes hfe(unsigned tracks=1,unsigned phase=1,unsigned rotate=0,bool duplicate=false){const unsigned cells=duplicate?125000:62500,sideBytes=cells/4,storage=((sideBytes+255)/256)*512;Bytes b(1024+tracks*storage);std::copy_n("HXCPICFE",8,b.begin());b[9]=tracks;b[10]=2;b[11]=3;word(b,12,312);b[16]=11;word(b,18,1);b[22]=b[24]=255;
for(unsigned t=0;t<tracks;++t){const unsigned offset=1024+t*storage;word(b,512+t*4,offset/512);word(b,514+t*4,sideBytes*2);for(unsigned s=0;s<2;++s){auto c=revolution(t*2+s,cells,duplicate);std::rotate(c.begin(),c.begin()+rotate,c.end());for(unsigned i=0;i<c.size();++i)if(c[i]){const unsigned bit=2*i+phase,j=bit/8;b[offset+(j/256)*512+s*256+j%256]|=1<<(bit%8);}}}return b;}
static void flipCell(Bytes& b,unsigned cell){const unsigned bit=cell*2+1,j=bit/8;b[1024+(j/256)*512+j%256]^=1<<(bit%8);}
int main()try{
require(crc(Bytes{'1','2','3','4','5','6','7','8','9'})==0xbb3d,"CRC oracle golden vector");
for(unsigned phase:{0u,1u})for(unsigned rotation:{0u,2000u,62480u}){eii::HfeImage image(hfe(1,phase,rotation));for(unsigned s=0;s<2;++s){const auto sector=image.sector(0,s);require(sector.samplePhase==phase&&std::equal(sector.data.begin(),sector.data.end(),payload(s).begin()),"side interleave, phase or circular frame");}rejects([&]{image.extractEmuii();});rejects([&]{image.sector(1,0);});}
auto full=hfe(80);const auto decoded=eii::HfeImage(full).extractEmuii();for(unsigned id=0;id<160;++id)require(std::equal(decoded.begin()+id*3584,decoded.begin()+(id+1)*3584,payload(id).begin()),"full160 payload order");
// Header ID CRC begins at cell112; data begins at cell208.
for(unsigned cell:{113u,209u,208u}){auto b=hfe();flipCell(b,cell);rejects([&]{eii::HfeImage(b).sector(0,0);});}
// A valid sector from a different physical side must not satisfy the requested ID.
auto wrong=hfe();for(unsigned block=1024;block<wrong.size();block+=512)std::swap_ranges(wrong.begin()+block,wrong.begin()+block+256,wrong.begin()+block+256);rejects([&]{eii::HfeImage(wrong).sector(0,0);});
rejects([]{eii::HfeImage(hfe(1,1,0,true)).sector(0,0);});
for(unsigned size:{0u,511u,515u,1024u,32000u}){auto b=hfe();b.resize(size);rejects([&]{eii::HfeImage image(b);image.sector(0,0);});}
for(unsigned field:{8u,10u,11u,16u,22u}){auto b=hfe();b[field]=99;rejects([&]{eii::HfeImage image(b);});}
auto overlap=hfe(2);word(overlap,516,2);rejects([&]{eii::HfeImage image(overlap);});
auto missing=full;std::fill(missing.end()-31744,missing.end(),0);rejects([&]{eii::HfeImage(missing).extractEmuii();});
// Trailing sync candidate must not read beyond the revolution buffer.
auto tail=hfe();std::fill(tail.begin()+1024,tail.end(),0);Bytes sync;emit(sync,{0xfa,0x96});for(unsigned i=0;i<32;++i)if(sync[i])flipCell(tail,62500-32+i);rejects([&]{eii::HfeImage(tail).sector(0,0);});
std::cout<<"PASS: independent CRC oracle;160 interleaved sectors;both pulse phases;circular frames;header/data CRC and clock corruption;wrong physical ID;duplicate/missing sectors;geometry/revision/overlap/truncation guards.\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
