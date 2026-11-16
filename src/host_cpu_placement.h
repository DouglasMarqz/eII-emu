#pragma once
#include <cstdlib>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <vector>
#endif
namespace eii::host {
enum class CpuRole { Machine, Dsp };
// Soft Windows CPU Sets for the two busy owners: the machine thread and the
// SSM2045 DSP worker get two different physical cores in the same L3 group.
// Measured on a Ryzen 9 5900X (0.71): with default scheduling the two threads
// shared one core's SMT siblings, which slowed the machine thread by about 22%
// in SSM96k and more in SSM192k. Not hard affinity: no process mask, priority
// or registry change; other threads stay unrestricted. The emulated machine
// is unchanged. EII_CPU_PLACEMENT=0 disables it.
inline bool placeCurrentThread(CpuRole role) noexcept {
#ifdef _WIN32
  if (const char *e = std::getenv("EII_CPU_PLACEMENT"); e && e[0] == '0') return false;
  try {
    // Loaded at run time: the API is Windows 10+ and hidden by older SDK targets.
    using GetInfo = BOOL(WINAPI *)(PSYSTEM_CPU_SET_INFORMATION, ULONG, PULONG, HANDLE, ULONG);
    using SetSets = BOOL(WINAPI *)(HANDLE, const ULONG *, ULONG);
    const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    if (!kernel) return false;
    const auto getInfo = reinterpret_cast<GetInfo>(reinterpret_cast<void *>(GetProcAddress(kernel, "GetSystemCpuSetInformation")));
    const auto setSets = reinterpret_cast<SetSets>(reinterpret_cast<void *>(GetProcAddress(kernel, "SetThreadSelectedCpuSets")));
    if (!getInfo || !setSets) return false;
    ULONG length = 0;
    const HANDLE process = GetCurrentProcess();
    getInfo(nullptr, 0, &length, process, 0);
    if (!length) return false;
    std::vector<unsigned char> buffer(length);
    auto *first = reinterpret_cast<SYSTEM_CPU_SET_INFORMATION *>(buffer.data());
    if (!getInfo(first, length, &length, process, 0)) return false;
    struct Core { WORD group; BYTE core, llc; std::vector<ULONG> ids; };
    std::vector<Core> cores;
    for (ULONG offset = 0; offset < length;) {
      const auto *entry = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION *>(buffer.data() + offset);
      if (!entry->Size) break;
      if (entry->Type == CpuSetInformation) {
        const auto &s = entry->CpuSet;
        auto it = std::find_if(cores.begin(), cores.end(), [&](const Core &c) { return c.group == s.Group && c.core == s.CoreIndex; });
        if (it == cores.end()) { cores.push_back({s.Group, s.CoreIndex, s.LastLevelCacheIndex, {}}); it = cores.end() - 1; }
        it->ids.push_back(s.Id);
      }
      offset += entry->Size;
    }
    // Largest L3 group; skip its first core (interrupt/system default) when possible.
    BYTE bestLlc = 0; std::size_t bestCount = 0;
    for (const auto &c : cores) {
      const auto n = std::size_t(std::count_if(cores.begin(), cores.end(), [&](const Core &o) { return o.llc == c.llc && o.group == c.group; }));
      if (n > bestCount) { bestCount = n; bestLlc = c.llc; }
    }
    std::vector<const Core *> group;
    for (const auto &c : cores) if (c.llc == bestLlc) group.push_back(&c);
    std::sort(group.begin(), group.end(), [](const Core *a, const Core *b) { return a->group != b->group ? a->group < b->group : a->core < b->core; });
    if (group.size() < 2) return false;
    const std::size_t base = group.size() >= 3 ? 1 : 0;
    const Core &pick = *group[base + (role == CpuRole::Dsp ? 1 : 0)];
    return setSets(GetCurrentThread(), pick.ids.data(), ULONG(pick.ids.size())) != 0;
  } catch (...) {
    return false;
  }
#else
  (void)role;
  return false;
#endif
}
}
