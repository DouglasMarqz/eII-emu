#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <chrono>
#include <map>
#include <sstream>
#include <vector>
#include <algorithm>
#include <iomanip>

namespace eii::host {
// UI only: manual capture, or1Hz while an explicit performance trace is active.
// No enumeration when tracing is off; keep a separate instance per sampling series.
class WindowsThreadCapture {
    struct Times {unsigned long long created=0,cpu=0;};
    struct Entry {DWORD id;Times time;std::string name;double busy=-1;};
    std::map<DWORD,Times> previous_;
    std::chrono::steady_clock::time_point previousTime_{};
    static unsigned long long value(FILETIME t) noexcept {
        return (static_cast<unsigned long long>(t.dwHighDateTime)<<32)|t.dwLowDateTime;
    }
public:
    std::string capture() {
        const auto now=std::chrono::steady_clock::now();
        const double elapsed=previousTime_.time_since_epoch().count()
            ?std::chrono::duration<double>(now-previousTime_).count():0;
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
        if(snapshot==INVALID_HANDLE_VALUE)return "HOST THREADS enumeration-error="+std::to_string(GetLastError());
        std::vector<Entry> entries;std::map<DWORD,Times> current;
        unsigned total=0,unreadable=0;
        THREADENTRY32 te{};te.dwSize=sizeof(te);
        for(BOOL ok=Thread32First(snapshot,&te);ok;ok=Thread32Next(snapshot,&te)) {
            if(te.th32OwnerProcessID!=GetCurrentProcessId())continue;
            ++total;
            HANDLE thread=OpenThread(THREAD_QUERY_LIMITED_INFORMATION,FALSE,te.th32ThreadID);
            if(!thread){++unreadable;continue;}
            FILETIME created,exited,kernel,user;
            if(GetThreadTimes(thread,&created,&exited,&kernel,&user)) {
                Entry e{te.th32ThreadID,{value(created),value(kernel)+value(user)},"unlabelled",-1};
                PWSTR description=nullptr;
                if(SUCCEEDED(GetThreadDescription(thread,&description)) && description) {
                    if(*description){e.name.clear();for(auto p=description;*p;++p)e.name+=(*p>=32 && *p<127)?char(*p):'?';}
                    LocalFree(description);
                }
                const auto it=previous_.find(e.id);
                if(elapsed>0 && it!=previous_.end() && it->second.created==e.time.created && e.time.cpu>=it->second.cpu)
                    e.busy=double(e.time.cpu-it->second.cpu)*1e-7/elapsed;
                current[e.id]=e.time;entries.push_back(std::move(e));
            } else ++unreadable;
            CloseHandle(thread);
        }
        CloseHandle(snapshot);
        std::sort(entries.begin(),entries.end(),[](const auto&a,const auto&b){return a.busy>b.busy;});
        std::ostringstream out;out<<std::fixed<<std::setprecision(4);
        out<<"HOST THREADS count="<<total<<" unreadable="<<unreadable<<" interval-s="<<elapsed
           <<" (same live thread between captures; busy=one-core fraction; -1=no prior sample; lifetime-cpu-s=cumulative)";
        for(const auto&e:entries)out<<"\nHOST THREAD tid="<<e.id<<" name=["<<e.name<<"] busy="<<e.busy<<" lifetime-cpu-s="<<double(e.time.cpu)*1e-7;
        previous_=std::move(current);previousTime_=now;return out.str();
    }
};
}
#endif
