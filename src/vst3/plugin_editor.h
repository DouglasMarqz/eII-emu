#pragma once
// VST3 editor: the GUI v1.0 vector panel (windows_vector_panel.h, unchanged)
// embedded as a child of the host-provided HWND. No VSTGUI. Settings use
// ordinary UI-thread menus/dialogs; nothing here runs inside process().
#include "vst3/plugin_engine.h"
#include "vst3/panel_glyphs.h"
#include "windows_vector_panel.h"
#include "windows_filter_service.h"
#include "windows_filter_service_profiles.h"
#include <commdlg.h>
#include <shobjidl.h>
#include <commctrl.h>
#include <optional>
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"

namespace eii::vst {
HINSTANCE moduleInstance();
void ensureGdiplus(); // module-lifetime GDI+ reference (released in ExitDll)

// Implemented by the plug-in object; called on the UI thread only.
struct EditorBackend {
    virtual ~EditorBackend()=default;
    virtual PluginEngine& engine()=0;
    virtual bool hasEngine()=0; // false after IPluginBase::terminate (editor may outlive it in odd hosts)
    virtual PluginSettings settings()=0;
    virtual void updateSettings(const std::function<void(PluginSettings&)>& change)=0;
    virtual void editParameter(unsigned id,double normalized)=0; // begin/perform (handler) + local value
    virtual void endParameterEdit()=0;
    virtual double parameter(unsigned id)=0;
    virtual void rememberEditorSize(int width,int height)=0; // stored in the project state
    virtual bool saveDefaults()=0; // write current settings as the new-instance defaults
    virtual void retain()=0;virtual void drop()=0;
};

class Editor final : public Steinberg::IPlugView, public Steinberg::IPlugViewContentScaleSupport {
public:
    // Default fits a typical desktop. Fine legends need explicit zoom; host
    // content scale or parent monitor DPI also scales the initial view.
    static constexpr int defaultWidth=1500,minWidth=1000;
    static int heightFor(int w){return int(std::lround(w*double(panel::height)/panel::width));}
    explicit Editor(EditorBackend& b):backend_(b){
        backend_.retain();const auto s=backend_.settings();
        if(s.editorWidth>=minWidth){userSized_=true;size_={0,0,s.editorWidth,heightFor(s.editorWidth)};}
        else size_={0,0,defaultWidth,heightFor(defaultWidth)};
    }
    ~Editor(){detach();backend_.drop();}
    // FUnknown
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid,void** obj)override{
        if(Steinberg::FUnknownPrivate::iidEqual(iid,Steinberg::FUnknown::iid)||Steinberg::FUnknownPrivate::iidEqual(iid,Steinberg::IPlugView::iid)){addRef();*obj=static_cast<IPlugView*>(this);return Steinberg::kResultOk;}
        if(Steinberg::FUnknownPrivate::iidEqual(iid,Steinberg::IPlugViewContentScaleSupport::iid)){addRef();*obj=static_cast<IPlugViewContentScaleSupport*>(this);return Steinberg::kResultOk;}
        *obj=nullptr;return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef()override{return ++refs_;}
    Steinberg::uint32 PLUGIN_API release()override{const auto n=--refs_;if(!n)delete this;return n;}
    // IPlugView
    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type)override{return std::strcmp(type,Steinberg::kPlatformTypeHWND)==0?Steinberg::kResultTrue:Steinberg::kResultFalse;}
    Steinberg::tresult PLUGIN_API attached(void* parent,Steinberg::FIDString type)override{
        if(!parent||isPlatformTypeSupported(type)!=Steinberg::kResultTrue||container_)return Steinberg::kResultFalse;
        // Some old hosts call onSize with their default before attachment. Read
        // the instance preference again; those provisional sizes are not user zoom.
        attaching_=true;const auto saved=backend_.settings();
        if(saved.editorWidth>=minWidth){userSized_=true;size_={0,0,saved.editorWidth,heightFor(saved.editorWidth)};}
        if(!userSized_&&!hostScaled_){ // no host content scale: follow the parent's monitor DPI
            using GetDpi=UINT(WINAPI*)(HWND);const auto get=reinterpret_cast<GetDpi>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow")));
            const UINT dpi=get?get(static_cast<HWND>(parent)):96;if(dpi>96)rescale(double(dpi)/96.0,false);
        }
        try{create(static_cast<HWND>(parent));}catch(const std::exception& e){detach();MessageBoxA(static_cast<HWND>(parent),e.what(),"Emulator II",MB_ICONERROR);return Steinberg::kResultFalse;}
        // The host may have queried getSize before attached() discovers the DPI.
        if(frame_){auto requested=size_;if(frame_->resizeView(this,&requested)==Steinberg::kResultTrue)onSize(&requested);}
        attaching_=false;
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API removed()override{detach();return Steinberg::kResultOk;}
    Steinberg::tresult PLUGIN_API onWheel(float)override{return Steinberg::kResultFalse;}
    Steinberg::tresult PLUGIN_API onKeyDown(Steinberg::char16,Steinberg::int16,Steinberg::int16)override{return Steinberg::kResultFalse;}
    Steinberg::tresult PLUGIN_API onKeyUp(Steinberg::char16,Steinberg::int16,Steinberg::int16)override{return Steinberg::kResultFalse;}
    Steinberg::tresult PLUGIN_API getSize(Steinberg::ViewRect* r)override{if(!r)return Steinberg::kInvalidArgument;*r=size_;return Steinberg::kResultOk;}
    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* r)override{
        if(!r||r->getWidth()<=0||r->getHeight()<=0)return Steinberg::kInvalidArgument;size_=*r;
        if(container_&&!attaching_&&backend_.hasEngine()){userSized_=true;backend_.rememberEditorSize(r->getWidth(),r->getHeight());}
        if(container_)SetWindowPos(container_,nullptr,0,0,r->getWidth(),r->getHeight(),SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOMOVE);
        if(panel_)SetWindowPos(panel_->handle(),nullptr,0,0,r->getWidth(),r->getHeight(),SWP_NOZORDER|SWP_NOACTIVATE);
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API onFocus(Steinberg::TBool)override{return Steinberg::kResultOk;}
    Steinberg::tresult PLUGIN_API setFrame(Steinberg::IPlugFrame* f)override{frame_=f;return Steinberg::kResultOk;}
    Steinberg::tresult PLUGIN_API canResize()override{return Steinberg::kResultTrue;}
    Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* r)override{
        if(!r)return Steinberg::kInvalidArgument;
        // Keep the panel aspect ratio, following whichever edge the user dragged more.
        const double dw=std::abs(r->getWidth()-size_.getWidth())/double(panel::width),dh=std::abs(r->getHeight()-size_.getHeight())/double(panel::height);
        int w=dh>dw?int(std::lround(r->getHeight()*double(panel::width)/panel::height)):r->getWidth();w=std::max(minWidth,w);
        r->right=r->left+w;r->bottom=r->top+heightFor(w);return Steinberg::kResultTrue;
    }
    // IPlugViewContentScaleSupport
    Steinberg::tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor)override{
        if(!(factor>0.25f&&factor<8.f))return Steinberg::kInvalidArgument;hostScaled_=true;
        if(!userSized_)rescale(factor,true);return Steinberg::kResultOk;
    }
