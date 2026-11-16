#pragma once
#ifdef _WIN32
#include "filter_service_profiles.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <optional>
namespace eii::host {
inline std::filesystem::path globalFilterServicePath(){
    const auto size=GetEnvironmentVariableW(L"LOCALAPPDATA",nullptr,0);
    if(!size)throw std::runtime_error("Local application data folder unavailable");
    std::wstring root(size,L'\0');const auto copied=GetEnvironmentVariableW(L"LOCALAPPDATA",root.data(),size);
    if(!copied||copied>=size)throw std::runtime_error("Local application data folder unavailable");root.resize(copied);
    return std::filesystem::path(root)/L"EmulatorII-Custom"/L"filter-service-global.txt";
}
inline std::optional<FilterServiceProfiles> loadGlobalFilterService(){
    const auto path=globalFilterServicePath();if(!std::filesystem::exists(path))return {};
    std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Could not open global Filter Service profile");
    std::string text(65537,'\0');in.read(text.data(),text.size());text.resize(std::size_t(in.gcount()));
    if(in.bad())throw std::runtime_error("Could not read global Filter Service profile");
    return FilterServiceProfiles::parse(text);
}
inline void saveGlobalFilterService(const FilterServiceProfiles& profiles){
    const auto text=profiles.serialize();const auto path=globalFilterServicePath();
    std::filesystem::create_directories(path.parent_path());
    wchar_t temp[MAX_PATH];if(!GetTempFileNameW(path.parent_path().c_str(),L"EII",0,temp))throw std::runtime_error("Could not create temporary global profile");
    HANDLE file=CreateFileW(temp,GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){DeleteFileW(temp);throw std::runtime_error("Could not open temporary global profile");}
    DWORD written=0;const bool ok=WriteFile(file,text.data(),DWORD(text.size()),&written,nullptr)&&written==text.size()&&FlushFileBuffers(file);
    CloseHandle(file);
    if(!ok||!MoveFileExW(temp,path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        DeleteFileW(temp);throw std::runtime_error("Could not save global Filter Service profile; previous file preserved");
    }
}
}
#endif
