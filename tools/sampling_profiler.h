// In-process sampling profiler for the offline probes (Windows, no admin
// rights, no ETW). A helper thread suspends every other thread of the process
// about once per millisecond, unwinds its stack and counts the return-address
// chains. write() stores "thread_id count rva rva ..." lines (innermost
// first, RVAs relative to the EXE); tools/profile_report.py resolves them
// with llvm-symbolizer including inlined frames. Build the probe with -g.
// Measurement tool only: never part of the plug-in.
#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <atomic>
#include <cstdio>
#include <map>
#include <thread>
#include <vector>
namespace eii::tools {
class SamplingProfiler {
public:
    SamplingProfiler():base_(reinterpret_cast<std::uint64_t>(GetModuleHandleW(nullptr))),thread_([this]{loop();}){}
    ~SamplingProfiler(){stop();}
    void enable(bool on){enabled_.store(on);}
    void stop(){if(thread_.joinable()){quit_.store(true);thread_.join();}}
    void write(const char* path){
        stop();std::FILE* f=std::fopen(path,"w");if(!f)return;
        for(const auto& [key,count]:samples_){std::fprintf(f,"%lu %llu",key.first,(unsigned long long)count);for(auto a:key.second)std::fprintf(f," %llx",(unsigned long long)a);std::fputc('\n',f);}
        std::fclose(f);
    }
private:
    void loop(){
        timeBeginPeriod(1);const DWORD self=GetCurrentThreadId(),pid=GetCurrentProcessId();
        std::map<DWORD,HANDLE> handles;unsigned refresh=0;
        while(!quit_.load()){
            Sleep(1);if(!enabled_.load())continue;
            if(!(refresh++&255)){
                HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);THREADENTRY32 e{};e.dwSize=sizeof e;
                if(Thread32First(snap,&e))do{if(e.th32OwnerProcessID==pid&&e.th32ThreadID!=self&&!handles.count(e.th32ThreadID))
                    if(HANDLE h=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,e.th32ThreadID))handles[e.th32ThreadID]=h;}while(Thread32Next(snap,&e));
                CloseHandle(snap);
            }
            for(const auto& [id,h]:handles){
                if(SuspendThread(h)==DWORD(-1))continue;
                CONTEXT c{};c.ContextFlags=CONTEXT_FULL;std::vector<std::uint64_t> chain;
                if(GetThreadContext(h,&c)){
                    for(unsigned depth=0;depth<24&&c.Rip;++depth){
                        chain.push_back(c.Rip-base_);
                        DWORD64 imageBase=0;const auto fn=RtlLookupFunctionEntry(c.Rip,&imageBase,nullptr);
                        if(!fn){if(depth)break;c.Rip=*reinterpret_cast<DWORD64*>(c.Rsp);c.Rsp+=8;continue;}
                        void* handler=nullptr;DWORD64 frame=0;
                        RtlVirtualUnwind(UNW_FLAG_NHANDLER,imageBase,c.Rip,fn,&c,&handler,&frame,nullptr);
                        if(c.Rip-base_>0x10000000ull)break; // left the EXE (system DLL frames are not needed)
                    }
                }
                ResumeThread(h);
                if(!chain.empty())++samples_[{id,std::move(chain)}];
            }
        }
        for(const auto& [id,h]:handles)CloseHandle(h);timeEndPeriod(1);
    }
    std::uint64_t base_;std::atomic<bool> quit_{false},enabled_{false};
    std::map<std::pair<DWORD,std::vector<std::uint64_t>>,std::uint64_t> samples_;
    std::thread thread_;
};
}