private:
    std::optional<unsigned> pendingModel_;
    EditorBackend& backend_;std::atomic<Steinberg::uint32> refs_{1};
    Steinberg::ViewRect size_{};Steinberg::IPlugFrame* frame_=nullptr;bool userSized_=false,hostScaled_=false,attaching_=false;
    void requestWidth(int width){
        if(!frame_){ask(L"The host did not provide an editor resize interface.",L"Editor size",MB_ICONINFORMATION);return;}
        const int w=std::clamp(width,minWidth,4000);Steinberg::ViewRect r{0,0,w,heightFor(w)};
        if(frame_->resizeView(this,&r)==Steinberg::kResultTrue){
            // A legacy host can resize its frame without calling onSize back.
            onSize(&r);userSized_=true;backend_.rememberEditorSize(r.getWidth(),r.getHeight());
        }else ask(L"The host declined this editor size. Enable plug-in window resizing in the host.",L"Editor size",MB_ICONINFORMATION);
    }
    void rescale(double factor,bool notify){
        const int w=std::max(minWidth,int(std::lround(defaultWidth*factor)));Steinberg::ViewRect r{0,0,w,heightFor(w)};
        if(r.getWidth()==size_.getWidth())return;
        if(notify&&frame_&&container_){frame_->resizeView(this,&r);} // host answers with onSize
        else size_=r;
    }
    HWND container_=nullptr;std::unique_ptr<host::VectorPanelWindow> panel_;std::unique_ptr<host::FilterServiceWindow> filter_;
    analog::RuntimeAudioMode filterMode_=analog::RuntimeAudioMode::BasicVoice;
    HFONT font_=nullptr;std::uint64_t shownRevision_=0;std::string lastError_;bool modal_=false;
    static constexpr const wchar_t* containerClass=L"EIIVst3EditorHost";
    static constexpr UINT_PTR timerId=1;

    void create(HWND parent){
        ensureGdiplus();
        INITCOMMONCONTROLSEX common{sizeof(common),ICC_BAR_CLASSES};InitCommonControlsEx(&common);
        WNDCLASSW cls{};cls.lpfnWndProc=containerProc;cls.hInstance=moduleInstance();cls.lpszClassName=containerClass;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        RegisterClassW(&cls); // repeated registration fails harmlessly
        container_=CreateWindowExW(0,containerClass,L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,size_.getWidth(),size_.getHeight(),parent,nullptr,moduleInstance(),this);
        if(!container_)throw std::runtime_error("Could not create editor window");
        font_=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        const auto s=backend_.settings();
        host::VectorPanelWindow::Hooks hooks;
        hooks.contact=[this](ScannerButton b,bool down){if(backend_.hasEngine())backend_.engine().button(b,down);};
        hooks.voltage=[this](unsigned i,double v){backend_.editParameter(i,v/5.0);};
        hooks.volume=[this](float g){backend_.editParameter(6,g);};
        hooks.inputGain=[this](float g){backend_.updateSettings([g](PluginSettings& p){p.inputGain=g;});};
        hooks.save=[this]{backend_.endParameterEdit();};
        hooks.command=[this](unsigned id){command(id);};
        hooks.glyph=panelGlyph;
        std::array<double,6> controls;for(unsigned i=0;i<6;++i)controls[i]=backend_.parameter(i)*5.0;
        panel_=std::make_unique<host::VectorPanelWindow>(std::move(hooks),controls,float(backend_.parameter(6)),float(s.inputGain));
        panel_->traceState(backend_.hasEngine()&&backend_.engine().samplingTraceRecording()?1:0);
        const HWND h=panel_->handle();
        SetWindowLongPtrW(h,GWL_STYLE,WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS);SetWindowLongPtrW(h,GWL_EXSTYLE,0);
        SetParent(h,container_);
        SetWindowPos(h,nullptr,0,0,size_.getWidth(),size_.getHeight(),SWP_NOZORDER|SWP_FRAMECHANGED|SWP_SHOWWINDOW);
        SetTimer(container_,timerId,33,nullptr);
        tick();
    }
    void detach(){
        if(container_)KillTimer(container_,timerId);
        filter_.reset();panel_.reset();
        if(container_){SetWindowLongPtrW(container_,GWLP_USERDATA,0);DestroyWindow(container_);container_=nullptr;}
        if(font_){DeleteObject(font_);font_=nullptr;}
    }
    static LRESULT CALLBACK containerProc(HWND h,UINT m,WPARAM w,LPARAM l){
        if(m==WM_NCCREATE)SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));
        auto* self=reinterpret_cast<Editor*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(self&&m==WM_TIMER&&w==timerId){self->tick();return 0;}
        if(m==WM_ERASEBKGND)return 1;
        return DefWindowProcW(h,m,w,l);
    }
    HWND owner()const{return container_?GetAncestor(container_,GA_ROOT):nullptr;}
    int ask(const std::wstring& text,const wchar_t* title,UINT flags){modal_=true;const int r=MessageBoxW(owner(),text.c_str(),title,flags);modal_=false;return r;}
    static std::wstring wide(const std::string& s){
        if(s.empty())return {};const int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);
        std::wstring out(std::size_t(n),L' ');MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),out.data(),n);return out;}
    void tick(){
        if(!panel_||!backend_.hasEngine())return;
        if(pendingModel_&&(!filter_||!filter_->dispatchActive())){const auto id=*pendingModel_;pendingModel_.reset();changeModel(id);}
        if(filter_&&filterMode_!=backend_.settings().audioMode&&!filter_->dispatchActive())filter_.reset();
        const auto s=backend_.engine().status();
        panel_->traceState(backend_.engine().samplingTraceRecording()?1:0);
        if(s.panel.revision!=shownRevision_){shownRevision_=s.panel.revision;panel_->update(s.panel);}
        if(!modal_&&!s.panel.error.empty()&&s.panel.error!=lastError_){lastError_=s.panel.error;ask(wide(s.panel.error),L"Emulator II",MB_ICONINFORMATION);}
        if(s.panel.error.empty())lastError_.clear();
    }

    // ---- file pickers (UI thread) ----
    // Library layout: <library>\ROMS, \DISKS, \HDD. Dialogs start there when it exists.
    std::wstring libraryDir(const wchar_t* sub){
        const auto lib=backend_.settings().library;if(lib.empty())return {};
        std::error_code e;const auto d=lib/sub;return std::filesystem::is_directory(d,e)?d.wstring():std::wstring{};
    }
    bool openFile(std::filesystem::path& out,const wchar_t* filter,const wchar_t* title,const wchar_t* sub=nullptr){
        wchar_t name[32768]={};OPENFILENAMEW d{};d.lStructSize=sizeof(d);d.hwndOwner=owner();d.lpstrFile=name;d.nMaxFile=32768;d.lpstrFilter=filter;d.lpstrTitle=title;
        const auto dir=sub?libraryDir(sub):std::wstring{};if(!dir.empty())d.lpstrInitialDir=dir.c_str();
        d.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;modal_=true;const bool ok=GetOpenFileNameW(&d);modal_=false;if(ok)out=name;return ok;
    }
    // Copies always go to NEW files: existing files are never replaced.
    bool newFile(std::filesystem::path& out,const wchar_t* title,const std::wstring& ext){
        wchar_t name[32768]={};const auto suggestion=L"EII-copy."+ext;lstrcpynW(name,suggestion.c_str(),32768);
        OPENFILENAMEW d{};d.lStructSize=sizeof(d);d.hwndOwner=owner();d.lpstrFile=name;d.nMaxFile=32768;d.lpstrDefExt=ext.c_str();
        d.lpstrFilter=ext==L"hfe"?L"HFEv1 floppy\0*.hfe\0All files\0*.*\0":ext==L"emuiifd"?L"EMUIIFD floppy\0*.emuiifd\0All files\0*.*\0":L"EII raw HDD (*.img)\0*.img\0All files\0*.*\0";
        const auto dir=libraryDir(ext==L"img"?L"HDD":L"DISKS");if(!dir.empty())d.lpstrInitialDir=dir.c_str();
        d.lpstrTitle=title;d.Flags=OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;modal_=true;const bool ok=GetSaveFileNameW(&d);modal_=false;if(!ok)return false;
        out=name;std::error_code e;
        if(std::filesystem::exists(out,e)||e){ask(L"Choose a new filename. Existing disk files are never replaced.",L"Disk copy",MB_ICONINFORMATION);return false;}
        return true;
    }
    bool finish(std::future<std::string> f,const wchar_t* title){
        try{if(f.wait_for(std::chrono::seconds(10))!=std::future_status::ready)throw std::runtime_error("The machine did not answer within 10 s.");(void)f.get();return true;}
        catch(const std::exception& e){ask(wide(e.what()),title,MB_ICONERROR);return false;}
    }
    PanelMediaState media(){auto f=backend_.engine().mediaState();if(f.wait_for(std::chrono::seconds(5))!=std::future_status::ready)throw std::runtime_error("The machine did not answer.");return f.get();}
    bool saveCopy(bool hdd,const PanelHddState& state){
        if(!state.idle){ask(L"Finish the sampler disk operation before saving.",L"Disk copy",MB_ICONINFORMATION);return false;}
        std::filesystem::path p;if(!newFile(p,hdd?L"Save HDD copy to a NEW file":L"Save floppy copy to a NEW file",hdd?L"img":wide(state.format)))return false;
        if(!finish(hdd?backend_.engine().saveHddCopy(p):backend_.engine().saveFloppyCopy(p),L"Disk copy not saved; changes retained"))return false;
        // Saved copy becomes the selection for the next boot (standalone behavior).
        backend_.updateSettings([&](PluginSettings& s){(hdd?s.hdd:s.floppy)=p;});return true;
    }
    // Standalone policy: dirty working media need save/discard/cancel before reset or eject.
    // Returns -1 cancel, 0 nothing dirty/saved, 1 discard.
    int resolveDirty(bool includeHdd){
        PanelMediaState m;try{m=media();}catch(const std::exception& e){ask(wide(e.what()),L"Disk image",MB_ICONERROR);return -1;}
        const bool hdd=includeHdd&&m.hdd.dirty,floppy=m.floppy.dirty;if(!hdd&&!floppy)return 0;
        const int a=ask(L"Disk images have unsaved changes. Save copies first?\n\nYes: save each changed image to a new file\nNo: discard working-copy changes\nCancel: keep running",L"Unsaved disk changes",MB_YESNOCANCEL|MB_ICONWARNING);
        if(a==IDCANCEL)return -1;if(a==IDNO)return 1;
        if(hdd&&!saveCopy(true,m.hdd))return -1;
        if(floppy&&!saveCopy(false,m.floppy))return -1;
        return 1; // saved copies; the in-memory working data equals them
    }
    void boot(){
        auto s=backend_.settings();
        if(s.romFolder.empty()){chooseRoms();s=backend_.settings();if(s.romFolder.empty())return;}
        const int d=resolveDirty(true);if(d<0)return;
        backend_.engine().boot(s,d>0);
    }
    bool pickFolder(std::filesystem::path& out,const wchar_t* title){
        const HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
        IFileOpenDialog* d=nullptr;bool ok=false;
        if(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&d)))){
            DWORD o=0;d->GetOptions(&o);d->SetOptions(o|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);d->SetTitle(title);
            modal_=true;const HRESULT shown=d->Show(owner());modal_=false;
            IShellItem* item=nullptr;
            if(SUCCEEDED(shown)&&SUCCEEDED(d->GetResult(&item))){PWSTR path=nullptr;
                if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){out=path;ok=true;CoTaskMemFree(path);}item->Release();}
            d->Release();
        }
        if(init==S_OK||init==S_FALSE)CoUninitialize();
        return ok;
    }
    // Choose (or create) the library root with ROMS, DISKS and HDD subfolders.
    // A ROMS folder holding main.bin becomes the ROM folder when none is set or it is gone.
    void chooseLibrary(){
        std::filesystem::path root;if(!pickFolder(root,L"Emulator II library folder (ROMS, DISKS, HDD are created inside)"))return;
        std::error_code e;for(const wchar_t* sub:{L"ROMS",L"DISKS",L"HDD"})std::filesystem::create_directories(root/sub,e);
        backend_.updateSettings([&](PluginSettings& s){
            s.library=root;std::error_code x;
            if((s.romFolder.empty()||!std::filesystem::exists(s.romFolder/"main.bin",x))&&std::filesystem::exists(root/L"ROMS"/"main.bin",x))s.romFolder=root/L"ROMS";
        });
        ask((L"Library: "+root.wstring()+L"\n\nROMS: main.bin, scanner.bin, ic135.bin, ic137.bin (main23.bin for HDD)\nDISKS: floppy images\nHDD: hard-disk images\n\nFile dialogs now start in these folders. Settings are saved for new instances automatically.").c_str(),L"Library folder",MB_ICONINFORMATION);
    }
    // Multi-select .EII picker: returns full paths (single pick or dir + names).
    std::vector<std::filesystem::path> openFiles(const wchar_t* filter,const wchar_t* title,const wchar_t* sub){
        std::vector<wchar_t> name(1<<17);OPENFILENAMEW d{};d.lStructSize=sizeof(d);d.hwndOwner=owner();d.lpstrFile=name.data();d.nMaxFile=DWORD(name.size());d.lpstrFilter=filter;d.lpstrTitle=title;
        const auto dir=libraryDir(sub);if(!dir.empty())d.lpstrInitialDir=dir.c_str();
        d.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_ALLOWMULTISELECT|OFN_EXPLORER;modal_=true;const bool ok=GetOpenFileNameW(&d);modal_=false;
        std::vector<std::filesystem::path> out;if(!ok)return out;
        const std::wstring first=name.data();const wchar_t* p=name.data()+first.size()+1;
        if(!*p){out.emplace_back(first);return out;}
        while(*p){std::wstring f=p;out.push_back(std::filesystem::path(first)/f);p+=f.size()+1;}
        return out;
    }
    void importEiiToHdd(){
        const auto s=backend_.settings();
        if(s.hdd.empty()){ask(L"Select or create an HDD image first (HDD menu).",L"Import .EII",MB_ICONINFORMATION);return;}
        const auto files=openFiles(L"EII banks (*.eii)\0*.eii\0\0",L"Select .EII banks to write into free HDD slots",L"DISKS");if(files.empty())return;
        const bool direct=backend_.engine().status().panel.hdd.direct;
        std::filesystem::path target;
        if(direct){if(ask(L"Write "+std::to_wstring(files.size())+L" bank(s) into the free slots of\n"+s.hdd.wstring()+L"\n(direct image writes are on) and reboot?",L"Import .EII",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK)return;}
        else if(!newFile(target,L"Save the HDD with the imported banks as a NEW file",L"img"))return;
        try{auto f=backend_.engine().importEiiToHdd(files,target);
            if(f.wait_for(std::chrono::seconds(30))!=std::future_status::ready)throw std::runtime_error("The machine did not answer within 30 s.");
            const auto msg=f.get();if(!target.empty())backend_.updateSettings([&](PluginSettings& x){x.hdd=target;});
            ask(wide(msg),L"Import .EII",MB_ICONINFORMATION);}
        catch(const std::exception& e){ask(wide(e.what()),L"Import .EII",MB_ICONERROR);}
    }
    void chooseRoms(){
        std::filesystem::path p;
        if(openFile(p,L"Prepared main ROM\0main.bin\0Binary ROM\0*.bin\0\0",L"Select main.bin in your prepared ROM folder",L"ROMS"))
            backend_.updateSettings([&](PluginSettings& s){s.romFolder=p.parent_path();});
    }
    void insertFloppy(bool eject){
        std::filesystem::path p;
        if(!eject&&!openFile(p,L"EII floppy images and banks\0*.hfe;*.emuiifd;*.e2o;*.eii\0EII bank (*.eii)\0*.eii\0All files\0*.*\0\0",L"Select an OS / bank disk or an .EII bank",L"DISKS"))return;
        const auto s=backend_.settings();
        if(!backend_.engine().status().panel.loaded){backend_.updateSettings([&](PluginSettings& x){x.floppy=p;});return;}
        const int d=resolveDirty(false);if(d<0)return;
        if(finish(backend_.engine().mountFloppy(p,s.directImageWrites,d>0),L"Floppy"))backend_.updateSettings([&](PluginSettings& x){x.floppy=p;});
    }
    void showStatus(){
        const auto s=backend_.engine().status();const auto cfg=backend_.settings();
        std::wostringstream o;
        o<<L"Selected VCF / VCA: "<<wide(audioModelName(cfg.audioMode))
         <<L"\nRunning VCF / VCA: "<<(s.panel.loaded?wide(audioModelName(s.panel.audioMode)):L"not booted")<<L"\nROM folder: "<<(cfg.romFolder.empty()?L"(none)":cfg.romFolder.wstring())<<L"\nFloppy (boot): "<<(cfg.floppy.empty()?L"(none)":cfg.floppy.wstring())
         <<L"\nHDD (boot): "<<(cfg.hdd.empty()?L"(none)":cfg.hdd.wstring())
         <<L"\nPanel response running: "<<(s.panel.loaded?(s.fastPanelActive?L"FAST / CUSTOM":L"original scanner firmware"):L"not booted")
         <<L"\nPanel response next boot: "<<(cfg.fastPanel?L"FAST / CUSTOM":L"original scanner firmware")
         <<L"\nMemory running: "<<(s.panel.loaded?(s.memory1mbActive?L"1 MB (2 x 512 KB banks)":L"512 KB"):L"not booted")<<L", next boot: "<<(cfg.memory1mb?L"1 MB (2 x 512 KB banks; floppy boot needs main21dm.bin)":L"512 KB")
         <<L"\n\nMounted floppy: "<<(s.panel.floppy.mounted?(!s.panel.floppy.writable?L"read-only":s.panel.floppy.direct?L"DIRECT":s.panel.floppy.dirty?L"COPY / UNSAVED":L"COPY"):L"none")
         <<L"\nMounted HDD: "<<(s.panel.hdd.mounted?(s.panel.hdd.direct?L"DIRECT":s.panel.hdd.dirty?L"COPY / UNSAVED":L"COPY"):L"none")
         <<L"\n"<<wide(s.panel.startup)<<L"\n"<<wide(s.panel.media)
         <<L"\n\nHost "<<s.hostRate<<L" Hz, latency "<<s.latency<<L" samples, "<<(s.anchored?L"running":L"waiting for host processing")
         <<L"\nEmulated "<<double(s.panel.ticks)/4e6<<L" s, speed "<<int(s.panel.speedRatio*100+.5)<<L"%"
         <<L"\nLate frames (realtime underruns): "<<s.underruns<<L", stall recoveries: "<<backend_.engine().rebases()
         <<L"\nMachine load "<<int(s.machineLoad*100+.5)<<L"% of real time, DSP load "<<int(s.panel.dsp.load*100+.5)<<L"%"<<L", event drops: "<<s.eventDrops<<L", DSP drops: "<<s.dspDrops
         <<L"\nTrace / detailed timing probes: "<<(backend_.engine().samplingTraceRecording()?L"ON":L"OFF")
         <<L"\nHost callbacks: "<<s.callbacks<<L", max gap: "<<s.callbackMaxGapMs<<L" ms, owner lag: "<<s.ownerLagMs<<L" ms"
         <<L"\nOwner MMCSS: "<<s.ownerMmcss<<L", HighQoS: "<<s.ownerHighQos<<L", DSP MMCSS: "<<s.panel.dsp.multimedia<<L", DSP HighQoS: "<<s.panel.dsp.highQos<<L", high-resolution waits: "<<s.highResolutionWait
         <<L"\nSampling input frames: "<<s.inputFrames<<L", dropped: "<<s.inputDrops<<L", missing reads: "<<s.inputMissing
         <<L"\nInput calibration: "<<s.inputTrimDb<<L" dB | SAR reads: "<<s.sampleReads<<L", unknown: "<<s.sampleUnknownReads<<L", analog rail clips: "<<s.sampleRailClips
         <<L"\nPreset recall: "<<(s.recallProgram<0?std::wstring(L"none"):L"program "+std::to_wstring(s.recallProgram&0x7f)+L" ch "+std::to_wstring(((s.recallProgram>>8)&15)+1))<<(s.recallPending?L" (pending)":L"");
        if(!s.panel.error.empty())o<<L"\n\nLast message: "<<wide(s.panel.error);
        ask(o.str(),L"Emulator II status",MB_ICONINFORMATION);
    }
    void changeModel(unsigned id){
        // A file chooser/notification may pump this editor's timer. Never
        // delete its service while that service still has a call on the stack.
        if(filter_&&filter_->dispatchActive()){pendingModel_=id;return;}
        filter_.reset();
        const auto mode=audioModeFromIndex(id==48?6:id==47?5:id==46?4:id==45?3:unsigned(id-40));
        backend_.updateSettings([&](PluginSettings& x){x.audioMode=mode;});
        backend_.engine().audioModel(mode);
    }
    void settingsMenu(){
        const auto s=backend_.settings();const auto st=backend_.engine().status();
        HMENU menu=CreatePopupMenu(),model=CreatePopupMenu(),hdd=CreatePopupMenu(),floppy=CreatePopupMenu();
        auto check=[](bool b){return UINT(MF_STRING|(b?MF_CHECKED:0));};
        AppendMenuW(menu,MF_STRING,2,s.library.empty()?L"Library folder (ROMS / DISKS / HDD)...":(L"Library folder: "+s.library.wstring()+L"...").c_str());
        AppendMenuW(menu,MF_STRING,1,L"ROM folder... (next boot)");
        AppendMenuW(menu,MF_STRING,4,s.osImage.empty()?L"OS image for .EII banks... (automatic: OS 3.1 in the library)":(L"OS image for .EII banks: "+s.osImage.filename().wstring()+L"...").c_str());
        AppendMenuW(floppy,MF_STRING,10,L"Insert floppy image or .EII bank...");AppendMenuW(floppy,MF_STRING,11,L"Save floppy copy... (finish DISK first)");AppendMenuW(floppy,MF_STRING,12,L"Eject floppy");
        AppendMenuW(menu,MF_POPUP,UINT_PTR(floppy),L"Floppy");
        AppendMenuW(hdd,MF_STRING,20,L"Select HDD image... (next boot)");AppendMenuW(hdd,MF_STRING,21,L"Create blank 20 MB HDD... (next boot)");
        AppendMenuW(hdd,MF_STRING,22,L"No HDD (next boot)");AppendMenuW(hdd,MF_STRING,24,L"Import .EII banks into free HDD slots...");AppendMenuW(hdd,MF_STRING,23,L"Save HDD copy... (finish DISK 7 first)");
        AppendMenuW(menu,MF_POPUP,UINT_PTR(hdd),L"HDD");
        AppendMenuW(menu,check(s.directImageWrites),30,L"Direct image writes (next mount / boot)");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        const auto m=audioModeIndex(s.audioMode);
        // Requested settings (project state) vs what the running engine applies.
        const bool running=st.panel.loaded,historical=s.audioMode==analog::RuntimeAudioMode::BasicVoice;
        const bool modelPending=running&&st.panel.audioMode!=s.audioMode;
        AppendMenuW(model,MF_STRING,46,L"EIII CEM3387 VCF - CUSTOM / EII VCA (96k)");
        AppendMenuW(model,MF_STRING,45,L"Legacy + Q - fast VCF / linear VCA (96k)");
        AppendMenuW(model,MF_STRING,41,L"SSM2045 - nonlinear VCF / VCA (96k)");
        AppendMenuW(model,MF_STRING,42,L"SSM2045 - nonlinear VCF / VCA (192k)");
        AppendMenuW(model,MF_STRING,47,L"SSM2045 model 2 - block-diagram VCF / VCA (96k)");
        AppendMenuW(model,MF_STRING,48,L"SSM2045 model 2 - block-diagram VCF / VCA (192k)");
        AppendMenuW(model,MF_SEPARATOR,0,nullptr);
        AppendMenuW(model,MF_STRING,40,L"Historical Legacy - zero-Q VCF / linear VCA");
        CheckMenuRadioItem(model,40,48,m==6?48:m==5?47:m==4?46:m==3?45:m==2?42:m==1?41:40,MF_BYCOMMAND);
        AppendMenuW(menu,MF_POPUP,UINT_PTR(model),modelPending?L"VCF / VCA model (choose one) - requested, not yet active":L"VCF / VCA model (choose one)");
        HMENU stages=CreatePopupMenu();
        // DAC route: ON = AM6072 + IC73 deglitch integrator (faithful, default);
        // OFF = no DAC, raw digital codes (level-matched at 1 kHz).
        const bool nativeRequested=s.nativeAudio&&!historical;
        std::wstring hw=L"Hardware DAC + deglitch front-end (faithful)",fast=L"No DAC: raw digital codes";
        if(historical)hw+=L" - not available in Historical Legacy";
        else if(running&&!modelPending&&st.panel.nativeAudio!=s.nativeAudio)hw+=L" - requested, not active";
        AppendMenuW(stages,MF_STRING|(nativeRequested?MF_CHECKED:0)|(historical?MF_GRAYED:0),43,hw.c_str());
        AppendMenuW(stages,MF_STRING|(nativeRequested?0:MF_CHECKED),49,fast.c_str());
        std::wstring s3528=L"S3528 reconstruction after VCF";if(running&&st.panel.reconstruction!=s.reconstruction)s3528+=L" - requested, not active";
        AppendMenuW(stages,check(s.reconstruction),44,s3528.c_str());
        AppendMenuW(menu,MF_POPUP,UINT_PTR(stages),L"Common signal stages");
        HMENU postGain=CreatePopupMenu();
        for(int db=0;db<=int(postOutputMaxDb);++db){const auto label=L"+"+std::to_wstring(db)+L" dB"+(db==0?L" (unity)":L"");AppendMenuW(postGain,check(std::abs(s.postOutputDb-db)<.01),UINT(840+db),label.c_str());}
        AppendMenuW(menu,MF_POPUP,UINT_PTR(postGain),L"Final host output gain (after engine)");
        HMENU inputTrim=CreatePopupMenu();
        for(int db=-24;db<=24;db+=3){const auto label=std::to_wstring(db)+L" dB"+(db==0?L" (unity / default)":L"");AppendMenuW(inputTrim,check(std::abs(s.sampleInputTrimDb-db)<.01),UINT(800+(db+24)/3),label.c_str());}
        const auto inputLabel=L"Sampling input calibration: "+std::to_wstring(s.sampleInputTrimDb)+L" dB";
        AppendMenuW(menu,MF_POPUP,UINT_PTR(inputTrim),inputLabel.c_str());
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,check(s.midiWheels),50,L"MIDI pitch bend / mod wheel to panel wheels");
        AppendMenuW(menu,check(s.noteOffZero),51,L"Note Off as 90/0");
        AppendMenuW(menu,check(s.autoBoot),52,L"Boot automatically when the project loads (stored in the project)");
        AppendMenuW(menu,check(s.fastPanel),53,L"Fast panel response (CUSTOM, next boot)");
        AppendMenuW(menu,check(s.sliderThrottle),56,L"Slider throttle - comfort option, not original behaviour (waits for the firmware queue)");
        HMENU bootOrder=CreatePopupMenu();
        AppendMenuW(bootOrder,check(!s.bootFloppy),57,L"1. Hard disk  2. Floppy (floppy goes in after boot)");
        AppendMenuW(bootOrder,check(s.bootFloppy),58,L"1. Floppy  2. Hard disk (original HD PROM order)");
        AppendMenuW(menu,MF_POPUP,UINT_PTR(bootOrder),s.bootFloppy?L"Boot order: Floppy first (next boot)":L"Boot order: Hard disk first (next boot)");
        AppendMenuW(menu,check(s.analogDrift),59,L"Analog drift - ESTIMATED, not measured (filter die temperature wander; off = exact)");
        AppendMenuW(menu,check(s.memory1mb),54,L"Memory 1 MB, two 512 KB banks (EII+ / EII+ HD; next boot)");
        HMENU margin=CreatePopupMenu();
        AppendMenuW(margin,check(s.marginMs==10),70,L"10 ms");AppendMenuW(margin,check(s.marginMs==20),71,L"20 ms");AppendMenuW(margin,check(s.marginMs==30),73,L"30 ms (default)");AppendMenuW(margin,check(s.marginMs==40),72,L"40 ms");
        AppendMenuW(menu,MF_POPUP,UINT_PTR(margin),L"Realtime safety margin (adds latency; next activation)");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        HMENU zoom=CreatePopupMenu();
        AppendMenuW(zoom,MF_STRING,900,L"Zoom -");AppendMenuW(zoom,MF_STRING,901,L"Zoom +");AppendMenuW(zoom,MF_STRING,902,L"Fit screen");
        for(unsigned i=0;i<4;++i){const int width=1000+int(i)*500;const auto label=std::to_wstring(width)+L" px";
            AppendMenuW(zoom,check(size_.getWidth()==width),910+i,label.c_str());}
        AppendMenuW(menu,MF_POPUP,UINT_PTR(zoom),L"Editor size / zoom");
        AppendMenuW(menu,MF_STRING,3,L"Save settings as default for new instances");
        AppendMenuW(menu,MF_STRING,60,L"Status...");
        (void)st;
        POINT pt;GetCursorPos(&pt);modal_=true;
        const auto id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,container_,nullptr);modal_=false;DestroyMenu(menu);
        auto& e=backend_.engine();
        switch(id){
        case 900:case 901:requestWidth(size_.getWidth()+(id==901?250:-250));break;
        case 910:case 911:case 912:case 913:requestWidth(1000+int(id-910)*500);break;
        case 902:{MONITORINFO info{sizeof(info)};if(GetMonitorInfoW(MonitorFromWindow(container_,MONITOR_DEFAULTTONEAREST),&info)){
            const int available=std::max(1,int(info.rcWork.bottom-info.rcWork.top)-120);
            requestWidth(std::min(int(info.rcWork.right-info.rcWork.left)-80,int(available*double(panel::width)/panel::height)));}break;}
        case 1:chooseRoms();break;
        case 2:chooseLibrary();break;
        case 4:{std::filesystem::path p;if(openFile(p,L"EII OS disk\0*.emuiifd;*.hfe\0All files\0*.*\0\0",L"OS disk used for .EII bank imports (OS 3.1 recommended)",L"DISKS"))backend_.updateSettings([&](PluginSettings& x){x.osImage=p;});break;}
        case 3:ask(backend_.saveDefaults()?L"Saved. New plug-in instances start with these settings (a loaded project still uses its own).":L"Could not write the settings file in %LOCALAPPDATA%\\EmulatorII-VST3.",L"Settings",MB_ICONINFORMATION);break;
        case 53:backend_.updateSettings([](PluginSettings& x){x.fastPanel=!x.fastPanel;});break;
        case 59:backend_.updateSettings([](PluginSettings& x){x.analogDrift=!x.analogDrift;});break;
        case 57:case 58:backend_.updateSettings([id](PluginSettings& x){x.bootFloppy=id==58;});break;
        case 54:backend_.updateSettings([](PluginSettings& x){x.memory1mb=!x.memory1mb;});break;
        case 56:backend_.updateSettings([](PluginSettings& x){x.sliderThrottle=!x.sliderThrottle;});break;
        case 10:insertFloppy(false);break;
        case 11:{PanelMediaState md;try{md=media();}catch(...){break;}if(!md.floppy.mounted){ask(L"No floppy mounted.",L"Floppy",MB_ICONINFORMATION);break;}saveCopy(false,md.floppy);break;}
        case 12:insertFloppy(true);break;
        case 20:{std::filesystem::path p;if(openFile(p,L"EII raw HDD\0*.img;*.hdd;*.bin\0All files\0*.*\0",L"Select raw EII HDD image (main23.bin required)",L"HDD")){backend_.updateSettings([&](PluginSettings& x){x.hdd=p;});ask(L"HDD selected. Boot / reset to apply; main23.bin will be used.",L"HDD",MB_ICONINFORMATION);}break;}
        case 21:{std::filesystem::path p;if(!newFile(p,L"Create a NEW blank EII HDD image",L"img"))break;
            try{host::saveNewHddImage(p,std::vector<std::uint8_t>(host::hddImageBytes));backend_.updateSettings([&](PluginSettings& x){x.hdd=p;});
                ask(L"Blank HDD created and selected.\n\nSelect the HD OS floppy, Boot / reset (main23.bin), then use SPECIAL 22 to format it.",L"HDD",MB_ICONINFORMATION);}
            catch(const std::exception& ex){ask(wide(ex.what()),L"Cannot create HDD",MB_ICONERROR);}break;}
        case 22:backend_.updateSettings([](PluginSettings& x){x.hdd.clear();});break;
        case 24:importEiiToHdd();break;
        case 23:{PanelMediaState md;try{md=media();}catch(...){break;}if(!md.hdd.mounted){ask(L"No HDD mounted.",L"HDD",MB_ICONINFORMATION);break;}saveCopy(true,md.hdd);break;}
        case 30:backend_.updateSettings([&](PluginSettings& x){x.directImageWrites=!s.directImageWrites;});break;
        case 40:case 41:case 42:case 45:case 46:case 47:case 48:changeModel(id);break;
        case 43:case 49:{const bool on=id==43;backend_.updateSettings([&](PluginSettings& x){x.nativeAudio=on;});e.nativeAudio(on);break;}
        case 44:backend_.updateSettings([&](PluginSettings& x){x.reconstruction=!s.reconstruction;});e.reconstruction(!s.reconstruction);break;
        case 50:case 51:{auto n=s;(id==50?n.midiWheels:n.noteOffZero)^=true;backend_.updateSettings([&](PluginSettings& x){x.midiWheels=n.midiWheels;x.noteOffZero=n.noteOffZero;});e.midiOptions(n.midiWheels,n.noteOffZero);break;}
        case 52:backend_.updateSettings([&](PluginSettings& x){x.autoBoot=!s.autoBoot;});break;
        case 60:showStatus();break;
        case 70:case 71:case 72:case 73:{const int ms=id==70?10:id==71?20:id==73?30:40;backend_.updateSettings([ms](PluginSettings& x){x.marginMs=ms;});break;}
        default:if(id>=840&&id<=840+unsigned(postOutputMaxDb)){backend_.editParameter(7,double(id-840)/postOutputMaxDb);backend_.endParameterEdit();}else if(id>=800&&id<=816)backend_.updateSettings([&](PluginSettings& x){x.sampleInputTrimDb=-24+3*int(id-800);});break;
        }
    }
    void command(unsigned id){
        if(!backend_.hasEngine())return;
        auto& e=backend_.engine();
        switch(id){
        case 123:settingsMenu();break;
        case 102:boot();break;
        case 103:{const auto s=e.status();e.setPaused(!s.paused);break;}
        case 115:e.allNotesOff();break;
        case 122:{
            if(filter_&&filter_->dispatchActive())break;
            const auto requestedModel=backend_.settings().audioMode;
            if(requestedModel==analog::RuntimeAudioMode::BasicVoice){ask(L"Historical zero-Q mode has no complete editable profile. Select Legacy + Q or SSM2045 in Settings > VCF / VCA model.",L"Historical Legacy",MB_ICONINFORMATION);break;}
            // Check before overwriting filterMode_: an old view must never be
            // relabelled to edit a different model's profile.
            if(filter_&&filterMode_!=requestedModel)filter_.reset();
            filterMode_=requestedModel;
            if(!filter_)filter_=std::make_unique<host::FilterServiceWindow>(activeCalibration(backend_.settings()),
                [this,editingModel=requestedModel](const analog::FilterCalibration& c){
                    if(backend_.settings().audioMode!=editingModel)throw std::runtime_error("Active model changed. Reopen Filter Service before editing.");
                    backend_.updateSettings([&](PluginSettings& x){if(editingModel==analog::RuntimeAudioMode::EiiiCustom)x.eiiiCalibration=c;else if(editingModel==analog::RuntimeAudioMode::LegacyQ)x.legacyCalibration=c;else if(analog::ssm2AudioMode(editingModel))x.ssm2Calibration=c;else x.calibration=c;});backend_.engine().calibrate(c);},
                []{return true;}, // stored in the project state
                [this]{host::FilterServiceMetrics m;const auto s=backend_.engine().status();m.dsp=s.panel.dsp;m.nativeAudio=s.panel.nativeAudio;m.reconstruction=s.panel.reconstruction;m.sampleInput=L"Host Sampling In connected to analog/SAR path. RECORD TRACE starts diagnostics; STOP + SAVE ends and exports them. Off by default.";return m;},
                [this](unsigned p){return filterMode_==analog::RuntimeAudioMode::EiiiCustom?analog::cemParameterSupported(p):filterMode_==analog::RuntimeAudioMode::LegacyQ?analog::legacyQParameterSupported(p):analog::ssm2AudioMode(filterMode_)?analog::ssm2ParameterSupported(p):analog::ssmParameterSupported(p);},filterMode_==analog::RuntimeAudioMode::LegacyQ,wide(audioModelName(requestedModel)),
                [this,editingModel=requestedModel](bool saveGlobal){
                    if(backend_.settings().audioMode!=editingModel)throw std::runtime_error("Active model changed. Reopen Filter Service.");
                    if(saveGlobal){const auto s=backend_.settings();host::saveGlobalFilterService({s.calibration,s.legacyCalibration,s.eiiiCalibration,s.ssm2Calibration});}
                    else {const auto profiles=host::loadGlobalFilterService();if(!profiles)throw std::runtime_error("No global Filter Service profile saved yet.");
                        backend_.updateSettings([&](PluginSettings& s){s.calibration=profiles->ssm;s.legacyCalibration=profiles->legacy;if(profiles->hasEiii)s.eiiiCalibration=profiles->eiii;if(profiles->hasSsm2)s.ssm2Calibration=profiles->ssm2;});
                        backend_.engine().calibrate(activeCalibration(backend_.settings()));}
                    return activeCalibration(backend_.settings());
                },filterMode_==analog::RuntimeAudioMode::EiiiCustom);
            filter_->show(owner(),font_);break;}
        case 125:{
            if(!e.samplingTraceRecording()){
                try{auto f=e.startSamplingTrace();if(f.wait_for(std::chrono::seconds(5))!=std::future_status::ready)throw std::runtime_error("Sampling trace start timed out");f.get();panel_->traceState(1);}
                catch(const std::exception& ex){ask(wide(ex.what()),L"Sampling trace",MB_ICONERROR);}
                break;
            }
            std::string snapshot;
            try{auto f=e.samplingTrace();if(f.wait_for(std::chrono::seconds(5))!=std::future_status::ready)throw std::runtime_error("Sampling trace timed out");snapshot=f.get();panel_->traceState(0);}
            catch(const std::exception& ex){ask(wide(ex.what()),L"Sampling trace",MB_ICONERROR);break;}
            wchar_t name[32768]=L"EII-sampling-trace.csv";OPENFILENAMEW d{};d.lStructSize=sizeof(d);d.hwndOwner=owner();d.lpstrFile=name;d.nMaxFile=32768;d.lpstrFilter=L"Sampling diagnostic CSV\0*.csv\0";d.lpstrDefExt=L"csv";d.lpstrTitle=L"Save recent sampling diagnostics";d.Flags=OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_OVERWRITEPROMPT;
            modal_=true;const bool ok=GetSaveFileNameW(&d);modal_=false;if(!ok)break;
            try{std::ofstream out(std::filesystem::path(name),std::ios::binary);out.write(snapshot.data(),std::streamsize(snapshot.size()));out.close();if(!out)throw std::runtime_error("Could not save sampling trace");ask(L"Recent sampling diagnostics saved from before the save dialog. No ROM, media or audio payload included.",L"Sampling trace",MB_ICONINFORMATION);}
            catch(const std::exception& ex){ask(wide(ex.what()),L"Sampling trace",MB_ICONERROR);}break;}

        default:break;
        }
    }
};
}
