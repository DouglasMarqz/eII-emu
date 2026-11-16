#define NOMINMAX
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <sstream>
#include <filesystem>
namespace eii_profile {
const GUID perfInfo={0xce1dbfb4,0x137e,0x4da6,{0x87,0xb0,0x3f,0x59,0xaa,0x10,0x2c,0xbc}};
std::set<ULONG> selected;
std::map<std::pair<ULONG,std::uint64_t>,std::uint64_t> counts;
std::uint64_t seen=0,decoded=0,failed=0,matched=0;bool callbackFailed=false;
bool property(EVENT_RECORD* e,const wchar_t* name,std::uint64_t& value){
 PROPERTY_DATA_DESCRIPTOR d{};d.PropertyName=reinterpret_cast<ULONGLONG>(name);d.ArrayIndex=ULONG_MAX;
 ULONG size=0; if(TdhGetPropertySize(e,0,nullptr,1,&d,&size)!=ERROR_SUCCESS||(size!=4&&size!=8))return false;
 value=0;return TdhGetProperty(e,0,nullptr,1,&d,size,reinterpret_cast<BYTE*>(&value))==ERROR_SUCCESS;
}
void WINAPI event(EVENT_RECORD* e)noexcept{
 if(!IsEqualGUID(e->EventHeader.ProviderId,perfInfo)||e->EventHeader.EventDescriptor.Opcode!=46)return;
 ++seen;std::uint64_t tid=0,ip=0;
 if(!property(e,L"ThreadId",tid)){++failed;return;}
 ++decoded;if(!selected.count(static_cast<ULONG>(tid)))return;
 if(!property(e,L"InstructionPointer",ip)){++failed;return;}
 try{++counts[{static_cast<ULONG>(tid),ip}];++matched;}catch(...){callbackFailed=true;}
}
}
int wmain(int argc,wchar_t** argv){
 using namespace eii_profile;
 if(argc!=4){std::cerr<<"Usage: etl_samples.exe trace.etl output-folder tid,tid\n";return 2;}
 std::wstringstream ids(argv[3]);std::wstring token;
 try{while(std::getline(ids,token,L',')){const auto id=std::stoul(token);if(!id||id>ULONG_MAX)return 2;selected.insert(static_cast<ULONG>(id));}}catch(...){return 2;}
 if(selected.empty())return 2;
 EVENT_TRACE_LOGFILEW log{};log.LogFileName=argv[1];log.ProcessTraceMode=PROCESS_TRACE_MODE_EVENT_RECORD;log.EventRecordCallback=event;
 const auto trace=OpenTraceW(&log);if(trace==INVALID_PROCESSTRACE_HANDLE){std::cerr<<"OpenTrace error "<<GetLastError()<<'\n';return 3;}
 auto handle=trace;const auto status=ProcessTrace(&handle,1,nullptr,nullptr);CloseTrace(trace);
 const std::filesystem::path out(argv[2]);
 std::ofstream csv(out/L"samples.csv");csv<<"thread_id,instruction_pointer_hex,sample_count\n";
 for(const auto& [key,count]:counts)csv<<key.first<<",0x"<<std::hex<<key.second<<std::dec<<','<<count<<'\n';
 std::ofstream summary(out/L"decoder.txt");summary<<"process_trace_status="<<status<<"\nperfinfo_sample_events="<<seen<<"\ndecoded_thread_ids="<<decoded<<"\nproperty_failures="<<failed<<"\nselected_thread_samples="<<matched<<"\nevents_lost="<<log.EventsLost<<"\nheader_events_lost="<<log.LogfileHeader.EventsLost<<"\ncallback_allocation_failure="<<callbackFailed<<"\ncounts_are_sampled_self_IP_not_inclusive_stacks=1\n";
 std::cout<<"Selected-thread CPU samples: "<<matched<<"; property errors: "<<failed<<'\n';
 if(!csv||!summary)return 4;
 return status==ERROR_SUCCESS&&matched&&!callbackFailed?0:5;
}
