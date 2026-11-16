// Headless VST3 host test for EmulatorII.vst3 (Windows). Loads the bundle with
// the SDK hosting module API, boots the real firmware from caller-supplied ROMs
// and floppy (never stored in Git), renders offline, plays MIDI notes, measures
// pitch, round-trips state, exercises realtime processing and runs two
// instances concurrently. Exit code 0 = all checks passed.
//   vst3_host_test <EmulatorII.vst3> <rom-folder> <floppy> [wav-output-folder]
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <mmsystem.h>
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/gui/iplugview.h"
#include "vst3/eii_vst3_ids.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>
#include <random>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {
int failures=0;std::string placementSetting="plugin-default-off";
void check(bool ok,const std::string& what){std::cout<<(ok?"PASS ":"FAIL ")<<what<<"\n";if(!ok)++failures;}
std::string utf8(const std::wstring& w){if(w.empty())return {};const int n=WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),nullptr,0,nullptr,nullptr);std::string s(std::size_t(n),' ');WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),s.data(),n,nullptr,nullptr);return s;}
template<class T> IPtr<T> qi(FUnknown* u){T* p=nullptr;if(u&&u->queryInterface(T::iid,reinterpret_cast<void**>(&p))==kResultOk&&p)return owned(p);return nullptr;}
// Threads this plug-in starts are named "EII ..." (machine owner, SSM2045 DSP).
unsigned eiiThreads(){
    using GetDescription=HRESULT(WINAPI*)(HANDLE,PWSTR*);
    static const auto get=reinterpret_cast<GetDescription>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetThreadDescription")));
    unsigned n=0;const HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);THREADENTRY32 t{};t.dwSize=sizeof(t);
    if(get&&Thread32First(snap,&t))do{
        if(t.th32OwnerProcessID!=GetCurrentProcessId())continue;
        const HANDLE h=OpenThread(THREAD_QUERY_LIMITED_INFORMATION,FALSE,t.th32ThreadID);if(!h)continue;
        PWSTR name=nullptr;if(SUCCEEDED(get(h,&name))&&name){if(std::wstring(name).rfind(L"EII ",0)==0)++n;LocalFree(name);}CloseHandle(h);
    }while(Thread32Next(snap,&t));
    CloseHandle(snap);return n;
}
// YIN fundamental estimate (de Cheveigne & Kawahara), 50..2000 Hz.
double pitch(const std::vector<float>& x,std::size_t begin,std::size_t length,double rate){
    const std::size_t maxTau=std::size_t(rate/50),minTau=std::size_t(rate/2000);
    if(begin+length+maxTau>x.size())return 0;
    std::vector<double> d(maxTau+1,0),cmnd(maxTau+1,1);
    for(std::size_t tau=1;tau<=maxTau;++tau){double s=0;for(std::size_t i=0;i<length;++i){const double v=double(x[begin+i])-x[begin+i+tau];s+=v*v;}d[tau]=s;}
    double running=0;for(std::size_t tau=1;tau<=maxTau;++tau){running+=d[tau];cmnd[tau]=running>0?d[tau]*double(tau)/running:1;}
    std::size_t best=0;for(std::size_t tau=minTau;tau<maxTau;++tau)if(cmnd[tau]<0.15){while(tau+1<maxTau&&cmnd[tau+1]<cmnd[tau])++tau;best=tau;break;}
    if(!best){double m=1e9;for(std::size_t tau=minTau;tau<maxTau;++tau)if(cmnd[tau]<m){m=cmnd[tau];best=tau;}}
    const double a=cmnd[best-1],b=cmnd[best],c=cmnd[best+1],den=a-2*b+c;const double shift=den!=0?0.5*(a-c)/den:0;
    return rate/(double(best)+shift);
}
double rms(const std::vector<float>& x,std::size_t begin,std::size_t length){double s=0;for(std::size_t i=begin;i<begin+length&&i<x.size();++i)s+=double(x[i])*x[i];return std::sqrt(s/double(length));}
void writeWav(const std::string& path,const std::vector<float>& l,const std::vector<float>& r,unsigned rate){
    std::ofstream f(path,std::ios::binary);if(!f)return;const std::uint32_t frames=std::uint32_t(l.size()),bytes=frames*4;
    auto u32=[&](std::uint32_t v){f.write(reinterpret_cast<const char*>(&v),4);};auto u16=[&](std::uint16_t v){f.write(reinterpret_cast<const char*>(&v),2);};
    f.write("RIFF",4);u32(36+bytes);f.write("WAVEfmt ",8);u32(16);u16(1);u16(2);u32(rate);u32(rate*4);u16(4);u16(16);f.write("data",4);u32(bytes);
    for(std::uint32_t i=0;i<frames;++i){for(float v:{l[i],r[i]}){const auto s=std::int16_t(std::lround(std::clamp(v,-1.f,1.f)*32767));f.write(reinterpret_cast<const char*>(&s),2);}}
}

