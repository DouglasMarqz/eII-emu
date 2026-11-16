#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace eii::host {
#if defined(_WIN32) && defined(EII_VST3_BACKGROUND_AUDIO)
// Only plug-in-owned threads use this scope. Never change the host process QoS.
class PluginThreadQos {
public:
 PluginThreadQos()noexcept{
  const auto kernel=GetModuleHandleW(L"kernel32.dll");
  const auto get=reinterpret_cast<Get>(GetProcAddress(kernel,"GetThreadInformation"));
  set_=reinterpret_cast<Set>(GetProcAddress(kernel,"SetThreadInformation"));
  saved_.Version=THREAD_POWER_THROTTLING_CURRENT_VERSION;
  if(get&&set_&&get(GetCurrentThread(),ThreadPowerThrottling,&saved_,sizeof(saved_))){
   auto desired=saved_;desired.ControlMask|=THREAD_POWER_THROTTLING_EXECUTION_SPEED;
   desired.StateMask&=~THREAD_POWER_THROTTLING_EXECUTION_SPEED;
   changed_=set_(GetCurrentThread(),ThreadPowerThrottling,&desired,sizeof(desired))!=0;
  }
 }
 ~PluginThreadQos(){if(changed_)set_(GetCurrentThread(),ThreadPowerThrottling,&saved_,sizeof(saved_));}
 bool applied()const noexcept{return changed_;}
private:
 using Get=BOOL(WINAPI*)(HANDLE,THREAD_INFORMATION_CLASS,LPVOID,DWORD);
 using Set=BOOL(WINAPI*)(HANDLE,THREAD_INFORMATION_CLASS,LPVOID,DWORD);
 Set set_=nullptr;THREAD_POWER_THROTTLING_STATE saved_{};bool changed_=false;
};
#endif
// Scoped, best-effort Windows multimedia scheduling. No process-wide priority,
// registry change, affinity binding or spin wait; every acquired API is undone.
class AudioThreadSchedule {
public:
 explicit AudioThreadSchedule(bool timer=false)noexcept {
#ifdef _WIN32
  avrt_=LoadLibraryW(L"avrt.dll");
  if(avrt_){auto set=reinterpret_cast<Set>(GetProcAddress(avrt_,"AvSetMmThreadCharacteristicsW"));revert_=reinterpret_cast<Revert>(GetProcAddress(avrt_,"AvRevertMmThreadCharacteristics"));DWORD index=0;if(set&&revert_)task_=set(
#ifdef EII_VST3_BACKGROUND_AUDIO
  L"Pro Audio"
#else
  L"Audio"
#endif
  ,&index);}
  if(timer){winmm_=LoadLibraryW(L"winmm.dll");if(winmm_){auto begin=reinterpret_cast<Period>(GetProcAddress(winmm_,"timeBeginPeriod"));end_=reinterpret_cast<Period>(GetProcAddress(winmm_,"timeEndPeriod"));if(begin&&end_)timer_=begin(1)==0;}}
#else
  (void)timer;
#endif
 }
 ~AudioThreadSchedule(){
#ifdef _WIN32
  if(timer_)end_(1);if(task_)revert_(task_);if(winmm_)FreeLibrary(winmm_);if(avrt_)FreeLibrary(avrt_);
#endif
 }
 bool multimedia()const noexcept {
#ifdef _WIN32
  return task_!=nullptr;
#else
  return false;
#endif
 }
 bool highQos()const noexcept {
#if defined(_WIN32) && defined(EII_VST3_BACKGROUND_AUDIO)
  return qos_.applied();
#else
  return false;
#endif
 }
 bool timerResolution()const noexcept {
#ifdef _WIN32
  return timer_;
#else
  return false;
#endif
 }
 AudioThreadSchedule(const AudioThreadSchedule&)=delete;
 AudioThreadSchedule& operator=(const AudioThreadSchedule&)=delete;
private:
#if defined(_WIN32) && defined(EII_VST3_BACKGROUND_AUDIO)
 PluginThreadQos qos_;
#endif
#ifdef _WIN32
 using Set=HANDLE(WINAPI*)(LPCWSTR,LPDWORD);using Revert=BOOL(WINAPI*)(HANDLE);using Period=UINT(WINAPI*)(UINT);
 HMODULE avrt_=nullptr,winmm_=nullptr;HANDLE task_=nullptr;Revert revert_=nullptr;Period end_=nullptr;bool timer_=false;
#endif
};
}
