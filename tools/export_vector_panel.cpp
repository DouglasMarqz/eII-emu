// Standalone design preview. Shares all hit geometry, IDs and legends with Win64.
#include "vector_panel_layout.h"
#include "vector_panel_logos.h"
#include <iostream>
#include <codecvt>
#include <locale>
#include <sstream>
using namespace eii::panel;
static std::string u(const wchar_t*s){std::wstring_convert<std::codecvt_utf8<wchar_t>> c;auto r=c.to_bytes(s);std::string out;for(char x:r){if(x=='&')out+="&amp;";else if(x=='<')out+="&lt;";else if(x=='>')out+="&gt;";else out+=x;}return out;}
static void rect(float x,float y,float w,float h,const char*c,float radius=0){std::cout<<"<rect x='"<<x<<"' y='"<<y<<"' width='"<<w<<"' height='"<<h<<"' rx='"<<radius<<"' fill='"<<c<<"'/>\n";}
static void txt(float x,float y,const wchar_t*t,int size=12,const char*color="#e6e6d8",bool middle=false){std::cout<<"<text x='"<<x<<"' y='"<<y<<"' font-size='"<<size<<"' fill='"<<color<<"'"<<(middle?" text-anchor='middle'":"")<<">";std::wistringstream lines(t);std::wstring s;bool first=true;while(std::getline(lines,s)){std::cout<<"<tspan x='"<<x<<"' dy='"<<(first?0:(size<=11?11.7:13))<<"'>"<<u(s.c_str())<<"</tspan>";first=false;}std::cout<<"</text>\n";}
static void module(float x,float y,float w,float h,const wchar_t*t){rect(x-3,y-3,w+6,h+6,"#ddd7c1",19);rect(x,y,w,h,"#1c354b",17);rect(x+2,y+2,w-4,44,"#ddd7c1",12);rect(x+2,y+35,w-4,11,"#ddd7c1");rect(x+5,y+44,w-10,2,"#8d3631");txt(x+16,y+32,t,30,"#272f30");}
template<std::size_t N> static void logo(const std::array<logos::Command,N>& commands,float w,float h,Rect box,const char*color){
 float scale=std::min(box.w/w,box.h/h);std::cout<<"<path fill='"<<color<<"' fill-rule='nonzero' transform='translate("<<box.x+(box.w-w*scale)/2<<","<<box.y+(box.h-h*scale)/2<<") scale("<<scale<<")' d='";
 for(auto&c:commands){std::cout<<c.op;unsigned count=c.op=='C'?6:(c.op=='M'||c.op=='L')?2:0;for(unsigned i=0;i<count;++i)std::cout<<c.p[i]<<' ';}std::cout<<"'/>\n";
}
int main(){std::cout<<"<svg xmlns='http://www.w3.org/2000/svg' width='1960' height='760' viewBox='0 0 1960 760'><title>Emulator II vector layout preview</title><desc>Design preview, not a running Windows screenshot. Live firmware LCD replaces the placeholder in the application.</desc><defs><linearGradient id='key' x2='0' y2='1'><stop stop-color='#45433d'/><stop offset='1' stop-color='#0d1114'/></linearGradient><linearGradient id='cap' x2='0' y2='1'><stop stop-color='#d5d1bc'/><stop offset='1' stop-color='#565349'/></linearGradient></defs><g font-family='Arial, sans-serif' xml:space='preserve'>\n";
 rect(0,0,1960,760,"#272929");rect(5,5,1950,605,"#ddd7c1",22);module(17,17,608,580,L"");module(640,17,310,580,L"Sequencer");module(970,17,975,298,L"");module(815,415,135,182,L"Enter");module(970,335,450,262,L"Real Time Control");module(1440,335,300,262,L"Special    Disk");module(1760,335,185,262,L"Sample");
 rect(20,20,275,95,"#ddd7c1");logo(logos::emulator,logos::emulatorWidth,logos::emulatorHeight,{34,44,250,56},"#1c354b");txt(314,52,L"Master Control",30,"#272f30");txt(986,52,L"Filter",30,"#272f30");txt(1173,52,L"VCA/LFO",30,"#272f30");txt(1365,52,L"Voice Definition",28,"#272f30");txt(1690,52,L"Preset Definition",28,"#272f30");
 for(auto&l:legends)txt(l.x,l.y+l.pitch-3,l.lines,int(l.size));for(auto&x:legendBoxes)txt(x.x+x.w/2,x.y+11,x.label,10,"#e6e6d8",true);
 for(auto&k:keys){auto r=k.rect();rect(r.x-4,r.y-4,r.w+8,r.h+8,"#090e11");rect(r.x,r.y,r.w,r.h,k.red?"#da3d2f":"url(#key)");bool digit=unsigned(k.id)>=8&&unsigned(k.id)<=17;if(!(k.x>970&&k.y<315))txt(k.x,digit?k.y-28:k.y+49,k.label,digit?9:12,"#e6e6d8",true);if(eii::ScannerControls::ledPin(k.id).latch>=0)std::cout<<"<circle cx='"<<k.x<<"' cy='"<<k.led().y+10<<"' r='10' fill='#6f2c27'/><circle cx='"<<k.x-3<<"' cy='"<<k.led().y+7<<"' r='3' fill='#a0634a'/>";}
 for(auto&f:faders){if(f.control<4){for(int j=0;j<11;++j)rect(f.track.x-35,f.track.y+j*f.track.h/10,70,.7,"#9b9e92");txt(f.track.x+10,f.track.y+f.track.h+32,f.label,16,"#e6e6d8",true);}else txt(f.track.x+13,745,f.label,12,"#ddd7c1",true);rect(f.track.x+f.track.w/2-3,f.track.y,6,f.track.h,"#070f17");float y=f.track.y+f.track.h*(f.control==4?.5f:1.f);rect(f.track.x-17,y-7,f.track.w+34,14,"url(#cap)");rect(f.track.x-15,y,f.track.w+30,1,"#272f30");}
 std::cout<<"<circle cx='321' cy='179' r='29' fill='#1c1c19' stroke='#615e52' stroke-width='3'/><path d='M315 174 L302 160' stroke='#ddd7c1' stroke-width='4'/>";txt(322,232,L"MIX OUT\nVOLUME",12,"#e6e6d8",true);
 rect(lcd.x-8,lcd.y-8,lcd.w+16,lcd.h+16,"#1b1e1c");rect(lcd.x,lcd.y,lcd.w,lcd.h,"#91a670");txt(lcd.x+15,lcd.y+28,L"EMULATOR II",18,"#1e3826");txt(lcd.x+15,lcd.y+53,L"VECTOR PREVIEW",14,"#1e3826");
 txt(1812,465,L"INPUT GAIN\n(recording pending)",9,"#a0a6a3",true);std::cout<<"<circle cx='1797' cy='426' r='22' fill='#2a2b28'/><path d='M1797 426 L1785 414' stroke='#828070' stroke-width='3'/>";txt(1512,511,L"11–CATALOG SPECIAL\nFUNCTIONS",10,"#e6e6d8",true);
 txt(25,646,L"VECTOR PREVIEW  •  NOT A RUNTIME CAPTURE",12,"#ddd7c1");logo(logos::company,logos::companyWidth,logos::companyHeight,{1500,613,435,36},"#ddd7c1");
 for(auto&b:hostButtons){rect(b.rect.x,b.rect.y,b.rect.w,b.rect.h,"#374144",6);txt(b.rect.x+b.rect.w/2,b.rect.y+33,b.label,15,"#ddd7c1",true);}txt(1750,725,L"FIRMWARE FRONT PANEL  /  0.70",14,"#91a19e",true);
 std::cout<<"</g></svg>\n";
}