struct Instance {
    IPtr<IComponent> component;IPtr<IAudioProcessor> processor;IPtr<IEditController> controller;IPtr<eii::vst::IEiiDiagnostics> diag;
    HostProcessData data;std::vector<float> left,right,inputLeft,inputRight;int block=512;double rate=48000;
    ~Instance(){shutdown();}
    bool create(const VST3::Hosting::PluginFactory& factory,const VST3::UID& cid,FUnknown* host){
        component=factory.createInstance<IComponent>(cid);if(!component)return false;
        if(component->initialize(host)!=kResultOk)return false;
        processor=qi<IAudioProcessor>(component);controller=qi<IEditController>(component);diag=qi<eii::vst::IEiiDiagnostics>(component);
        return processor&&controller&&diag;
    }
    bool setState(const std::string& text){MemoryStream s;int32 w=0;s.write(const_cast<char*>(text.data()),int32(text.size()),&w);s.seek(0,IBStream::kIBSeekSet,nullptr);return component->setState(&s)==kResultOk;}
    std::string getState(){MemoryStream s;if(component->getState(&s)!=kResultOk)return {};return std::string(s.getData(),std::size_t(s.getSize()));}
    bool start(double sampleRate,int32 mode,int blockSize){
        rate=sampleRate;block=blockSize;
        ProcessSetup setup{mode,kSample32,blockSize,sampleRate};
        if(processor->setupProcessing(setup)!=kResultOk)return false;
        component->activateBus(kAudio,kOutput,0,true);component->activateBus(kEvent,kInput,0,true);
        if(component->setActive(true)!=kResultOk)return false;
        processor->setProcessing(true);
        data.prepare(*component,0,kSample32);left.resize(std::size_t(blockSize));right.resize(std::size_t(blockSize)); // host-owned buffers
        float* buffers[2]={left.data(),right.data()};if(!data.setChannelBuffers(kOutput,0,buffers,2))return false;
        inputLeft.assign(std::size_t(blockSize),0);inputRight.assign(std::size_t(blockSize),0);
        float* inputBuffers[2]={inputLeft.data(),inputRight.data()};
        if(!data.setChannelBuffers(kInput,0,inputBuffers,2))return false;
        data.numSamples=blockSize;data.processMode=mode;return true;
    }
    void shutdown(){if(component){processor->setProcessing(false);component->setActive(false);component->terminate();diag=nullptr;controller=nullptr;processor=nullptr;component=nullptr;}}
    bool process(EventList* events=nullptr,ParameterChanges* params=nullptr){
        data.inputEvents=events;data.inputParameterChanges=params;const auto r=processor->process(data);data.inputEvents=nullptr;data.inputParameterChanges=nullptr;return r==kResultOk;
    }
    std::string status(const std::string& key){
        char buffer[2048];diag->statusText(buffer,sizeof buffer);std::istringstream in(buffer);std::string line;
        while(std::getline(in,line))if(line.rfind(key+"=",0)==0)return line.substr(key.size()+1);return {};
    }
    bool ready(){const auto l=status("lcd0");return l.size()>=3&&l[0]=='P'&&std::isdigit(static_cast<unsigned char>(l[1]))&&std::isdigit(static_cast<unsigned char>(l[2]));}
};

// Deliberately emulate an old host: resize the parent but do not call onSize.
// The editor must handle accepted resize requests and restore its saved size.
class LegacyEditorFrame final : public IPlugFrame {
public:
    HWND window=nullptr;unsigned resizeCalls=0;
    tresult PLUGIN_API queryInterface(const TUID iid,void** obj)override{
        if(FUnknownPrivate::iidEqual(iid,IPlugFrame::iid)||FUnknownPrivate::iidEqual(iid,FUnknown::iid)){*obj=this;addRef();return kResultOk;}
        *obj=nullptr;return kNoInterface;
    }
    uint32 PLUGIN_API addRef()override{return ++refs;}
    uint32 PLUGIN_API release()override{return --refs;} // test scope owns the frame
    tresult PLUGIN_API resizeView(IPlugView*,ViewRect* r)override{
        if(!window||!r)return kResultFalse;++resizeCalls;
        SetWindowPos(window,nullptr,0,0,r->getWidth(),r->getHeight(),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);return kResultTrue;
    }
private:uint32 refs=1;
};

// Opens the editor twice in a frame window, pumps messages while processing,
// captures the first one (PrintWindow) and checks resizing and teardown.
bool editorTest(Instance& in,const std::string& bmpPath,double& litFraction,int& defaultWidth){
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"EiiVst3HostTestFrame";RegisterClassW(&wc);
    for(int round=0;round<2;++round){
        IPlugView* raw=in.controller->createView(ViewType::kEditor);if(!raw)return false;IPtr<IPlugView> view=owned(raw);
        if(view->isPlatformTypeSupported(kPlatformTypeHWND)!=kResultTrue)return false;
        ViewRect r;view->getSize(&r);
        // Reopening legacy hosts may send their stale default onSize before attached.
        if(round==1){ViewRect stale{0,0,1500,582};view->onSize(&stale);}
        HWND frame=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"EII VST3 editor test",WS_POPUP|WS_VISIBLE,40,40,r.getWidth(),r.getHeight(),nullptr,nullptr,wc.hInstance,nullptr);
        LegacyEditorFrame legacy;legacy.window=frame;view->setFrame(&legacy);
        if(!frame||view->attached(frame,kPlatformTypeHWND)!=kResultOk){view->setFrame(nullptr);if(frame)DestroyWindow(frame);return false;}
        if(!legacy.resizeCalls){view->removed();view->setFrame(nullptr);DestroyWindow(frame);return false;}
        auto pump=[&](double seconds){const auto until=std::chrono::steady_clock::now()+std::chrono::duration<double>(seconds);
            while(std::chrono::steady_clock::now()<until){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}in.process();Sleep(5);}};
        view->getSize(&r); // attachment must restore the saved instance size
        SetWindowPos(frame,nullptr,0,0,r.getWidth(),r.getHeight(),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        pump(1.5);
        if(round==0){
            HDC screen=GetDC(frame);HDC mem=CreateCompatibleDC(screen);HBITMAP bmp=CreateCompatibleBitmap(screen,r.getWidth(),r.getHeight());auto old=SelectObject(mem,bmp);
            PrintWindow(frame,mem,2/*PW_RENDERFULLCONTENT*/);SelectObject(mem,old);
            BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=r.getWidth();bi.bmiHeader.biHeight=r.getHeight();bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
            std::vector<std::uint32_t> px(std::size_t(r.getWidth())*std::size_t(r.getHeight()));GetDIBits(mem,bmp,0,UINT(r.getHeight()),px.data(),&bi,DIB_RGB_COLORS);
            std::size_t lit=0;for(auto v:px)if((v&0xffffff)>0x101010)++lit;litFraction=double(lit)/double(px.size());
            if(!bmpPath.empty()){std::ofstream f(bmpPath,std::ios::binary);BITMAPFILEHEADER fh{};fh.bfType=0x4d42;fh.bfOffBits=sizeof(fh)+sizeof(BITMAPINFOHEADER);fh.bfSize=DWORD(fh.bfOffBits+px.size()*4);
                f.write(reinterpret_cast<const char*>(&fh),sizeof fh);f.write(reinterpret_cast<const char*>(&bi.bmiHeader),sizeof(BITMAPINFOHEADER));f.write(reinterpret_cast<const char*>(px.data()),std::streamsize(px.size()*4));}
            DeleteObject(bmp);DeleteDC(mem);ReleaseDC(frame,screen);
        }
        if(round==0)defaultWidth=r.getWidth();else if(r.getWidth()!=1100)return false; // size remembered across views
        ViewRect nr{0,0,1100,427};view->checkSizeConstraint(&nr);
        const bool aspect=std::abs(double(nr.getHeight())/nr.getWidth()-760.0/1960)<0.01;
        SetWindowPos(frame,nullptr,0,0,nr.getWidth(),nr.getHeight(),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);view->onSize(&nr);pump(0.3);
        view->removed();view->setFrame(nullptr);view=nullptr;DestroyWindow(frame);
        if(!aspect)return false;
    }
    return true;
}
std::string configText(const std::string& rom,const std::string& floppy){
    std::string extra;if(const char* c=std::getenv("EII_TEST_CONTROLS"))extra=std::string("controls=")+c+"\n";
    return "EIIVST3 1\nrom="+rom+"\nfloppy="+floppy+"\nautoboot=1\nmodel=0\n"+extra;
}
Event note(bool on,int key,int offset){Event e{};e.busIndex=0;e.sampleOffset=offset;
    if(on){e.type=Event::kNoteOnEvent;e.noteOn.channel=0;e.noteOn.pitch=int16(key);e.noteOn.velocity=100.f/127;e.noteOn.noteId=-1;}
    else{e.type=Event::kNoteOffEvent;e.noteOff.channel=0;e.noteOff.pitch=int16(key);e.noteOff.velocity=0;e.noteOff.noteId=-1;}return e;}

