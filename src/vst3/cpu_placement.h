#pragma once
// Optional plug-in CPU placement. The standalone's host_cpu_placement.h puts its
// two busy threads on fixed cores (one machine per process). In a DAW every
// instance would claim the same pair, so the plug-in keeps that off and, when
// enabled (EII_CPU_PLACEMENT=dynamic), hands each busy thread its own least
// claimed physical core of the largest L3 group instead (soft CPU sets: a
// preference, no affinity mask, priority or registry change).
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace eii::vst {
class CorePool {
public:
    // Decided once in InitDll (EII_CPU_PLACEMENT=dynamic), which then sets the
    // variable to 0 so the core's fixed standalone placement stays off.
    static std::atomic<bool>& enabledFlag(){static std::atomic<bool> f{false};return f;}
    static bool enabled(){return enabledFlag().load();}
    // Returns a claim index (or -1) and applies the CPU set to thread h.
    static int place(HANDLE h){
        auto& p=instance();std::lock_guard l(p.mutex_);
        if(!p.ready_)p.scan();if(p.cores_.empty()||!p.setSets_)return -1;
        std::size_t best=0;for(std::size_t i=1;i<p.cores_.size();++i)if(p.claims_[i]<p.claims_[best])best=i;
        if(!p.setSets_(h,p.cores_[best].data(),ULONG(p.cores_[best].size())))return -1;
        ++p.claims_[best];return int(best);
    }
    static void release(int claim){auto& p=instance();std::lock_guard l(p.mutex_);if(claim>=0&&std::size_t(claim)<p.claims_.size()&&p.claims_[std::size_t(claim)])--p.claims_[std::size_t(claim)];}
    // The SSM2045 worker names itself "EII SSM2045" when it starts; find the one
    // that belongs to the engine just created (not yet known to the pool).
    static DWORD claimNewDspThread(){
        using GetDescription=HRESULT(WINAPI*)(HANDLE,PWSTR*);
        static const auto get=reinterpret_cast<GetDescription>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetThreadDescription")));
        if(!get)return 0;
        auto& p=instance();
        for(int attempt=0;attempt<50;++attempt){
            DWORD found=0;const HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);THREADENTRY32 t{};t.dwSize=sizeof(t);
            if(Thread32First(snap,&t))do{
                if(t.th32OwnerProcessID!=GetCurrentProcessId())continue;
                {std::lock_guard l(p.mutex_);if(p.dsp_.count(t.th32ThreadID))continue;}
                const HANDLE h=OpenThread(THREAD_QUERY_LIMITED_INFORMATION,FALSE,t.th32ThreadID);if(!h)continue;
                PWSTR name=nullptr;if(SUCCEEDED(get(h,&name))&&name){if(std::wstring(name)==L"EII SSM2045")found=t.th32ThreadID;LocalFree(name);}CloseHandle(h);
            }while(!found&&Thread32Next(snap,&t));
            CloseHandle(snap);
            if(found){std::lock_guard l(p.mutex_);p.dsp_.insert(found);return found;}
            Sleep(1);
        }
        return 0;
    }
    static void forgetDspThread(DWORD id){auto& p=instance();std::lock_guard l(p.mutex_);p.dsp_.erase(id);}
private:
    using GetInfo=BOOL(WINAPI*)(PSYSTEM_CPU_SET_INFORMATION,ULONG,PULONG,HANDLE,ULONG);
    using SetSets=BOOL(WINAPI*)(HANDLE,const ULONG*,ULONG);
    static CorePool& instance(){static CorePool p;return p;}
    void scan(){
        ready_=true;const HMODULE k=GetModuleHandleW(L"kernel32.dll");
        const auto getInfo=reinterpret_cast<GetInfo>(reinterpret_cast<void*>(GetProcAddress(k,"GetSystemCpuSetInformation")));
        setSets_=reinterpret_cast<SetSets>(reinterpret_cast<void*>(GetProcAddress(k,"SetThreadSelectedCpuSets")));
        if(!getInfo||!setSets_)return;
        ULONG length=0;getInfo(nullptr,0,&length,GetCurrentProcess(),0);if(!length)return;
        std::vector<unsigned char> buffer(length);if(!getInfo(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data()),length,&length,GetCurrentProcess(),0))return;
        struct Core{WORD group;BYTE core,llc;std::vector<ULONG> ids;};std::vector<Core> all;
        for(ULONG off=0;off<length;){const auto* e=reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(buffer.data()+off);if(!e->Size)break;
            if(e->Type==CpuSetInformation){const auto& s=e->CpuSet;auto it=std::find_if(all.begin(),all.end(),[&](const Core& c){return c.group==s.Group&&c.core==s.CoreIndex;});
                if(it==all.end()){all.push_back({s.Group,s.CoreIndex,s.LastLevelCacheIndex,{}});it=all.end()-1;}it->ids.push_back(s.Id);}
            off+=e->Size;}
        BYTE llc=0;std::size_t count=0;
        for(const auto& c:all){const auto n=std::size_t(std::count_if(all.begin(),all.end(),[&](const Core& o){return o.llc==c.llc&&o.group==c.group;}));if(n>count){count=n;llc=c.llc;}}
        std::vector<const Core*> group;for(const auto& c:all)if(c.llc==llc)group.push_back(&c);
        std::sort(group.begin(),group.end(),[](const Core* a,const Core* b){return a->group!=b->group?a->group<b->group:a->core<b->core;});
        for(std::size_t i=group.size()>=3?1:0;i<group.size();++i)cores_.push_back(group[i]->ids); // skip the system's first core
        claims_.assign(cores_.size(),0);
    }
    std::mutex mutex_;bool ready_=false;SetSets setSets_=nullptr;
    std::vector<std::vector<ULONG>> cores_;std::vector<unsigned> claims_;std::set<DWORD> dsp_;
};
}
#endif
