#pragma once
#include "vector_panel_logos.h"
#include "windows_paint_clip.h"
#include "vector_panel_layout.h"
#include "front_panel_session.h"
#include "gui_stats.h"
#include <gdiplus.h>
#include <functional>
#include <map>
#include <memory>
#include <tuple>
#include <string>
#include <windowsx.h>
namespace eii::host {
class VectorPanelWindow {
public:
 struct Hooks {
  std::function<void(ScannerButton,bool)> contact;
  std::function<void(unsigned,double)> voltage;
  std::function<void(float)> volume;
  std::function<void(float)> inputGain; // panel GAIN pot (7-42 +INPUT.V attenuation)
  std::function<void(unsigned)> command;
  std::function<void()> save,close;
  std::function<std::array<unsigned,7>(unsigned char)> glyph;
 };
 VectorPanelWindow(Hooks hooks,const std::array<double,6>& controls,float gain,float inputGain=1):hooks_(std::move(hooks)),controls_(controls),gain_(gain),inputGain_(inputGain) {
  Gdiplus::GdiplusStartupInput in;if(Gdiplus::GdiplusStartup(&gdip_,&in,nullptr)!=Gdiplus::Ok)throw std::runtime_error("Vector graphics initialization failed");
  WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"EIIVectorFrontPanel";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&cls);
  hwnd_=CreateWindowExW(0,cls.lpszClassName,L"Emulator II  •  vector front panel 0.71",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1580,670,nullptr,nullptr,cls.hInstance,this);
  if(!hwnd_){Gdiplus::GdiplusShutdown(gdip_);gdip_=0;throw std::runtime_error("Could not create vector panel");}
 }
 ~VectorPanelWindow(){if(hwnd_)DestroyWindow(hwnd_);clearCache();fonts().clear();if(gdip_)Gdiplus::GdiplusShutdown(gdip_);}
 void show(){ShowWindow(hwnd_,SW_SHOW);UpdateWindow(hwnd_);}
 HWND handle()const{return hwnd_;}
 void traceState(unsigned state){if(traceState_==state)return;traceState_=state;dirty(panel::hostButtons[0].rect);}
 void update(const PanelSnapshot& next) {
  bool lcd=next.lcd!=snapshot_.lcd||next.lcdOn!=snapshot_.lcdOn;
  for(const auto& k:panel::keys)if(!(ScannerControls::ledState(next.leds,k.id)==ScannerControls::ledState(snapshot_.leds,k.id)))dirty(k.led());
  snapshot_=next;if(lcd)dirty(panel::lcd);
  if(next.loaded)for(unsigned i=0;i<6;++i){const auto input=i<4?unsigned(ScannerAnalogInput::SliderA)+i:i==4?unsigned(ScannerAnalogInput::LeftWheel):unsigned(ScannerAnalogInput::RightWheel);auto cv=next.controls[i];(void)input;if(drag_!=int(i)&&cv.known&&std::abs(controls_[i]-cv.volts)>.0001){controls_[i]=cv.volts;dirty(faderBounds(i));}}
  auto state=std::wstring(next.loaded?(next.running?L"RUNNING":L"PAUSED"):L"SETTINGS: SELECT ROM / DISK")+L"   •   "+std::to_wstring(int(next.speedRatio*100+.5))+L"%   •   "+std::to_wstring(next.ticks/4000000)+L" s"+((next.nativeAudio||next.reconstruction)?std::wstring(L"\n")+(next.nativeAudio?L"NATIVE DAC  ":L"")+(next.reconstruction?L"S3528":L""):L"");
  if(state!=status_){status_=std::move(state);dirty({25,625,300,50});}
 }
private:
 using Color=Gdiplus::Color;using Graphics=Gdiplus::Graphics;using RectF=Gdiplus::RectF;
 Hooks hooks_;HWND hwnd_=nullptr;ULONG_PTR gdip_=0;HBITMAP cache_=nullptr;int cacheW_=0,cacheH_=0;bool sizing_=false,stale_=false;
 unsigned traceState_=0;
 PanelSnapshot snapshot_;std::array<double,6> controls_;float gain_=1,inputGain_=1;std::wstring status_;
 int held_=-1,drag_=-1,focus_=-1;WPARAM heldKey_=0;float dragY_=0,dragGain_=0,grabDy_=0;bool closing_=false;
 inline static const Color navy=Color(255,47,78,122),cream=Color(255,222,223,220),ink=Color(255,55,62,82),red=Color(255,158,55,58),white=Color(255,238,240,242),slate=Color(255,96,108,140),band=Color(255,201,202,198),rim=Color(255,244,245,245);
 panel::Viewport viewport()const {RECT r;GetClientRect(hwnd_,&r);return panel::Viewport::fit(float(r.right),float(r.bottom));}
 void clearCache(){if(cache_){DeleteObject(cache_);cache_=nullptr;}cacheW_=cacheH_=0;}
 static RectF rf(panel::Rect r){return {r.x,r.y,r.w,r.h};}
 static panel::Rect faderBounds(unsigned i){auto r=panel::faders[i].track;return {r.x-25,r.y-8,r.w+50,r.h+16};}
 void dirty(panel::Rect r){if(!hwnd_)return;r.x-=9;r.y-=9;r.w+=18;r.h+=18;auto v=viewport();RECT p{LONG(std::floor(v.x+r.x*v.scale))-3,LONG(std::floor(v.y+r.y*v.scale))-3,LONG(std::ceil(v.x+(r.x+r.w)*v.scale))+3,LONG(std::ceil(v.y+(r.y+r.h)*v.scale))+3};InvalidateRect(hwnd_,&p,FALSE);}
 static void fill(Graphics&g,Color c,panel::Rect r){Gdiplus::SolidBrush b(c);g.FillRectangle(&b,rf(r));}
 static void line(Graphics&g,Color c,float x,float y,float x2,float y2,float width=1){Gdiplus::Pen p(c,width);g.DrawLine(&p,x,y,x2,y2);}
 static void text(Graphics&g,const wchar_t*s,panel::Rect r,float size,Color c,bool center=false,bool bold=false,const wchar_t*family=L"Arial") {
  auto& font=cachedFont(family,size,bold?Gdiplus::FontStyleBold:Gdiplus::FontStyleRegular);Gdiplus::SolidBrush b(c);Gdiplus::StringFormat format;format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);format.SetAlignment(center?Gdiplus::StringAlignmentCenter:Gdiplus::StringAlignmentNear);format.SetLineAlignment(Gdiplus::StringAlignmentNear);g.DrawString(s,-1,&font,rf(r),&format,&b);
 }
 static void rounded(Graphics&g,panel::Rect r,Color color,float radius=18,bool border=false) {
  Gdiplus::GraphicsPath p;const float d=radius*2;p.AddArc(r.x,r.y,d,d,180,90);p.AddArc(r.x+r.w-d,r.y,d,d,270,90);p.AddArc(r.x+r.w-d,r.y+r.h-d,d,d,0,90);p.AddArc(r.x,r.y+r.h-d,d,d,90,90);p.CloseFigure();Gdiplus::SolidBrush b(color);g.FillPath(&b,&p);if(border){Gdiplus::Pen pen(cream,5);g.DrawPath(&pen,&p);}
 }
 // GDI+ font objects are costly to build; the static panel uses a few dozen
 // (face,size,style) combinations, so they are created once per window.
 struct FontKey{std::wstring face;float size;int style;bool operator<(const FontKey&o)const{return std::tie(face,size,style)<std::tie(o.face,o.size,o.style);}};
 static std::map<FontKey,std::unique_ptr<Gdiplus::Font>>& fonts(){static std::map<FontKey,std::unique_ptr<Gdiplus::Font>> m;return m;}
 static Gdiplus::Font& cachedFont(const wchar_t* family,float size,int style){auto& m=fonts();FontKey k{family,size,style};auto it=m.find(k);
  if(it==m.end()){Gdiplus::FontFamily ff(family);it=m.emplace(k,std::make_unique<Gdiplus::Font>(&ff,size,style,Gdiplus::UnitPixel)).first;}return *it->second;}
 static constexpr const wchar_t* legendFace=L"Arial";
 static constexpr const wchar_t* titleFace=L"Constantia";
 // Printed-panel faces with a safe fallback when a Windows font is absent.
 static const wchar_t* face(const wchar_t* wanted){static std::map<std::wstring,const wchar_t*> known;auto it=known.find(wanted);if(it!=known.end())return it->second;
  const wchar_t* pick=L"Arial";for(const wchar_t* c:{wanted,wanted==legendFace?L"Bahnschrift SemiBold Condensed":L"Palatino Linotype"}){Gdiplus::FontFamily f(c);if(f.IsAvailable()){pick=c;break;}}
  known.emplace(wanted,pick);return pick;}
 // Fine flecks on the blue fields: one small tile built once and used as a
 // texture brush, so window resizing never redraws thousands of dots.
 static Gdiplus::Bitmap& fieldTexture(){
  static Gdiplus::Bitmap* tile=[]{auto* b=new Gdiplus::Bitmap(96,96,PixelFormat32bppARGB);std::uint32_t s=0x9e3779b9u;
   for(int y=0;y<96;++y)for(int x=0;x<96;++x){s=s*1664525u+1013904223u;const int n=int((s>>24)%23)-11;const int v=(s>>20)%41==0?28:(s>>20)%37==0?-24:0;
    auto c=[&](int base){return BYTE(std::clamp(base+n/3+v,0,255));};b->SetPixel(x,y,Color(255,c(47),c(78),c(122)));}
   return b;}();
  return *tile;}
 static void field(Graphics&g,panel::Rect r,float radius){Gdiplus::GraphicsPath p;const float d=radius*2;p.AddArc(r.x,r.y,d,d,180,90);p.AddArc(r.x+r.w-d,r.y,d,d,270,90);p.AddArc(r.x+r.w-d,r.y+r.h-d,d,d,0,90);p.AddArc(r.x,r.y+r.h-d,d,d,90,90);p.CloseFigure();
  Gdiplus::TextureBrush b(&fieldTexture());g.FillPath(&b,&p);Gdiplus::Pen pen(Color(255,240,241,242),4);g.DrawPath(&pen,&p);}
 // Section title on the grey band: the red rule stops either side of the
 // lettering, the slate rule below runs the full width.
 static void title(Graphics&g,const wchar_t*s,float x,float y,float size){
  auto& font=cachedFont(face(titleFace),size,Gdiplus::FontStyleRegular);RectF box;g.MeasureString(s,-1,&font,Gdiplus::PointF(x,y),&box);
  fill(g,band,{x-5,y+20,box.Width+8,y+45.8f-(y+20)});Gdiplus::SolidBrush b(ink);g.DrawString(s,-1,&font,Gdiplus::PointF(x,y+4),&b);}
 // Module as printed: white rounded rim; inside it a rounded field whose top
 // band is grey (title, red rule broken by the lettering, slate rule below)
 // and whose remainder is the speckled blue.
 static Gdiplus::GraphicsPath* roundPath(panel::Rect r,float radius){auto* p=new Gdiplus::GraphicsPath;const float d=radius*2;p->AddArc(r.x,r.y,d,d,180,90);p->AddArc(r.x+r.w-d,r.y,d,d,270,90);p->AddArc(r.x+r.w-d,r.y+r.h-d,d,d,0,90);p->AddArc(r.x,r.y+r.h-d,d,d,90,90);p->CloseFigure();return p;}
 static void module(Graphics&g,panel::Rect r,const wchar_t*s,float titleSize=30){rounded(g,r,rim,18);const panel::Rect in{r.x+4,r.y+4,r.w-8,r.h-8};std::unique_ptr<Gdiplus::GraphicsPath> p(roundPath(in,14));
  Gdiplus::GraphicsState state=g.Save();g.SetClip(p.get(),Gdiplus::CombineModeIntersect);{Gdiplus::TextureBrush b(&fieldTexture());g.FillRectangle(&b,rf(in));}fill(g,band,{in.x,in.y,in.w,42.5f});
  line(g,red,in.x+2,r.y+43,in.x+in.w-2,r.y+43,2);line(g,slate,in.x,r.y+46.5f,in.x+in.w,r.y+46.5f,1.5f);g.Restore(state);if(*s)title(g,s,r.x+22,r.y,titleSize);}
 // One legend row per item, each underlined across the column width.
 static void legend(Graphics&g,const panel::Legend&l){auto& font=cachedFont(face(legendFace),l.size,Gdiplus::FontStyleBold);Gdiplus::SolidBrush b(white);Gdiplus::FontFamily fam;font.GetFamily(&fam);const float ascent=l.size*fam.GetCellAscent(Gdiplus::FontStyleBold)/float(fam.GetEmHeight(Gdiplus::FontStyleBold));
  const wchar_t*p=l.lines;float y=l.y;while(*p){const wchar_t*e=p;while(*e&&*e!=L'\n')++e;const bool indent=*p==L'\t';const wchar_t*q=indent?p+1:p;
   if(e>q){g.DrawString(q,INT(e-q),&font,Gdiplus::PointF(l.x+(indent?10.f:0.f),y+l.pitch-1.f-2.f-ascent),&b);line(g,Color(235,236,238,240),l.x,y+l.pitch-1.f,l.x+l.w,y+l.pitch-1.f,.8f);}
   y+=l.pitch;p=*e?e+1:e;}}
 template<std::size_t N> static void logo(Graphics& g,const std::array<panel::logos::Command,N>& commands,float width,float height,panel::Rect box,Color color) {
  const float scale=std::min(box.w/width,box.h/height);const float x=box.x+(box.w-width*scale)/2,y=box.y+(box.h-height*scale)/2;
  Gdiplus::GraphicsPath path;Gdiplus::PointF at{};
  for(const auto& c:commands){auto point=[&](unsigned i){return Gdiplus::PointF(x+c.p[i]*scale,y+c.p[i+1]*scale);};
   if(c.op=='M'){path.StartFigure();at=point(0);}else if(c.op=='L'){auto next=point(0);path.AddLine(at,next);at=next;}
   else if(c.op=='C'){auto next=point(4);path.AddBezier(at,point(0),point(2),next);at=next;}else if(c.op=='Z')path.CloseFigure();}
  Gdiplus::SolidBrush brush(color);path.SetFillMode(Gdiplus::FillModeWinding);g.FillPath(&brush,&path);
 }
 // Module rectangles, titles and logo boxes follow the photographed overlay
 // (see vector_panel_layout.h for the mapping); blue fields start 46 units
 // below each module's top edge.
 static void knob(Graphics&g,panel::Rect r,float value){const float cx=r.x+r.w/2,cy=r.y+r.h/2,rad=r.w/2;
  {Gdiplus::SolidBrush shadow(Color(90,0,0,0));g.FillEllipse(&shadow,RectF(r.x+2,r.y+3,r.w,r.h));}
  Gdiplus::GraphicsPath p;p.AddEllipse(rf(r));Gdiplus::PathGradientBrush b(&p);b.SetCenterPoint(Gdiplus::PointF(cx-rad*.3f,cy-rad*.35f));b.SetCenterColor(Color(255,58,58,56));Color rim(255,10,10,10);INT n=1;b.SetSurroundColors(&rim,&n);g.FillEllipse(&b,rf(r));
  Gdiplus::Pen ridge(Color(255,30,30,30),1.f);for(int i=0;i<24;++i){const float a=i*3.14159265f/12;line(g,Color(255,34,34,34),cx+rad*.86f*std::cos(a),cy+rad*.86f*std::sin(a),cx+rad*.98f*std::cos(a),cy+rad*.98f*std::sin(a),1.2f);}
  const float a=(-225+value*270)*3.14159265f/180;line(g,Color(255,240,240,236),cx+rad*.18f*std::cos(a),cy+rad*.18f*std::sin(a),cx+rad*.9f*std::cos(a),cy+rad*.9f*std::sin(a),3.5f);}
 void staticPanel(Graphics&g) {
  fill(g,Color(255,84,88,96),{0,0,panel::width,panel::height});rounded(g,{5,5,1950,605},cream,22);
  module(g,{22,8,629,589},L"");module(g,{657,8,323,589},L"Sequencer");
  module(g,{988,8,956,323},L"");
  title(g,L"Filter",1021,8,30);title(g,L"VCA/LFO",1195,8,30);title(g,L"Voice Definition",1364,8,30);title(g,L"Preset Definition",1676,8,30);
  // Enter sits in a notch of the Sequencer field: clear a frame-coloured gap first.
  rounded(g,{828,325,164,280},cream,20);module(g,{840,336,140,258},L"Enter");
  module(g,{989,333,333,264},L"Real Time Control");module(g,{1329,333,273,264},L"");title(g,L"Special",1345,333,30);title(g,L"Disk",1492,333,30);module(g,{1610,333,325,184},L"Sample");
  rounded(g,{8,8,303,114},Color(255,238,240,240),16);fill(g,Color(255,238,240,240),{8,8,40,114});fill(g,Color(255,238,240,240),{8,8,303,40});logo(g,panel::logos::emulator,panel::logos::emulatorWidth,panel::logos::emulatorHeight,{26,44,268,56},navy);
  title(g,L"Master Control",318,8,30);
  for(const auto& k:panel::keys) {
   const bool digit=unsigned(k.id)>=8&&unsigned(k.id)<=17;
   if(*k.label&&!(k.x>970&&k.y<315))text(g,k.label,{k.x-61,digit?k.y-37:k.y+27,122,37},digit?12:11.5f,white,true,true,face(legendFace));
  }
  text(g,L"NO",{412,474,120,20},11.5f,white,true,true,face(legendFace));text(g,L"YES",{527,474,120,20},11.5f,white,true,true,face(legendFace));
  for(const auto& f:panel::faders){if(f.control<4){for(unsigned j=0;j<11;++j)line(g,Color(150,225,228,232),f.track.x-28,f.track.y+j*f.track.h/10,f.track.x+48,f.track.y+j*f.track.h/10);text(g,f.label,{f.track.x-25,f.track.y+f.track.h+16,70,30},15,white,true,true,face(legendFace));}else text(g,f.label,{f.track.x-33,730,95,25},12,cream,true);}
  text(g,L"MIX OUT\nVOLUME",{311,232,80,40},11,white,true,true,face(legendFace));
  for(const auto& l:panel::legends)legend(g,l);
  {auto& font=cachedFont(face(legendFace),9.5f,Gdiplus::FontStyleBold);Gdiplus::SolidBrush b(white);Gdiplus::StringFormat c;c.SetAlignment(Gdiplus::StringAlignmentCenter);Gdiplus::Pen pen(white,1);
   for(const auto& x:panel::legendBoxes){g.DrawRectangle(&pen,x.x,x.y,x.w,13.f);g.DrawString(x.label,-1,&font,RectF(x.x,x.y,x.w,13),&c,&b);}}
  for(const auto& d:panel::legendDividers)line(g,Color(235,236,238,240),d[0],d[1],d[0],d[2],1.5f);
  text(g,L"11-CATALOG SPECIAL\nFUNCTIONS",{1325,464,150,40},10,white,true,true,face(legendFace));
  text(g,L"GAIN",{1629,470,80,16},11,white,true,true,face(legendFace)); // knob drawn live below
  logo(g,panel::logos::company,panel::logos::companyWidth,panel::logos::companyHeight,{1614,535,318,40},navy);
  for(const auto& b:panel::hostButtons){if(b.command==125)continue;rounded(g,b.rect,Color(255,62,68,78),6);text(g,b.label,{b.rect.x,b.rect.y+17,b.rect.w,b.rect.h-14},15,cream,true,true);}
  text(g,L"FIRMWARE FRONT PANEL  /  0.72",{1570,703,370,28},14,Color(255,145,161,158),true);
 }
 void dynamics(Graphics&g) {
  {const auto& r=panel::hostButtons[0].rect;if(g.IsVisible(rf(r))){rounded(g,r,traceState_==1?Color(255,158,55,58):Color(255,62,68,78),6);text(g,traceState_==1?L"STOP + SAVE TRACE":traceState_==2?L"SAVE TRACE":L"RECORD TRACE",{r.x,r.y+13,r.w,r.h-10},15,cream,true,true);}}
  for(unsigned i=0;i<panel::keys.size();++i){const auto& k=panel::keys[i];auto r=k.rect();if(!g.IsVisible(rf({r.x-8,r.y-45,r.w+16,r.h+55})))continue;const bool pressed=int(i)==held_;fill(g,Color(255,64,68,74),{r.x-4,r.y-4,r.w+8,r.h+8});fill(g,Color(255,20,22,24),{r.x-1,r.y-1,r.w+2,r.h+2});
   Gdiplus::LinearGradientBrush b(rf(r),k.red?Color(255,pressed?151:232,61,47):Color(255,pressed?23:58,pressed?24:58,pressed?25:54),k.red?Color(255,121,32,28):Color(255,13,17,20),Gdiplus::LinearGradientModeVertical);g.FillRectangle(&b,rf(r));line(g,Color(255,120,124,130),r.x+2,r.y+2,r.x+r.w-2,r.y+2,2);line(g,Color(255,4,5,7),r.x+2,r.y+r.h-1,r.x+r.w-2,r.y+r.h-1,2);
   if(int(i)==focus_){Gdiplus::Pen p(cream,1.5f);g.DrawRectangle(&p,r.x-7,r.y-7,r.w+14,r.h+14);}
   if(ScannerControls::ledPin(k.id).latch>=0){const auto v=ScannerControls::ledState(snapshot_.leds,k.id);auto lr=k.led();if(v.known&&v.high){Gdiplus::SolidBrush glow(Color(70,255,60,40));g.FillEllipse(&glow,RectF(lr.x-3,lr.y-3,lr.w+6,lr.h+6));}Gdiplus::SolidBrush lb(!v.known?Color(255,89,81,67):v.high?Color(255,255,52,40):Color(255,96,30,30));g.FillEllipse(&lb,rf(lr));Gdiplus::SolidBrush hi(v.known&&v.high?Color(220,255,189,131):Color(150,153,94,74));g.FillEllipse(&hi,RectF(lr.x+3,lr.y+2.5f,4.5f,3.5f));}
  }
  for(unsigned i=0;i<panel::faders.size();++i){const auto&r=panel::faders[i].track;if(!g.IsVisible(rf(faderBounds(i))))continue;fill(g,Color(255,7,15,23),{r.x+r.w/2-3,r.y,6,r.h});float y=r.y+r.h*(1-float(controls_[i])/5);Gdiplus::LinearGradientBrush b(RectF(r.x-17,y-7,r.w+34,14),Color(255,238,239,241),Color(255,112,116,124),Gdiplus::LinearGradientModeVertical);g.FillRectangle(&b,RectF(r.x-17,y-7,r.w+34,14));line(g,Color(255,250,250,250),r.x-15,y-1,r.x+r.w+15,y-1,1.5f);line(g,Color(255,40,42,48),r.x-15,y+1,r.x+r.w+15,y+1);}
  if(g.IsVisible(rf(panel::volume))) {
   knob(g,panel::volume,gain_);
  }
  if(g.IsVisible(rf(panel::gain)))knob(g,panel::gain,inputGain_);
  auto d=panel::lcd;
  if(g.IsVisible(rf({d.x-8,d.y-8,d.w+16,d.h+16}))) {
   fill(g,Color(255,40,42,44),{d.x-2,d.y-2,d.w+4,d.h+4});{Gdiplus::LinearGradientBrush bz(rf(d),Color(255,178,178,174),Color(255,128,128,124),Gdiplus::LinearGradientModeVertical);g.FillRectangle(&bz,rf(d));}
   const panel::Rect s{d.x+18.3f,d.y+16.5f,177.6f,45.2f};fill(g,Color(255,82,85,87),{s.x-6.7f,s.y-6.7f,s.w+13.4f,s.h+13.4f});fill(g,Color(255,198,202,184),s);
   if(hooks_.glyph&&g.IsVisible(rf(s))){
    std::array<RectF,1280> lit,unlit;unsigned on=0,off=0;
    const float pitchX=1.72f,pitchY=1.88f;
    for(unsigned row=0;row<2;++row)for(unsigned col=0;col<16;++col){
     unsigned char c=col<snapshot_.lcd[row].size()?snapshot_.lcd[row][col]:' ';auto bits=hooks_.glyph(c);
     for(unsigned y=0;y<8;++y)for(unsigned x=0;x<5;++x){
      RectF dot(s.x+5.5f+(col*6+x)*pitchX,s.y+8.2f+row*19.9f+y*pitchY,1.47f,1.62f);
      if(snapshot_.lcdOn&&y<7&&(bits[y]&(16>>x)))lit[on++]=dot;else unlit[off++]=dot;
     }
    }
    // Dot rectangles do not overlap. Submit each color once instead of
    // constructing1280 brushes and issuing1280 GDI+ draw calls per LCD paint.
    Gdiplus::SolidBrush bright(Color(255,72,76,74)),dim(Color(255,188,192,175));
    if(off)g.FillRectangles(&dim,unlit.data(),INT(off));
    if(on)g.FillRectangles(&bright,lit.data(),INT(on));
   }
  }
  if(g.IsVisible(rf({25,625,300,50})))text(g,status_.c_str(),{25,625,300,50},16,cream);
 }
 // GDI+ draws quickly into 32-bit DIB memory but slowly through a device
 // context, so both the static cache and each paint's back buffer are DIBs.
 static HBITMAP dib(HDC dc,int w,int h,void**bits){BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=w;bi.bmiHeader.biHeight=-h;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;return CreateDIBSection(dc,&bi,DIB_RGB_COLORS,bits,nullptr,0);}
 void paint(){WindowsPaintClip clip(hwnd_);PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd_,&ps);RECT client;GetClientRect(hwnd_,&client);int w=std::max(1,int(client.right)),h=std::max(1,int(client.bottom));auto v=viewport();
  if(cache_&&sizing_&&(w!=cacheW_||h!=cacheH_)){
   // Interactive resize: rescale the last crisp image (milliseconds) instead
   // of re-rendering every vector and glyph; WM_EXITSIZEMOVE redraws it once.
   void* bits=nullptr;HBITMAP next=dib(dc,w,h,&bits);HDC src=CreateCompatibleDC(dc),dst=CreateCompatibleDC(dc);auto os=SelectObject(src,cache_);auto od=SelectObject(dst,next);
   SetStretchBltMode(dst,HALFTONE);SetBrushOrgEx(dst,0,0,nullptr);StretchBlt(dst,0,0,w,h,src,0,0,cacheW_,cacheH_,SRCCOPY);SelectObject(src,os);SelectObject(dst,od);DeleteDC(src);DeleteDC(dst);
   DeleteObject(cache_);cache_=next;cacheW_=w;cacheH_=h;stale_=true;}
  if(!cache_||w!=cacheW_||h!=cacheH_){clearCache();void* bits=nullptr;cache_=dib(dc,w,h,&bits);cacheW_=w;cacheH_=h;
   if(cache_){Gdiplus::Bitmap target(w,h,w*4,PixelFormat32bppRGB,static_cast<BYTE*>(bits));Graphics g(&target);g.Clear(Color(255,26,29,30));g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);g.TranslateTransform(v.x,v.y);g.ScaleTransform(v.scale,v.scale);staticPanel(g);}}
  const int left=ps.rcPaint.left,top=ps.rcPaint.top,pw=ps.rcPaint.right-left,ph=ps.rcPaint.bottom-top;
  if(cache_&&pw>0&&ph>0){void* bits=nullptr;HBITMAP back=dib(dc,pw,ph,&bits);HDC cdc=CreateCompatibleDC(dc),bdc=CreateCompatibleDC(dc);auto oc=SelectObject(cdc,cache_);auto ob=SelectObject(bdc,back);
   BitBlt(bdc,0,0,pw,ph,cdc,left,top,SRCCOPY);GdiFlush();
   {Gdiplus::Bitmap target(pw,ph,pw*4,PixelFormat32bppRGB,static_cast<BYTE*>(bits));Graphics g(&target);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);g.TranslateTransform(v.x-float(left),v.y-float(top));g.ScaleTransform(v.scale,v.scale);dynamics(g);}
   BitBlt(dc,left,top,pw,ph,bdc,0,0,SRCCOPY);SelectObject(cdc,oc);SelectObject(bdc,ob);DeleteDC(cdc);DeleteDC(bdc);DeleteObject(back);}
  (void)clip;EndPaint(hwnd_,&ps);
 }
 void release(){heldKey_=0;if(held_>=0){auto i=held_;held_=-1;if(hooks_.contact)hooks_.contact(panel::keys[unsigned(i)].id,false);dirty(panel::keys[unsigned(i)].rect());}if(drag_>=0&&hooks_.save)hooks_.save();drag_=-1;}
 void drag(panel::Point p){if(drag_>=0&&drag_<6){auto i=unsigned(drag_);controls_[i]=panel::faderVolts(i,{p.x,p.y+grabDy_});eii::gui::stats.voltages.fetch_add(1,std::memory_order_relaxed);if(hooks_.voltage)hooks_.voltage(i,controls_[i]);dirty(faderBounds(i));}else if(drag_==6){gain_=std::clamp(dragGain_+(dragY_-p.y)/180.f,0.f,1.f);if(hooks_.volume)hooks_.volume(gain_);dirty(panel::volume);}else if(drag_==7){inputGain_=std::clamp(dragGain_+(dragY_-p.y)/180.f,0.f,1.f);if(hooks_.inputGain)hooks_.inputGain(inputGain_);dirty(panel::gain);}}
 static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){auto*self=reinterpret_cast<VectorPanelWindow*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){self=static_cast<VectorPanelWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->hwnd_=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(!self)return DefWindowProcW(h,m,w,l);return self->message(m,w,l);}
 LRESULT message(UINT m,WPARAM w,LPARAM l){switch(m){
 case WM_ERASEBKGND:return 1;
 case WM_PAINT:{const auto t=eii::gui::nowUs();paint();const auto d=eii::gui::nowUs()-t;eii::gui::stats.paints.fetch_add(1,std::memory_order_relaxed);eii::gui::stats.paintUs.fetch_add(d,std::memory_order_relaxed);eii::gui::storeMax(eii::gui::stats.paintMaxUs,d);return 0;}
 case WM_DPICHANGED:{auto*r=reinterpret_cast<RECT*>(l);SetWindowPos(hwnd_,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);return 0;}
 case WM_SIZE:if(!sizing_)clearCache();InvalidateRect(hwnd_,nullptr,FALSE);return 0;
 case WM_ENTERSIZEMOVE:sizing_=true;return 0;
 case WM_EXITSIZEMOVE:sizing_=false;if(stale_){stale_=false;clearCache();InvalidateRect(hwnd_,nullptr,FALSE);}return 0;
 case WM_GETMINMAXINFO:{auto*p=reinterpret_cast<MINMAXINFO*>(l);p->ptMinTrackSize={1000,430};return 0;}
 case WM_LBUTTONDOWN:{SetFocus(hwnd_);auto p=viewport().design({float(GET_X_LPARAM(l)),float(GET_Y_LPARAM(l))});int key=panel::keyAt(p);if(key>=0){release();if(focus_>=0){auto r=panel::keys[unsigned(focus_)].rect();r.x-=8;r.y-=8;r.w+=16;r.h+=16;dirty(r);}held_=focus_=key;SetCapture(hwnd_);if(hooks_.contact)hooks_.contact(panel::keys[unsigned(key)].id,true);dirty(panel::keys[unsigned(key)].rect());return 0;}int f=panel::faderAt(p);if(f>=0){drag_=f;const auto&tr=panel::faders[unsigned(f)].track;const float ky=tr.y+tr.h*(1-float(controls_[unsigned(f)])/5);grabDy_=f<4&&std::abs(p.y-ky)<=10?ky-p.y:0.f;SetCapture(hwnd_);if(grabDy_==0.f)drag(p);return 0;}if(panel::volume.contains(p)){drag_=6;dragY_=p.y;dragGain_=gain_;SetCapture(hwnd_);return 0;}if(panel::gain.contains(p)){drag_=7;dragY_=p.y;dragGain_=inputGain_;SetCapture(hwnd_);return 0;}for(auto b:panel::hostButtons)if(b.rect.contains(p)&&hooks_.command){hooks_.command(b.command);return 0;}return 0;}
 case WM_MOUSEMOVE:if(drag_>=0){const auto t=eii::gui::nowUs();drag(viewport().design({float(GET_X_LPARAM(l)),float(GET_Y_LPARAM(l))}));const auto d=eii::gui::nowUs()-t;eii::gui::stats.moves.fetch_add(1,std::memory_order_relaxed);eii::gui::stats.moveUs.fetch_add(d,std::memory_order_relaxed);eii::gui::storeMax(eii::gui::stats.moveMaxUs,d);}return 0;
 case WM_LBUTTONUP:release();if(GetCapture()==hwnd_)ReleaseCapture();return 0;
 case WM_CAPTURECHANGED:case WM_KILLFOCUS:case WM_CANCELMODE:release();return 0;
 case WM_MOUSEWHEEL:{POINT pt{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(hwnd_,&pt);auto p=viewport().design({float(pt.x),float(pt.y)});int f=panel::faderAt(p);if(f>=0){controls_[unsigned(f)]=std::clamp(controls_[unsigned(f)]+double(GET_WHEEL_DELTA_WPARAM(w))/WHEEL_DELTA*.05,0.,5.);if(hooks_.voltage)hooks_.voltage(unsigned(f),controls_[unsigned(f)]);dirty(faderBounds(unsigned(f)));if(hooks_.save)hooks_.save();}return 0;}
 case WM_KEYDOWN:{if(l&(LPARAM(1)<<30))return 0;if(w==VK_ESCAPE){release();return 0;}if(w==VK_TAB){if(focus_>=0)dirty(panel::keys[unsigned(focus_)].rect());focus_=(focus_+1)%30;dirty(panel::keys[unsigned(focus_)].rect());return 0;}int id=-1;if(w>=L'0'&&w<=L'9')id=w==L'0'?8:18-int(w-L'0');else if(w>=VK_NUMPAD0&&w<=VK_NUMPAD9)id=w==VK_NUMPAD0?8:18-int(w-VK_NUMPAD0);else if(w==VK_RETURN)id=int(ScannerButton::Enter);if(w==VK_SPACE&&focus_>=0)id=int(panel::keys[unsigned(focus_)].id);for(unsigned i=0;i<panel::keys.size();++i)if(int(panel::keys[i].id)==id){if(focus_>=0)dirty(panel::keys[unsigned(focus_)].rect());release();held_=focus_=int(i);heldKey_=w;if(hooks_.contact)hooks_.contact(panel::keys[i].id,true);dirty(panel::keys[i].rect());break;}return 0;}
 case WM_KEYUP:if(w==heldKey_)release();return 0;
 case WM_CLOSE:release();if(!closing_){closing_=true;if(hooks_.close)hooks_.close();}return 0;
 case WM_NCDESTROY:{auto r=DefWindowProcW(hwnd_,m,w,l);hwnd_=nullptr;return r;}
 }return DefWindowProcW(hwnd_,m,w,l);}
};
}