// ---------------- realtime soak ----------------
// DAW-like audio thread (MMCSS "Pro Audio", 1 ms timer, absolute deadlines,
// spin for the last 0.5 ms), random notes, synthetic background CPU load and
// random 5-20 ms suspensions of the plug-in's machine thread.
DWORD findThread(const wchar_t* prefix){
    using GetDescription=HRESULT(WINAPI*)(HANDLE,PWSTR*);
    static const auto get=reinterpret_cast<GetDescription>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetThreadDescription")));
    DWORD id=0;const HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);THREADENTRY32 t{};t.dwSize=sizeof(t);
    if(get&&Thread32First(snap,&t))do{
        if(t.th32OwnerProcessID!=GetCurrentProcessId())continue;
        const HANDLE h=OpenThread(THREAD_QUERY_LIMITED_INFORMATION,FALSE,t.th32ThreadID);if(!h)continue;
        PWSTR name=nullptr;if(SUCCEEDED(get(h,&name))&&name){if(std::wstring(name).rfind(prefix,0)==0)id=t.th32ThreadID;LocalFree(name);}CloseHandle(h);
    }while(!id&&Thread32Next(snap,&t));
    CloseHandle(snap);return id;
}
std::string modeConfig(const std::string& mode,const std::string& rom,const std::string& floppy){
    std::string c="EIIVST3 1\nrom="+rom+"\nfloppy="+floppy+"\nautoboot=1\n";
    if(mode=="legacyq")c+="model=3\nnative=1\ns3528=1\n";else if(mode=="legacy")c+="model=0\n";else if(mode=="ssm96")c+="model=1\n";else if(mode=="ssm192")c+="model=2\n";
    else if(mode=="native")c+="model=1\nnative=1\ns3528=1\n";else if(mode=="native192")c+="model=2\nnative=1\ns3528=1\n";
    else throw std::invalid_argument("mode: legacy|legacyq|ssm96|ssm192|native|native192");
    if(const char* m=std::getenv("EII_TEST_MARGIN"))c+=std::string("margin=")+m+"\n";
    return c;
}
// ---------------- fresh-instance level census ----------------
// Each instance starts exactly as a new plug-in instance: initialize() loads the
// per-user standalone preferences and the global Filter Service profile. Only the
// model / DAC route / floppy the user would pick in the menu are then changed in
// that instance's own state text (getState -> edit -> setState = project load).
std::string replaceKey(std::string text,const std::string& key,const std::string& value){
    const auto at=text.find("\n"+key+"=");
    if(at==std::string::npos){const auto cal=text.find("\ncalibration:");return text.insert(cal==std::string::npos?text.size():cal+1,key+"="+value+"\n");}
    const auto end=text.find('\n',at+1);return text.replace(at+1,end-at-1,key+"="+value);
}
struct LevelResult{bool booted=false;double rmsDb=-300,peakDb=-300;std::string lcd,applied;};
LevelResult levelRun(const VST3::Hosting::PluginFactory& factory,const VST3::UID& cid,FUnknown* host,const std::string& rom,const std::string& floppy,
                     unsigned model,int native,int s3528,std::string* freshState=nullptr){
    LevelResult r;Instance in;if(!in.create(factory,cid,host))return r;
    std::string st=in.getState();if(freshState)*freshState=st;
    st=replaceKey(st,"rom",rom);st=replaceKey(st,"floppy",floppy);st=replaceKey(st,"model",std::to_string(model));st=replaceKey(st,"autoboot","1");
    if(native>=0)st=replaceKey(st,"native",std::to_string(native));if(s3528>=0)st=replaceKey(st,"s3528",std::to_string(s3528));
    if(!in.setState(st)||!in.start(48000,kOffline,512))return r;
    const int bps=int(in.rate)/in.block;
    for(int b=0;b<bps*150&&!r.booted;++b){in.process();if(b%bps==0)r.booted=in.ready();}
    r.lcd=in.status("lcd0");r.applied="model="+in.status("appliedmodel")+" native="+in.status("appliednative")+" s3528="+in.status("applieds3528");if(!r.booted)return r;
    for(int b=0;b<bps;++b)in.process();
    std::vector<float> L;const int keys[4]={48,60,67,72};
    for(int k:keys){EventList on;auto e=note(true,k,0);on.addEvent(e);
        for(int b=0;b<bps;++b){in.process(b==0?&on:nullptr);L.insert(L.end(),in.left.begin(),in.left.end());}
        EventList off;auto f=note(false,k,0);off.addEvent(f);for(int b=0;b<bps/2;++b){in.process(b==0?&off:nullptr);L.insert(L.end(),in.left.begin(),in.left.end());}}
    double e=0;float pk=0;for(float x:L){e+=double(x)*x;pk=std::max(pk,std::abs(x));}
    r.rmsDb=10*std::log10(std::max(1e-30,e/double(L.size())));r.peakDb=20*std::log10(std::max(1e-15f,pk));return r;
}
int levelsMain(const VST3::Hosting::PluginFactory& factory,const VST3::UID& cid,FUnknown* host,const std::string& rom,const std::vector<std::string>& floppies,const std::string& models,const std::string& routes,int s3528){
    static const char* names[7]={"historical","ssm96","ssm192","legacyq","eiii","model2-96","model2-192"};
    {std::string fresh;Instance in;if(in.create(factory,cid,host)){fresh=in.getState();std::istringstream t(fresh);std::string l;
        while(std::getline(t,l)&&l!="calibration:")if(l.rfind("model=",0)==0||l.rfind("native=",0)==0||l.rfind("s3528=",0)==0||l.rfind("autoboot=",0)==0)std::cout<<"FRESH "<<l<<"\n";}}
    int bad=0;
    {   // Settings coherence: project state wins, applied state is reported, autoboot honoured.
        Instance in;in.create(factory,cid,host);std::string st=in.getState();
        st=replaceKey(st,"rom",rom);st=replaceKey(st,"floppy",floppies[0]);st=replaceKey(st,"model","5");st=replaceKey(st,"native","1");st=replaceKey(st,"s3528","0");st=replaceKey(st,"autoboot","0");
        check(in.setState(st)&&in.start(48000,kOffline,512),"SETTINGS project state with autoboot=0 accepted");
        for(int b=0;b<400;++b)in.process();
        check(in.status("loaded")=="0","SETTINGS autoboot=0: project load does not boot");
        const auto back=in.getState();
        check(back.find("\nmodel=5\n")!=std::string::npos&&back.find("\nnative=1\n")!=std::string::npos&&back.find("\ns3528=0\n")!=std::string::npos&&back.find("\nautoboot=0\n")!=std::string::npos,"SETTINGS model/route/S3528/autoboot persist in project state");
        Instance b;b.create(factory,cid,host);check(b.setState(replaceKey(back,"autoboot","1"))&&b.start(48000,kOffline,512),"SETTINGS same project with autoboot=1");
        bool ready=false;for(int k=0;k<93*150&&!ready;++k){b.process();if(k%93==0)ready=b.ready();}
        check(ready&&b.status("appliedmodel")=="5"&&b.status("appliednative")=="1"&&b.status("applieds3528")=="0","SETTINGS autoboot=1 boots and applies model 2 / hardware DAC / S3528 OFF from the project");
        check(b.setState(replaceKey(replaceKey(back,"model","4"),"native","0"))&&[&]{for(int k=0;k<200;++k)b.process();return true;}()&&b.status("appliedmodel")=="4"&&b.status("appliednative")=="0","SETTINGS a later project state is applied to the running engine");
    }
    for(const auto& floppy:floppies)for(char mc:models){const unsigned m=unsigned(mc-'0');if(m>6)continue;
        for(char rc:routes){const int native=rc=='n'?1:rc=='r'?0:-1;
            const auto r=levelRun(factory,cid,host,rom,floppy,m,native,s3528);
            std::cout<<"LEVEL bank=\""<<floppy.substr(floppy.find_last_of("\\/")+1)<<"\" model="<<names[m]<<" native="<<native<<" s3528="<<s3528<<" booted="<<r.booted
                     <<" rms_dBFS="<<r.rmsDb<<" peak_dBFS="<<r.peakDb<<" applied("<<r.applied<<") lcd='"<<r.lcd<<"'\n"<<std::flush;
            if(!r.booted)++bad;}}
    return bad?1:0;
}
// ---------------- slider automation load (run fragmentation) ----------------
// Fresh instance (owner defaults), boot, then 6 s offline per variant with N
// slider points per 512-frame block for slider A (and B). Reports wall time per
// emulated second and engine diagnostics: dense host points must not split the
// machine/DSP runs into partial blocks (the plug-in fell behind real time).
int slidersMain(const VST3::Hosting::PluginFactory& factory,const VST3::UID& cid,FUnknown* host,const std::string& rom,const std::string& floppy){
    Instance in;if(!in.create(factory,cid,host))return 1;std::string st=in.getState();
    st=replaceKey(st,"rom",rom);st=replaceKey(st,"floppy",floppy);st=replaceKey(st,"autoboot","1");
    if(!in.setState(st)||!in.start(48000,kOffline,512))return 1;
    bool ready=false;for(int b=0;b<93*150&&!ready;++b){in.process();if(b%93==0)ready=in.ready();}
    if(!ready){std::cout<<"SLIDERS boot failed\n";return 1;}
    for(int b=0;b<93;++b)in.process();
    int bad=0;
    for(unsigned control:{0u,1u})for(int points:{0,1,16,64}){
        const auto t0=std::chrono::steady_clock::now();const double ticks0=std::stod(in.status("ticks"));
        for(int b=0;b<93*6;++b){
            ParameterChanges changes;
            if(points){int32 qi=0;auto* q=changes.addParameterData(ParamID(control),qi);
                for(int k=0;k<points;++k){int32 pi=0;const double ph=std::fmod((b*512.0+k*512.0/points)/48000*3,1.0);q->addPoint(k*512/points,ph<.5?2*ph:2-2*ph,pi);}}
            in.process(nullptr,points?&changes:nullptr);
        }
        const double wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count(),emu=(std::stod(in.status("ticks"))-ticks0)/4e6;
        std::cout<<"SLIDERS control="<<char('A'+control)<<" points/block="<<points<<" wall_per_emulated_s="<<wall/emu<<" machineload="<<in.status("machineload")<<" dspwait="<<in.status("dspwait")<<" dspdrops="<<in.status("dspdrops")<<" eventdrops="<<in.status("eventdrops")<<"\n"<<std::flush;
    }
    return bad;
}
// Slider versus wheel through the whole plug-in path: MIDI note -> audio onset
// (offline, exact) and wall-clock keep-up (realtime, paced), while one panel
// control is moved by host points, by a no-echo GUI edit, or by MIDI CC1.
int rtslidersMain(const VST3::Hosting::PluginFactory& factory,const VST3::UID& cid,FUnknown* host,const std::string& rom,const std::string& floppy,bool fast){
    Instance in;if(!in.create(factory,cid,host))return 1;std::string st=in.getState();
    st=replaceKey(st,"rom",rom);st=replaceKey(st,"floppy",floppy);st=replaceKey(st,"autoboot","1");st=replaceKey(st,"wheels","1");st=replaceKey(st,"fastpanel",fast?"1":"0");
    const int block=256;
    if(!in.setState(st)||!in.start(48000,kOffline,block))return 1;
    const int perSecond=48000/block;bool ready=false;
    for(int b=0;b<perSecond*150&&!ready;++b){in.process();if(b%perSecond==0)ready=in.ready();}
    if(!ready){std::cout<<"RTSLIDERS boot failed\n";return 1;}
    for(int b=0;b<perSecond;++b)in.process();
    const int latency=int(in.processor->getLatencySamples());
    struct Cond{const char* name;int kind;unsigned control;}; // kind 0 idle, 1 host point/block, 2 GUI no-echo, 3 MIDI CC1
    const Cond conds[]={{"idle",0,0},{"A-host",1,0},{"B-host",1,1},{"C-host",1,2},{"D-host",1,3},{"RWheel-host",1,5},{"A-gui",2,0},{"B-gui",2,1},{"CC1-midi",3,0},{"idle",0,0}};
    auto drive=[&](const Cond& c,std::uint64_t b,ParameterChanges& changes,EventList& ev)->bool{
        const double ph=std::fmod(double(b)*block/48000*4.0,1.0),v=0.1+0.8*(ph<.5?2*ph:2-2*ph); // 4 Hz back-and-forth
        if(c.kind==1){int32 qi=0;auto* q=changes.addParameterData(ParamID(c.control),qi);int32 pi=0;q->addPoint(0,v,pi);return true;}
        if(c.kind==2)in.controller->setParamNormalized(ParamID(c.control),v);
        if(c.kind==3){int32 qi=0;auto* q=changes.addParameterData(ParamID(1000+1),qi);int32 pi=0;q->addPoint(0,v,pi);return true;}
        return false;};
    int bad=0;
    // Phase 1: offline, exact emulated timing through the plug-in queue.
    for(const auto& c:conds){
        std::vector<double> lat;std::vector<float> mono;std::vector<std::int64_t> onsets;std::uint64_t b0=0;
        for(int n=0;n<16;++n){
            const int noteBlocks=int(0.4*perSecond);const int key=48+(n*5)%24;
            for(int k=0;k<noteBlocks;++k,++b0){
                ParameterChanges changes;EventList ev(8);const bool pc=drive(c,b0,changes,ev);
                if(k==0){Event e=note(true,key,100);ev.addEvent(e);onsets.push_back(std::int64_t(mono.size())+100);}
                if(k==int(0.12*perSecond)){Event e=note(false,key,0);ev.addEvent(e);}
                in.process(&ev,pc?&changes:nullptr);
                for(int j=0;j<block;++j)mono.push_back(in.left[std::size_t(j)]);
            }
        }
        for(auto o:onsets){
            const std::int64_t from=o+latency;double pre=0;int np=0;for(std::int64_t j=std::max<std::int64_t>(0,from-480);j<from&&j<std::int64_t(mono.size());++j){pre+=double(mono[std::size_t(j)])*mono[std::size_t(j)];++np;}
            const double thr=std::max(2e-3,6*std::sqrt(pre/std::max(1,np)));
            for(std::int64_t j=from;j<from+48000*0.15&&j<std::int64_t(mono.size());++j)if(std::abs(mono[std::size_t(j)])>thr){lat.push_back(double(j-o-latency)/48.0);break;}
        }
        double mean=0,mx=0,var=0;for(double x:lat){mean+=x;mx=std::max(mx,x);}mean/=std::max<size_t>(1,lat.size());for(double x:lat)var+=(x-mean)*(x-mean);
        std::cout<<"RTSLIDERS offline "<<c.name<<": onsets "<<lat.size()<<"/16 note->audio (beyond reported latency "<<latency<<") mean "<<mean<<" ms sd "<<std::sqrt(var/std::max<size_t>(1,lat.size()))<<" max "<<mx
                 <<" eventdrops="<<in.status("eventdrops")<<"\n"<<std::flush;
    }
    // Phase 2: realtime, wall-clock paced audio thread.
    in.data.processMode=kRealtime;timeBeginPeriod(1);
    for(const auto& c:conds){
        const auto late0=std::stoull(in.status("underruns"));const auto reb0=in.status("rebases");
        std::uint64_t lateBlocks=0;double lateMax=0;
        const auto period=std::chrono::duration<double>(double(block)/48000);auto deadline=std::chrono::steady_clock::now();
        for(std::uint64_t b=0;b<std::uint64_t(perSecond*6);++b){
            deadline+=std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
            std::this_thread::sleep_until(deadline-std::chrono::microseconds(500));while(std::chrono::steady_clock::now()<deadline){}
            ParameterChanges changes;EventList ev(8);const bool pc=drive(c,b,changes,ev);
            if(b%std::uint64_t(perSecond*0.4)==0){Event e=note(true,60+int(b%12),10);ev.addEvent(e);}
            if(b%std::uint64_t(perSecond*0.4)==std::uint64_t(perSecond*0.12)){Event e=note(false,60+int((b-std::uint64_t(perSecond*0.12))%12),0);ev.addEvent(e);}
            const auto t0=std::chrono::steady_clock::now();in.process(&ev,pc?&changes:nullptr);
            const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-t0).count();if(us>lateMax)lateMax=us;if(us>period.count()*5e5)++lateBlocks;
        }
        const auto late=std::stoull(in.status("underruns"))-late0;if(late)++bad;
        std::cout<<"RTSLIDERS realtime "<<c.name<<": late_frames="<<late<<" rebases="<<reb0<<"->"<<in.status("rebases")<<" slow_process_calls="<<lateBlocks<<" process_max_us="<<lateMax
                 <<" machineload="<<in.status("machineload")<<" ownerlagms="<<in.status("ownerlagms")<<" minlead="<<in.status("minlead")<<"\n"<<std::flush;
    }
    timeEndPeriod(1);
    return bad;
}
int soakMain(const VST3::Hosting::PluginFactory& factory,const VST3::UID& cid,FUnknown* host,const std::string& rom,const std::string& floppy,
             const std::string& mode,double minutes,const std::vector<int>& blocks){
    std::atomic<bool> loadRun{true};std::vector<std::thread> load;
    const unsigned loadThreads=2; // background load: 2 threads at 60 % duty
    for(unsigned i=0;i<loadThreads;++i)load.emplace_back([&]{std::uint64_t x=1;while(loadRun){const auto t=std::chrono::steady_clock::now();while(std::chrono::steady_clock::now()-t<std::chrono::milliseconds(6))x=x*6364136223846793005ull+1;Sleep(4);}if(x==42)std::cout<<"";});
    timeBeginPeriod(1);int bad=0;
    for(int block:blocks){
        Instance in;if(!in.create(factory,cid,host)||!in.setState(modeConfig(mode,rom,floppy))||!in.start(48000,kOffline,block)){std::cout<<"SOAK setup failed\n";return 1;}
        const int perSecond=48000/block;bool ready=false;
        for(int b=0;b<perSecond*400&&!ready;++b){in.process();if(b%perSecond==0)ready=in.ready();}
        if(!ready){std::cout<<"SOAK "<<mode<<" block="<<block<<" boot failed lcd0='"<<in.status("lcd0")<<"'\n";++bad;continue;}
        const auto lateBefore=std::stoull(in.status("underruns"));
        const std::string bootLoad=in.status("machineload"),bootDsp=in.status("dspload");
        in.data.processMode=kRealtime;
        std::atomic<bool> run{true};std::atomic<std::uint64_t> preempts{0},suspendedUs{0},blocksDone{0},overrunBlocks{0},audioLateMaxUs{0},audioLateBlocks{0},maxSuspendUs{0};
        std::thread audio([&]{
            using Set=HANDLE(WINAPI*)(LPCWSTR,LPDWORD);HMODULE avrt=LoadLibraryW(L"avrt.dll");HANDLE task=nullptr;
            if(avrt)if(auto set=reinterpret_cast<Set>(reinterpret_cast<void*>(GetProcAddress(avrt,"AvSetMmThreadCharacteristicsW")))){DWORD i=0;task=set(L"Pro Audio",&i);}
            std::mt19937 rng{unsigned(block)};EventList ev(32);int held[4]={-1,-1,-1,-1};std::uint64_t nextNote=0;
            const auto period=std::chrono::duration<double>(double(block)/48000);auto deadline=std::chrono::steady_clock::now();
            for(std::uint64_t b=0;run;++b){
                deadline+=std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
                std::this_thread::sleep_until(deadline-std::chrono::microseconds(500));while(std::chrono::steady_clock::now()<deadline){}
                {const auto lateUs=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-deadline).count();
                 if(lateUs>std::int64_t(audioLateMaxUs))audioLateMaxUs=std::uint64_t(lateUs);if(lateUs>2000)++audioLateBlocks;}
                ev.clear();
                if(b>=nextNote){const int slot=int(rng()%4);
                    if(held[slot]>=0){Event e=note(false,held[slot],int(rng()%unsigned(block)));ev.addEvent(e);held[slot]=-1;}
                    else{held[slot]=48+int(rng()%25);Event e=note(true,held[slot],int(rng()%unsigned(block)));ev.addEvent(e);}
                    nextNote=b+std::uint64_t(perSecond/8+int(rng()%unsigned(perSecond/3)));}
                const auto t0=std::chrono::steady_clock::now();in.process(&ev);++blocksDone;
                if(std::chrono::steady_clock::now()-t0>period/2)++overrunBlocks;
            }
            if(task)if(auto revert=reinterpret_cast<BOOL(WINAPI*)(HANDLE)>(reinterpret_cast<void*>(GetProcAddress(avrt,"AvRevertMmThreadCharacteristics"))))revert(task);
            if(avrt)FreeLibrary(avrt);
        });
        std::thread injector([&]{
            std::mt19937 rng{7u+unsigned(block)};
            while(run){
                Sleep(200+rng()%1300);if(!run)break;
                const DWORD id=findThread(L"EII VST3 machine");if(!id)continue;
                const HANDLE h=OpenThread(THREAD_SUSPEND_RESUME,FALSE,id);if(!h)continue;
                const unsigned ms=5+rng()%16;const auto t=std::chrono::steady_clock::now();
                if(SuspendThread(h)!=DWORD(-1)){Sleep(ms);ResumeThread(h);++preempts;const auto us=std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-t).count());suspendedUs+=us;if(us>maxSuspendUs)maxSuspendUs=us;}
                CloseHandle(h);
            }
        });
        std::this_thread::sleep_for(std::chrono::duration<double>(minutes*60));
        run=false;audio.join();injector.join();
        const auto late=std::stoull(in.status("underruns"))-lateBefore;
        std::cout<<"SOAK mode="<<mode<<" block="<<block<<" minutes="<<minutes<<" latency="<<in.processor->getLatencySamples()<<" late_frames="<<late
                 <<" blocks="<<blocksDone.load()<<" slow_process_calls="<<overrunBlocks.load()<<" audio_thread_late_blocks="<<audioLateBlocks.load()<<" audio_thread_late_max_ms="<<audioLateMaxUs.load()/1000.0<<" preemptions="<<preempts.load()<<" suspended_ms="<<suspendedUs.load()/1000<<" max_suspend_ms="<<maxSuspendUs.load()/1000.0
                 <<" min_lead_frames="<<in.status("minlead")<<" rebases="<<in.status("rebases")<<" boot_machine_load="<<bootLoad<<" boot_dsp_load="<<bootDsp<<" machine_load="<<in.status("machineload")<<" dsp_wait="<<in.status("dspwait")<<" dsp_load="<<in.status("dspload")<<" margin_ms="<<(std::getenv("EII_TEST_MARGIN")?std::getenv("EII_TEST_MARGIN"):"default")<<" owner_mmcss="<<in.status("ownermmcss")<<" owner_highqos="<<in.status("ownerhighqos")<<" highreswait="<<in.status("highreswait")<<" callback_maxgap_ms="<<in.status("callbackmaxgapms")<<" dspdrops="<<in.status("dspdrops")<<" eventdrops="<<in.status("eventdrops")<<" midi_read="<<in.status("midiread")
                 <<" placement="<<placementSetting<<"\n"<<std::flush;
        if(late)++bad;
    }
    timeEndPeriod(1);loadRun=false;for(auto& t:load)t.join();
    return bad?1:0;
}
// Boots offline, plays A3 and A4, returns their fundamentals.
struct PlayResult{double a3=0,c4=0,a4=0,level=0,silence=0;double bootSeconds=0,wallSeconds=0,speed=0;bool booted=false;};
PlayResult bootAndPlay(Instance& in,const std::string& wavPath){
    PlayResult r;const auto t0=std::chrono::steady_clock::now();
    const int blocksPerSecond=int(in.rate)/in.block;
    for(int b=0;b<blocksPerSecond*120;++b){in.process();if(b%blocksPerSecond==0&&in.ready()){r.booted=true;break;}}
    r.bootSeconds=std::stod(in.status("ticks").empty()?"0":in.status("ticks"))/4e6;
    std::cout<<"  boot: lcd0='"<<in.status("lcd0")<<"' lcd1='"<<in.status("lcd1")<<"' emulated "<<r.bootSeconds<<" s\n";
    if(!r.booted)return r;
    for(int b=0;b<blocksPerSecond;++b)in.process(); // settle
    std::vector<float> L,R;const unsigned latency=in.processor->getLatencySamples();
    auto run=[&](int blocks,EventList* ev){for(int b=0;b<blocks;++b){in.process(b==0?ev:nullptr);L.insert(L.end(),in.left.begin(),in.left.end());R.insert(R.end(),in.right.begin(),in.right.end());}};
    std::size_t marks[3];
    const int noteBlocks=blocksPerSecond*3/2,gap=blocksPerSecond;
    run(gap/2,nullptr);r.silence=rms(L,0,L.size());
    int keys[3]={57,60,69};
    for(int k=0;k<3;++k){
        EventList on;auto e=note(true,keys[k],100);on.addEvent(e);marks[k]=L.size()+100+latency;run(noteBlocks,&on);
        EventList off;auto f=note(false,keys[k],0);off.addEvent(f);run(gap,&off);
    }
    const auto w=std::size_t(in.rate*0.1);
    r.a3=pitch(L,marks[0]+std::size_t(in.rate*0.3),w,in.rate);r.c4=pitch(L,marks[1]+std::size_t(in.rate*0.3),w,in.rate);r.a4=pitch(L,marks[2]+std::size_t(in.rate*0.3),w,in.rate);
    r.level=std::min({rms(L,marks[0],std::size_t(in.rate)),rms(L,marks[1],std::size_t(in.rate)),rms(L,marks[2],std::size_t(in.rate))});
    r.wallSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
    r.speed=std::stod(in.status("ticks"))/4e6/r.wallSeconds; // offline emulation speed incl. host overhead
    std::cout<<"  midi received/read "<<in.status("midireceived")<<"/"<<in.status("midiread")<<" frames "<<in.status("frames")<<" peak "<<in.status("peak")<<" lcd0='"<<in.status("lcd0")<<"' error='"<<in.status("error")<<"'\n";
    if(!wavPath.empty())writeWav(wavPath,L,R,unsigned(in.rate));
    return r;
}
}

