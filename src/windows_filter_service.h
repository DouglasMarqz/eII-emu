#pragma once
#ifdef _WIN32
#include "filter_calibration.h"
#include "filter_service_adjustment.h"
#include "chip_audio_worker.h"
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <functional>
#include <fstream>
#include <memory>
#include <filesystem>
#include <cstring>
namespace eii::host {
// Modeless: the firmware panel remains usable while pin/model trims change.
struct FilterServiceMetrics {analog::ChipAudioWorker::Stats dsp;double droppedRate=0,missingRate=0,debtMs=0;std::uint64_t rebases=0;bool nativeAudio=false,reconstruction=false;std::wstring sampleInput;};
class FilterServiceWindow {
public:
 using Calibration=analog::FilterCalibration;
 FilterServiceWindow(Calibration c,std::function<void(const Calibration&)> apply,std::function<bool()> save,
                     std::function<FilterServiceMetrics()> stats,std::function<bool(unsigned)> supported={},bool legacyQ=false,std::wstring modelLabel={},std::function<Calibration(bool)> globalProfiles={},bool cemCustom=false):value_(c),apply_(std::move(apply)),save_(std::move(save)),stats_(std::move(stats)),supported_(std::move(supported)),legacyQ_(legacyQ),modelLabel_(std::move(modelLabel)),globalProfiles_(std::move(globalProfiles)),cemCustom_(cemCustom){}
 // HWND and timer must not outlive their callback target. All ownership
 // operations run on the window's UI thread. Disconnect first: DestroyWindow
 // can synchronously send focus/control notifications back into the parent.
 ~FilterServiceWindow(){
  closing_=true;const auto h=window_;window_=nullptr;
  if(h){KillTimer(h,1);KillTimer(h,2);SetWindowLongPtrW(h,GWLP_USERDATA,0);DestroyWindow(h);}
 }
 FilterServiceWindow(const FilterServiceWindow&)=delete;
 FilterServiceWindow& operator=(const FilterServiceWindow&)=delete;
 bool dispatchActive()const noexcept{return messageDepth_!=0;}
 bool translate(MSG* msg){return window_&&IsWindowVisible(window_)&&IsDialogMessageW(window_,msg);}
 void show(HWND owner,HFONT font) {
  if(!window_){font_=font;WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"EIIFilterService";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=GetSysColorBrush(COLOR_BTNFACE);RegisterClassW(&wc);
   const auto title=modelLabel_.empty()?std::wstring(L"EII - Filter service / calibration"):L"EII - "+modelLabel_+L" - Service";
   window_=CreateWindowW(wc.lpszClassName,title.c_str(),WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,810,780,owner,nullptr,wc.hInstance,this);
  }
  SetTimer(window_,1,250,nullptr);ShowWindow(window_,SW_SHOW);SetForegroundWindow(window_);
 }
private:
 struct Row {HWND label{},slider{},edit{},unit{};int index=-1;bool fineDrag=false;int anchorX=0,travelPixels=1;double anchorValue=0;};
 HWND window_{},groups_{},monitor_{},message_{};HFONT font_{};std::array<Row,16> rows_{};
 Calibration value_;std::function<void(const Calibration&)> apply_;std::function<bool()> save_;std::function<FilterServiceMetrics()> stats_;std::function<bool(unsigned)> supported_;bool legacyQ_=false;std::wstring modelLabel_;
 std::function<Calibration(bool)> globalProfiles_;bool cemCustom_=false;std::array<unsigned,9> groupIds_{};
 unsigned messageDepth_=0;bool closing_=false;
 bool updating_=false,dirty_=false,pendingApply_=false;ULONGLONG changed_=0;std::wstring monitorText_;
 static std::wstring wide(const std::string& s){return std::wstring(s.begin(),s.end());}
 HWND add(const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int width,int height,unsigned id=0){auto h=CreateWindowW(cls,text,WS_CHILD|WS_VISIBLE|style,x,y,width,height,window_,(HMENU)UINT_PTR(id),GetModuleHandleW(nullptr),nullptr);SendMessageW(h,WM_SETFONT,(WPARAM)font_,TRUE);return h;}
 static double position(unsigned k,int pos){const auto& d=analog::filterParameters[k];if(std::string(d.unit)=="0/1")return pos?1:0;const double f=double(pos)/10000;const double v=d.logarithmic?d.lo*std::pow(d.hi/d.lo,f):d.lo+f*(d.hi-d.lo);return std::string(d.unit)=="code"?std::round(v):v;}
 void updateRow(Row& r){if(r.index<0)return;const auto k=unsigned(r.index);const auto& d=analog::filterParameters[k];const double v=value_.values[k];const double f=d.logarithmic?std::log(v/d.lo)/std::log(d.hi/d.lo):(v-d.lo)/(d.hi-d.lo);
  SendMessageW(r.slider,TBM_SETPOS,TRUE,std::strcmp(d.unit,"0/1")==0?int(v):int(std::lround(f*10000)));
  SetWindowTextW(r.edit,wide(serviceValueText(v)).c_str());
 }
 void updateRows(){updating_=true;for(auto& r:rows_)updateRow(r);updating_=false;}
 void flushApply(){if(pendingApply_){pendingApply_=false;KillTimer(window_,2);apply_(value_);}}
 void endFine(){for(auto& r:rows_)r.fineDrag=false;flushApply();}
 static LRESULT CALLBACK sliderProcedure(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data){
  if(m==WM_NCDESTROY){RemoveWindowSubclass(h,sliderProcedure,id);return DefSubclassProc(h,m,w,l);}
  auto* self=reinterpret_cast<FilterServiceWindow*>(data);
  if(!self||self->closing_)return DefSubclassProc(h,m,w,l);
  struct DispatchScope {FilterServiceWindow* owner;DispatchScope(FilterServiceWindow* p):owner(p){++owner->messageDepth_;}~DispatchScope(){--owner->messageDepth_;}} dispatch(self);
  Row* row=nullptr;for(auto& r:self->rows_)if(r.slider==h){row=&r;break;}
  if(!row||row->index<0)return DefSubclassProc(h,m,w,l);
  const auto k=unsigned(row->index);const auto& d=analog::filterParameters[k];
  try{
   if(m==WM_LBUTTONDOWN&&(GetKeyState(VK_SHIFT)&0x8000)&&serviceContinuous(d)){
    SetFocus(h);RECT channel{};SendMessageW(h,TBM_GETCHANNELRECT,0,reinterpret_cast<LPARAM>(&channel));
    row->anchorValue=self->value_.values[k];row->anchorX=int(short(LOWORD(l)));row->travelPixels=std::max(1,int(channel.right-channel.left));row->fineDrag=true;SetCapture(h);return 0;
   }
   if(m==WM_MOUSEMOVE&&row->fineDrag){
    self->set(k,serviceFineOffset(d,row->anchorValue,double(int(short(LOWORD(l)))-row->anchorX)/row->travelPixels),true);return 0;
   }
   if(m==WM_LBUTTONUP&&row->fineDrag){
    self->set(k,serviceFineOffset(d,row->anchorValue,double(int(short(LOWORD(l)))-row->anchorX)/row->travelPixels),true);
    row->fineDrag=false;if(GetCapture()==h)ReleaseCapture();self->flushApply();return 0;
   }
   if(m==WM_KEYUP&&k==unsigned(analog::FilterParam::VcaOffset))self->flushApply();
   if(m==WM_LBUTTONUP&&k==unsigned(analog::FilterParam::VcaOffset)){const auto result=DefSubclassProc(h,m,w,l);self->flushApply();return result;}
   if(m==WM_CAPTURECHANGED||m==WM_CANCELMODE||m==WM_KILLFOCUS){row->fineDrag=false;self->flushApply();}
   if((GetKeyState(VK_SHIFT)&0x8000)&&serviceContinuous(d)){
    double travel=0;
    if(m==WM_KEYDOWN){switch(w){case VK_LEFT:case VK_DOWN:travel=-1.0/10000;break;case VK_RIGHT:case VK_UP:travel=1.0/10000;break;case VK_PRIOR:travel=10.0/10000;break;case VK_NEXT:travel=-10.0/10000;break;}}
    if(m==WM_MOUSEWHEEL)travel=double(short(HIWORD(w)))/WHEEL_DELTA/10000;
    if(travel!=0){self->set(k,serviceFineOffset(d,self->value_.values[k],travel),true);return 0;}
   }
  }catch(const std::exception& e){row->fineDrag=false;if(GetCapture()==h)ReleaseCapture();SetWindowTextW(self->message_,wide(e.what()).c_str());self->updateRows();return 0;}
  return DefSubclassProc(h,m,w,l);
 }
 unsigned selectedGroup()const{const auto i=SendMessageW(groups_,CB_GETCURSEL,0,0);return i<0?0:groupIds_[unsigned(i)];}
 void group(){endFine();updating_=true;unsigned selected=selectedGroup(),n=0;
  SetWindowTextW(GetDlgItem(window_,16),L"Reset group");
  for(unsigned k=0;k<analog::filterParameters.size();++k){auto& d=analog::filterParameters[k];if(d.group!=selected||(supported_&&!supported_(k)))continue;auto& r=rows_[n++];r.index=int(k);SetWindowTextW(r.label,wide(d.label).c_str());SetWindowTextW(r.unit,wide(d.unit).c_str());SendMessageW(r.slider,TBM_SETRANGEMAX,FALSE,std::string(d.unit)=="0/1"?1:10000);
   for(auto h:{r.label,r.slider,r.edit,r.unit})ShowWindow(h,SW_SHOW);
  }
  for(;n<rows_.size();++n){rows_[n].index=-1;for(auto h:{rows_[n].label,rows_[n].slider,rows_[n].edit,rows_[n].unit})ShowWindow(h,SW_HIDE);}updating_=false;updateRows();
 }
 void changed(bool defer=false,bool dcOffset=false){value_.validate();if(defer){if(dcOffset){pendingApply_=true;KillTimer(window_,2);SetTimer(window_,2,150,nullptr);}else if(!pendingApply_){pendingApply_=true;SetTimer(window_,2,16,nullptr);}}else{pendingApply_=false;KillTimer(window_,2);apply_(value_);}dirty_=true;changed_=GetTickCount64();SetWindowTextW(message_,dcOffset?L"DC offset preview: release the slider to apply. This is DC calibration, not audio gain.":L"Applied live. Auto-save pending. Native post-chain trim changes level without driving the VCA.");}
 void set(unsigned k,double v,bool fine=false){if(value_.values[k]==v)return;auto next=value_;next.values[k]=v;using P=analog::FilterParam;
  if(k==unsigned(P::I16))next[P::I15]=std::min(next[P::I15],500-v);
  if(k==unsigned(P::I15))next[P::I16]=std::min(next[P::I16],500-v);
  next.validate();value_=next;const bool dcOffset=k==unsigned(P::VcaOffset);changed(fine||dcOffset,dcOffset);if(fine){updating_=true;for(auto& r:rows_)if(r.index==int(k)||(k==unsigned(P::I16)&&r.index==int(P::I15))||(k==unsigned(P::I15)&&r.index==int(P::I16)))updateRow(r);updating_=false;}else updateRows();
 }
 void persist(){flushApply();if(dirty_){if(!save_()){changed_=GetTickCount64();SetWindowTextW(message_,L"Could not save calibration. Export a profile to preserve it.");return;}dirty_=false;SetWindowTextW(message_,L"Calibration saved. Native trim is post-chain; Reset defaults restores only the native section.");}}
 void transfer(bool import){wchar_t file[1024]={};lstrcpynW(file,legacyQ_?L"EII-Legacy-Q-VCF-VCA-profile.txt":L"EII-SSM2045-VCF-VCA-profile.txt",1024);OPENFILENAMEW d{};d.lStructSize=sizeof(d);d.hwndOwner=window_;d.lpstrFilter=L"EII filter calibration (*.txt)\0*.txt\0All files\0*.*\0";d.lpstrFile=file;d.nMaxFile=1024;d.lpstrDefExt=L"txt";d.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|(import?OFN_FILEMUSTEXIST:OFN_OVERWRITEPROMPT);
  if(!(import?GetOpenFileNameW(&d):GetSaveFileNameW(&d)))return;
  if(import){std::ifstream f(std::filesystem::path(file),std::ios::binary);std::string text(32769,'\0');f.read(text.data(),text.size());text.resize(std::size_t(f.gcount()));auto next=value_.importProfile(text);
   // Files exported before the common level model keep a deliberate native trim's loudness (level_model.h).
   if(analog::FilterCalibration::preLevelModel(text)&&text.find("\nNativeOutDb=")!=std::string::npos)analog::migrateLevelModel(next);
   value_=next;changed();updateRows();persist();}
  else{std::ofstream f(std::filesystem::path(file),std::ios::binary|std::ios::trunc);auto text=value_.serialize();f.write(text.data(),std::streamsize(text.size()));f.close();if(!f)throw std::runtime_error("Could not export calibration");SetWindowTextW(message_,L"Exported complete calibration profile.");}
 }
 void copy(){auto text=wide(value_.serialize());std::wstring crlf;for(auto c:text){if(c=='\n')crlf+='\r';crlf+=c;}const auto bytes=(crlf.size()+1)*sizeof(wchar_t);auto mem=GlobalAlloc(GMEM_MOVEABLE,bytes);if(!mem)throw std::runtime_error("Clipboard allocation failed");auto p=GlobalLock(mem);if(!p){GlobalFree(mem);throw std::runtime_error("Clipboard lock failed");}memcpy(p,crlf.c_str(),bytes);GlobalUnlock(mem);
  if(!OpenClipboard(window_)){GlobalFree(mem);throw std::runtime_error("Clipboard unavailable");}EmptyClipboard();if(!SetClipboardData(CF_UNICODETEXT,mem)){CloseClipboard();GlobalFree(mem);throw std::runtime_error("Clipboard copy failed");}CloseClipboard();SetWindowTextW(message_,L"Complete profile copied. Paste it into the conversation.");}
 void timer(){if(dirty_&&!pendingApply_&&GetTickCount64()-changed_>=500)persist();auto m=stats_();auto s=m.dsp;std::wostringstream o;if(selectedGroup()==6){const unsigned code=unsigned(value_[analog::FilterParam::S3528Code]);o<<L"S3528 test: code "<<code<<L" = "<<5000000./(40*analog::S3528Control::divisors[code])<<L" Hz; override "<<(value_[analog::FilterParam::S3528Override]?L"ON":L"OFF (firmware controls cutoff)")<<L"\r\n";}o<<std::fixed<<std::setprecision(1)<<L"DSP load "<<s.load*100<<L"% | native DAC "<<(m.nativeAudio?L"ON":L"OFF")<<L" | S3528 "<<(m.reconstruction?L"ON":L"OFF")<<L" | queue "<<s.queuedFrames/48.0<<L" ms | block "<<s.blockMs<<L" ms | DC "<<s.outputDc<<L"\r\n"<<L"DSP drop "<<s.dropped<<L" | recovery/held "<<s.recoveries<<L"/"<<s.held<<L" | fallback "<<s.fallbacks<<L" | wait "<<s.waitMs<<L" ms\r\n"<<L"Host drop/missing per sec "<<m.droppedRate<<L"/"<<m.missingRate<<L" | pacing rebases "<<m.rebases<<L" ("<<m.debtMs<<L" ms) | cal rev "<<s.calibrationRevision;
  if(!m.sampleInput.empty())o<<L"\r\n"<<m.sampleInput;
  auto text=o.str();if(text!=monitorText_){monitorText_=text;SetWindowTextW(monitor_,text.c_str());}}
 LRESULT message(UINT m,WPARAM w,LPARAM l){try{switch(m){case WM_CREATE:
  add(L"STATIC",modelLabel_.empty()?L"SSM tuning: nonlinear modes. Native controls: native route.":modelLabel_.c_str(),0,12,8,775,22);
  add(L"STATIC",cemCustom_?L"CEM drive is pre-VCF. Q makeup is post-VCF; zero Q gets no Q makeup.":L"Board FC/Q = external circuit. Chip FC/Q = internal model. VCA/output is separate.",0,12,31,775,22);
  groups_=add(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,12,57,180,240,10);
  {unsigned groupIndex=0,count=0;int initial=0;for(auto title:{L"Board FC/Q (external)",L"SSM2045 FC/Q (chip)",L"Chip offsets",L"VCA / output",L"Pin overrides (lab)",L"Native DAC / deglitch",L"S3528 test (lab)",L"CEM3387 custom",L"SSM2045 model 2 (chip)"}){
   // A group is listed only if the active model supports at least one of its rows.
   bool any=!supported_;for(unsigned k=0;supported_&&k<analog::filterParameters.size();++k)any|=analog::filterParameters[k].group==groupIndex&&supported_(k);
   const bool visible=any&&(supported_||groupIndex!=8)&&(cemCustom_?(groupIndex!=1&&groupIndex!=2):(groupIndex!=7&&!(legacyQ_&&groupIndex==2)));
   if(visible){groupIds_[count]=groupIndex;const auto text=cemCustom_&&groupIndex==3?L"EII VCA / output":legacyQ_&&groupIndex==1?L"Legacy VCF/Q core":title;
    SendMessageW(groups_,CB_ADDSTRING,0,(LPARAM)text);if(cemCustom_&&groupIndex==7)initial=int(count);++count;}++groupIndex;
  }SendMessageW(groups_,CB_SETCURSEL,initial,0);}

  add(L"BUTTON",L"Copy profile",BS_PUSHBUTTON,200,57,105,28,11);add(L"BUTTON",L"Export...",BS_PUSHBUTTON,313,57,80,28,12);add(L"BUTTON",L"Import...",BS_PUSHBUTTON,401,57,80,28,13);add(L"BUTTON",L"Reset all",BS_PUSHBUTTON,489,57,80,28,14);add(L"BUTTON",L"Save",BS_PUSHBUTTON,700,57,77,28,15);
  add(L"BUTTON",L"Reset group",BS_PUSHBUTTON,577,57,115,28,16);
  monitor_=add(L"STATIC",L"DSP starts when a nonlinear model is active.",0,12,91,775,62);
  for(unsigned n=0;n<rows_.size();++n){auto& r=rows_[n];const int y=153+int(n)*33;r.label=add(L"STATIC",L"",0,12,y+4,225,24);r.slider=add(TRACKBAR_CLASSW,L"",TBS_HORZ|TBS_NOTICKS|WS_TABSTOP,242,y,315,28,100+n);SetWindowSubclass(r.slider,sliderProcedure,1,reinterpret_cast<DWORD_PTR>(this));r.edit=add(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,565,y+1,135,26,200+n);r.unit=add(L"STATIC",L"",0,710,y+4,75,24);}
  message_=add(L"STATIC",L"Shift + drag / arrows / wheel: 100x finer. Numeric value + Tab applies full precision.",0,12,692,775,22);
  if(globalProfiles_){add(L"BUTTON",L"Save global",BS_PUSHBUTTON,12,716,140,24,17);add(L"BUTTON",L"Load global",BS_PUSHBUTTON,160,716,140,24,18);add(L"STATIC",L"All profiles: SSM, Legacy, CEM. New-instance defaults.",0,312,719,465,20);}
  group();SetTimer(window_,1,250,nullptr);return 0;
 case WM_COMMAND:
  if(LOWORD(w)==1){SetFocus(groups_);return 0;}
  if(LOWORD(w)==2){persist();KillTimer(window_,1);ShowWindow(window_,SW_HIDE);return 0;}
  if(LOWORD(w)==10&&HIWORD(w)==CBN_SELCHANGE){group();return 0;}
  if(LOWORD(w)>=200&&LOWORD(w)<216&&HIWORD(w)==EN_KILLFOCUS&&!updating_){auto& r=rows_[LOWORD(w)-200];if(r.index>=0){wchar_t raw[128];GetWindowTextW(r.edit,raw,128);std::wstring ws(raw);std::string text(ws.begin(),ws.end());std::istringstream in(text);in.imbue(std::locale::classic());double v;if(!(in>>v))throw std::invalid_argument("Invalid decimal number");in>>std::ws;if(!in.eof())throw std::invalid_argument("Invalid decimal number");set(unsigned(r.index),v);}return 0;}
  if(HIWORD(w)==BN_CLICKED){switch(LOWORD(w)){case 11:copy();break;case 12:transfer(false);break;case 13:transfer(true);break;case 14:value_=Calibration{};changed();updateRows();persist();break;case 15:persist();break;case 17:case 18:if(globalProfiles_){endFine();value_=globalProfiles_(LOWORD(w)==17);updateRows();SetWindowTextW(message_,LOWORD(w)==17?L"All complete model profiles saved globally. Existing projects keep their own settings.":L"All global model profiles loaded into this instance.");}break;case 16:value_.resetGroupDefaults(selectedGroup());changed();updateRows();persist();SetWindowTextW(message_,L"Selected group defaults restored. Other board/chip/VCA groups are preserved.");break;}return 0;}break;
 case WM_HSCROLL:if(LOWORD(w)==TB_ENDTRACK){flushApply();return 0;}if(!updating_)for(auto& r:rows_)if(r.index>=0&&r.slider==(HWND)l){set(unsigned(r.index),position(unsigned(r.index),int(SendMessageW(r.slider,TBM_GETPOS,0,0))));break;}return 0;
 case WM_TIMER:if(w==2){bool held=false;for(const auto& r:rows_)if(r.index==int(analog::FilterParam::VcaOffset)){
   held=r.fineDrag||GetCapture()==r.slider;
   if(GetFocus()==r.slider)for(int key:{VK_LEFT,VK_RIGHT,VK_UP,VK_DOWN,VK_PRIOR,VK_NEXT})held=held||(GetKeyState(key)&0x8000);
  }if(!held)flushApply();}else if(w==1)timer();return 0;
 case WM_CLOSE:endFine();persist();KillTimer(window_,1);ShowWindow(window_,SW_HIDE);return 0;
 case WM_DESTROY:KillTimer(window_,1);KillTimer(window_,2);return 0;
 }}catch(const std::exception& e){SetWindowTextW(message_,wide(e.what()).c_str());updateRows();}return DefWindowProcW(window_,m,w,l);}
 static LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l){
  auto* self=reinterpret_cast<FilterServiceWindow*>(GetWindowLongPtrW(h,GWLP_USERDATA));
  if(m==WM_NCCREATE){self=static_cast<FilterServiceWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window_=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
  if(m==WM_NCDESTROY){
   KillTimer(h,1);KillTimer(h,2);SetWindowLongPtrW(h,GWLP_USERDATA,0);
   if(self&&self->window_==h)self->window_=nullptr;
   return DefWindowProcW(h,m,w,l);
  }
  if(!self||self->closing_)return DefWindowProcW(h,m,w,l);
  ++self->messageDepth_;
  const auto result=self->message(m,w,l);
  --self->messageDepth_;
  return result;
 }
};
}
#endif
