#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <time.h>
#endif
namespace eii::host {
inline void nameCurrentThread(const wchar_t* name)noexcept {
#ifdef _WIN32
 (void)SetThreadDescription(GetCurrentThread(),name);
#else
 (void)name;
#endif
}
// Current owner thread only; sampled once per speed window, never per CPU tick.
inline double threadCpuSeconds()noexcept {
#ifdef _WIN32
 FILETIME created,exited,kernel,user;
 if(!GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user))return -1;
 auto value=[](FILETIME t){return (static_cast<unsigned long long>(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
 return double(value(kernel)+value(user))*1e-7;
#else
 timespec t{};if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t))return -1;
 return double(t.tv_sec)+double(t.tv_nsec)*1e-9;
#endif
}
}