int main(int argc,char** argv)try{
    if(argc<4){std::cerr<<"usage: vst3_host_test <EmulatorII.vst3> <rom-folder> <floppy> [wav-folder]\n";return 2;}
    if(const char* p=std::getenv("EII_CPU_PLACEMENT"))placementSetting=p; // read before InitDll may rewrite it
    const std::string bundle=argv[1],rom=argv[2],floppy=argv[3],wavDir=argc>4?argv[4]:"";const bool quick=argc>5&&std::string(argv[5])=="quick";
    std::string error;auto module=VST3::Hosting::Module::create(bundle,error);
    check(bool(module),"load bundle through VST3::Hosting::Module "+error);if(!module)return 1;
    const auto& factory=module->getFactory();
    VST3::UID cid;bool found=false;
    for(const auto& c:factory.classInfos())if(c.category()==kVstAudioEffectClass){cid=c.ID();found=true;
        std::cout<<"  class '"<<c.name()<<"' "<<c.subCategoriesString()<<" vendor '"<<c.vendor()<<"' sdk "<<c.sdkVersion()<<"\n";
        check(c.subCategoriesString()=="Instrument|Sampler","subcategory Instrument|Sampler");}
    check(found,"audio module class present");if(!found)return 1;
    auto host=owned(new HostApplication());
    if(argc>5&&std::string(argv[5])=="rtsliders"){const int r=rtslidersMain(factory,cid,host,rom,floppy,argc>6&&std::string(argv[6])=="fast");module.reset();return r;}
    if(argc>5&&std::string(argv[5])=="sliders"){const int r=slidersMain(factory,cid,host,rom,floppy);module.reset();return r;}
    if(argc>5&&std::string(argv[5])=="levels"){ // levels <models 0-6> <routes r|n|d> <s3528 -1|0|1> [second floppy]
        std::vector<std::string> fl{floppy};if(argc>9)fl.push_back(argv[9]);
        const int r=levelsMain(factory,cid,host,rom,fl,argc>6?argv[6]:"0123456",argc>7?argv[7]:"rn",argc>8?std::stoi(argv[8]):-1);module.reset();return r;
    }
    if(argc>6&&std::string(argv[5])=="soak"){
        std::vector<int> blocks{128,256,512};if(argc>8){blocks.clear();std::istringstream b(argv[8]);std::string x;while(std::getline(b,x,','))blocks.push_back(std::stoi(x));}
        const int r=soakMain(factory,cid,host,rom,floppy,argv[6],argc>7?std::stod(argv[7]):3.5,blocks);module.reset();return r;
    }

    // 1) 48 kHz offline: boot from floppy, play A3/A4.
    PlayResult r48;std::string saved;
    {
        Instance in;check(in.create(factory,cid,host),"instantiate + initialize (component/controller/diagnostics)");
        BusInfo inputInfo{};SpeakerArrangement inputArrangement=SpeakerArr::kStereo,outputArrangement=SpeakerArr::kStereo;
        check(in.component->getBusCount(kAudio,kInput)==1&&in.component->getBusInfo(kAudio,kInput,0,inputInfo)==kResultOk&&inputInfo.channelCount==2,"sampling stereo input bus advertised");
        check(in.processor->setBusArrangements(&inputArrangement,1,&outputArrangement,1)==kResultTrue,"stereo sampling input accepted");
        inputArrangement=SpeakerArr::kMono;
        check(in.processor->setBusArrangements(&inputArrangement,1,&outputArrangement,1)==kResultTrue&&in.component->getBusInfo(kAudio,kInput,0,inputInfo)==kResultOk&&inputInfo.channelCount==1,"mono sampling input accepted and reported");
        check(in.processor->setBusArrangements(nullptr,0,&outputArrangement,1)==kResultTrue,"output-only host accepted");
        inputArrangement=SpeakerArr::kStereo;in.processor->setBusArrangements(&inputArrangement,1,&outputArrangement,1);
        check(in.setState(configText(rom,floppy)),"setState with ROM folder + floppy");
        check(in.start(48000,kOffline,512),"setupProcessing/activate offline 48 kHz");
        std::cout<<"  latency "<<in.processor->getLatencySamples()<<" samples\n";
        r48=bootAndPlay(in,wavDir.empty()?"":wavDir+"\\vst3_host_test_48k.wav");
        check(r48.booted,"firmware boot reached preset display");
        std::cout<<"  48k: MIDI57="<<r48.a3<<" Hz MIDI60="<<r48.c4<<" Hz MIDI69="<<r48.a4<<" Hz level="<<r48.level<<" pre-note rms="<<r48.silence<<" wall="<<r48.wallSeconds<<" s\n";
        check(r48.level>1e-3,"nonzero audio after note on");
        check(r48.a3>0&&std::abs(r48.a4/r48.a3-2)<0.04,"MIDI 69/57 octave ratio within 2%");
        // Grand Piano #2 sounds one octave below concert pitch through the real firmware;
        // the core-only tools/midi_play_probe gives MIDI 60 = 130.8 Hz for the same disk.
        check(std::abs(1200*std::log2(r48.c4/130.81))<60,"MIDI 60 within 60 cents of 130.8 Hz (core midi_play_probe reference)");
        check(std::abs(1200*std::log2(r48.a4/220.0))<60,"MIDI 69 within 60 cents of 220 Hz");
        check(in.status("dspdrops")=="0"&&in.status("eventdrops")=="0","no DSP or event drops");
        check(in.status("underruns")=="0","offline render had no late frames");
        double lit=0;int defaultWidth=0;const bool editorOk=editorTest(in,wavDir.empty()?"":wavDir+"\\vst3_editor.bmp",lit,defaultWidth);
        std::cout<<"  editor capture: "<<int(lit*100)<<"% non-black pixels, default width "<<defaultWidth<<"\n";
        check(editorOk&&lit>0.5,"editor opens twice in a host HWND, draws, resizes with aspect ratio, reopens at the last size, closes");
        check(defaultWidth>=1500,"default editor width >= 1500 px (DPI-scaled)");
        check(in.getState().find("editor=1100x427")!=std::string::npos,"editor size stored in the state"); // the size editorTest set last
        // automation-visible parameter + state round trip
        in.controller->setParamNormalized(0,0.3);
        saved=in.getState();
        check(saved.find("rom=")!=std::string::npos&&saved.find("controls=1.5")!=std::string::npos,"getState contains paths and slider A");
        check(saved.find("HXCPICFE")==std::string::npos&&saved.size()<16384,"state holds no media bytes ("+std::to_string(saved.size())+" bytes)");
    }
    if(quick){std::cout<<(failures?"RESULT FAIL ":"RESULT PASS ")<<failures<<" failure(s) (quick)\n";return failures?1:0;}
    {
        Instance b;check(b.create(factory,cid,host),"second instance");
        auto noBoot=saved;const auto p=noBoot.find("autoboot=1");if(p!=std::string::npos)noBoot.replace(p,10,"autoboot=0");
        check(b.setState(noBoot),"setState round trip");
        check(std::abs(b.controller->getParamNormalized(0)-0.3)<1e-9,"restored slider A parameter");
        auto again=b.getState();auto expect=noBoot;
        check(again==expect,"state round trip identical");
    }
    // 2) 44.1 kHz: resampled output keeps the pitch; realtime mode paced by wall clock.
    {
        Instance in;in.create(factory,cid,host);in.setState(configText(rom,floppy));
        check(in.start(44100,kOffline,441),"setupProcessing/activate offline 44.1 kHz");
        const auto r=bootAndPlay(in,wavDir.empty()?"":wavDir+"\\vst3_host_test_44k1.wav");
        std::cout<<"  44.1k: MIDI57="<<r.a3<<" Hz MIDI60="<<r.c4<<" Hz MIDI69="<<r.a4<<" Hz level="<<r.level<<"\n";
        check(r.booted&&std::abs(r.a4/r48.a4-1)<0.005&&std::abs(r.a3/r48.a3-1)<0.005&&std::abs(r.c4/r48.c4-1)<0.005,"44.1 kHz pitch matches 48 kHz within 0.5%");
        // switch the same activation to realtime (processMode per call), paced in real time
        // 1 ms timer resolution, as audio hosts run; the default 15.6 ms tick turns
        // this paced loop into bursts of blocks no audio device would produce.
        timeBeginPeriod(1);
        in.data.processMode=kRealtime;const auto before=std::stoull(in.status("underruns"));
        const auto t0=std::chrono::steady_clock::now();const double period=double(in.block)/in.rate;
        for(int b=0;b<int(3.0/period);++b){
            EventList ev;Event e=note((b/50)%2==0,64,10);if(b%50==0)ev.addEvent(e);in.process(&ev);
            std::this_thread::sleep_until(t0+std::chrono::duration<double>(period*(b+1)));
        }
        timeEndPeriod(1);
        const auto late=std::stoull(in.status("underruns"))-before;
        std::cout<<"  realtime 3 s: late frames "<<late<<" (offline speed "<<r48.speed<<"x real time)\n";
        // A realtime guarantee needs CPU headroom; below 1.3x it is reported, not judged.
        if(r48.speed>=1.3)check(late<=std::uint64_t(in.rate*0.05),"realtime pacing: late frames <= 50 ms over 3 s");
        else std::cout<<"INFO realtime pacing not judged: this build/CPU emulates only "<<r48.speed<<"x real time\n";
    }
    // 3) two instances created, processed and destroyed concurrently
    {
        std::atomic<int> ok{0},started{0};
        std::atomic<unsigned> peak{0};std::atomic<bool> running{true};
        auto worker=[&](int id){
            Instance in;if(!in.create(factory,cid,host)){++started;return;}
            in.setState(configText(rom,floppy));if(!in.start(id?44100:48000,kRealtime,256)){++started;return;}
            // Both instances live for well under 100 ms on a fast PC, less than
            // one pass of the polling watcher. Count once while both are known
            // to be running (bounded wait for the other instance).
            ++started;for(int w=0;w<2000&&started<2;++w)Sleep(1);
            for(unsigned seen=eiiThreads(),old=peak.load();seen>old&&!peak.compare_exchange_weak(old,seen);){}
            for(int b=0;b<400;++b)if(!in.process())return;
            in.shutdown();++ok;
        };
        std::thread watch([&]{while(running){peak=std::max(peak.load(),eiiThreads());Sleep(20);}});
        std::thread a(worker,0),b(worker,1);a.join();b.join();running=false;watch.join();
        check(ok==2,"two concurrent instances processed and torn down");
        std::cout<<"  EII threads while both ran: "<<peak.load()<<"\n";
        check(peak>=2,"each instance owns its machine thread");
    }
    const auto after=eiiThreads();
    std::cout<<"  EII threads after teardown: "<<after<<"\n";
    check(after==0,"no plug-in threads left after teardown");
    module.reset();
    std::cout<<(failures?"RESULT FAIL ":"RESULT PASS ")<<failures<<" failure(s)\n";
    return failures?1:0;
}catch(const std::exception& e){std::cerr<<"exception: "<<e.what()<<"\n";return 1;}
