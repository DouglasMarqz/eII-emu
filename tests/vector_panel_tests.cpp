#include "vector_panel_layout.h"
#include "runtime_audio.h"
#include <iostream>
#include <stdexcept>
#include <bit>
static void require(bool b,const char*m){if(!b)throw std::runtime_error(m);}
int main()try{using namespace eii;using namespace eii::panel;std::array<bool,30> seen{};
 for(unsigned i=0;i<keys.size();++i){const auto& k=keys[i];auto id=unsigned(k.id);require(id<30&&!seen[id],"duplicate/missing firmware contact");seen[id]=true;require(keyAt({k.x,k.y})==int(i),"key center hit");for(unsigned j=0;j<i;++j)require(!keys[j].rect().contains({k.x,k.y}),"overlapping hit targets");
  for(auto wh:{Point{1000,430},Point{1580,670},Point{3840,2160},Point{800,1400}}){auto v=Viewport::fit(wh.x,wh.y);auto p=v.design(v.screen({k.x,k.y}));require(keyAt(p)==int(i),"resized contact routing");}}
 for(bool s:seen)require(s,"unrepresented physical key");require(keyAt({-1,-1})==-1,"outside panel hit");
 // Physical master keypad 1..9 is read left-to-right/top-to-bottom, with 7=No and 9=Yes.
 const ScannerButton expected[]={ScannerButton::Digit1,ScannerButton::Digit2,ScannerButton::Digit3,ScannerButton::Digit4,ScannerButton::Digit5,ScannerButton::Digit6,ScannerButton::Digit7,ScannerButton::Digit8,ScannerButton::Digit9};
 for(unsigned i=0;i<9;++i){int k=keyAt({471.f+58*(i%3),334.f+59*(i/3)});require(k>=0&&keys[unsigned(k)].id==expected[i],"physical keypad routing");}
 unsigned traces=0;
 for(unsigned i=0;i<hostButtons.size();++i){const auto& b=hostButtons[i];traces+=b.command==125;
  for(unsigned j=0;j<i;++j){const auto& a=hostButtons[j].rect;require(!(b.rect.x<a.x+a.w&&b.rect.x+b.rect.w>a.x&&b.rect.y<a.y+a.h&&b.rect.y+b.rect.h>a.y),"host command buttons overlap");}
  for(const auto& f:faders){const auto& r=f.track;require(!(b.rect.x<r.x+r.w+17&&b.rect.x+b.rect.w>r.x-17&&b.rect.y<r.y+r.h+7&&b.rect.y+b.rect.h>r.y-7),"trace/host button overlaps fader");}}
 require(traces==1&&hostButtons[0].command==125,"single accessible trace command and dynamic label target");
 for(unsigned i=0;i<faders.size();++i){auto r=faders[i].track;require(faderAt({r.x+r.w/2,r.y+r.h/2})==int(i),"fader hit");require(faderVolts(i,{r.x,r.y})==5&&faderVolts(i,{r.x,r.y+r.h})==0,"fader endpoint polarity");}
 host::AudioQueue q;host::StereoFrame input[]={{.25f,-.5f},{-.75f,.125f}},out[2];q.push(input);q.pop(out);for(unsigned i=0;i<2;++i){require(std::bit_cast<unsigned>(input[i].left)==std::bit_cast<unsigned>(out[i].left),"default PCM changed");require(std::bit_cast<unsigned>(input[i].right)==std::bit_cast<unsigned>(out[i].right),"default right PCM changed");}
 q.push(input);q.setOutputGain(.5f);q.pop(out);require(out[0].left==.125f&&out[1].right==.0625f,"host volume does not apply at consumer");q.push(input);q.setOutputGain(0);q.pop(out);require(out[0].left==0&&out[1].right==0,"host mute");
 std::cout<<"PASS:30 distinct firmware contacts; keypad No/Yes; resized/letterboxed hit tests;6 ADC fader ranges; default PCM bit-exact; host gain/mute\n";
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
