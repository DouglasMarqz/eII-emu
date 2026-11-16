// Native Windows regression, no ROMs or audio device required.
#include "windows_filter_service.h"
#include <iostream>
#include <stdexcept>
using eii::host::FilterServiceWindow;
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);std::cout<<"PASS "<<message<<'\n';}
static HWND findService(HWND owner){
    struct Search{HWND owner,result=nullptr;}s{owner};
    EnumThreadWindows(GetCurrentThreadId(),[](HWND h,LPARAM p)->BOOL{
        auto& s=*reinterpret_cast<Search*>(p);wchar_t cls[64]{};GetClassNameW(h,cls,64);
        if(GetWindow(h,GW_OWNER)==s.owner&&std::wstring(cls)==L"EIIFilterService"){s.result=h;return FALSE;}return TRUE;
    },reinterpret_cast<LPARAM>(&s));return s.result;
}
static void pump(){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
int main()try{
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES};InitCommonControlsEx(&controls);
    const HWND owner=CreateWindowW(L"STATIC",L"Filter service lifecycle test",WS_OVERLAPPEDWINDOW,0,0,900,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(owner!=nullptr,"owner created");unsigned statsCalls=0;
    for(unsigned round=0;round<12;++round){
        auto service=std::make_unique<FilterServiceWindow>(eii::analog::FilterCalibration{},[](const auto&){},[]{return true;},[&]{++statsCalls;return eii::host::FilterServiceMetrics{};},std::function<bool(unsigned)>{},bool(round&1),round&1?L"Legacy Q":L"SSM2045");
        service->show(owner,static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));const HWND h=findService(owner);
        require(h&&GetWindowLongPtrW(h,GWLP_USERDATA)!=0,"live service has window callback target");
        SendMessageW(h,WM_TIMER,1,0);const auto before=statsCalls;
        PostMessageW(h,WM_TIMER,1,0);service.reset(); // exact model-switch ownership action
        require(!IsWindow(h),"model switch destroys service HWND before freeing target");
        pump();require(statsCalls==before,"queued old timer cannot call released service");
    }
    {
        FilterServiceWindow service({},[](const auto&){},[]{return true;},[]{return eii::host::FilterServiceMetrics{};});
        service.show(owner,nullptr);auto h=findService(owner);SendMessageW(h,WM_CLOSE,0,0);
        require(IsWindow(h)&&!IsWindowVisible(h),"normal close hides retained service");
        service.show(owner,nullptr);require(IsWindowVisible(h),"retained service can reopen");
        DestroyWindow(h);require(!IsWindow(h),"external HWND destruction clears ownership safely");
    }
    {
        FilterServiceWindow* live=nullptr;bool guarded=false;
        FilterServiceWindow service({},[](const auto&){},[]{return true;},[&]{guarded=live&&live->dispatchActive();return eii::host::FilterServiceMetrics{};});live=&service;
        service.show(owner,nullptr);SendMessageW(findService(owner),WM_TIMER,1,0);
        require(guarded&&!service.dispatchActive(),"reentrant model switch can detect active dispatch and defer deletion");
    }
    DestroyWindow(owner);pump();std::cout<<"Filter service lifecycle regression passed\n";
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
