#pragma once
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <system_error>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace eii::host {
inline constexpr std::size_t hddImageBytes=613u*4u*18u*512u;
inline std::vector<std::uint8_t> loadHddImage(const std::filesystem::path& path) {
    std::ifstream f(path,std::ios::binary|std::ios::ate);
    if(!f || f.tellg()!=std::streamoff(hddImageBytes))
        throw std::runtime_error("HDD image must be a raw 20 MB EII image (22597632 bytes).");
    std::vector<std::uint8_t> bytes(hddImageBytes);f.seekg(0);
    if(!f.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("Could not read complete HDD image.");
    return bytes;
}
// Publish a complete, flushed sibling file, without ever replacing an existing
// path. The mounted source image is never opened for writing.
inline void saveNewImage(const std::filesystem::path& path,std::span<const std::uint8_t> bytes) {
    static std::atomic<unsigned long> sequence{0};
    std::filesystem::path temp;
#ifdef _WIN32
    HANDLE file=INVALID_HANDLE_VALUE;
    for(unsigned i=0;i<64;++i){temp=path;temp+=L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++sequence);
        file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file!=INVALID_HANDLE_VALUE)break;
        if(GetLastError()!=ERROR_FILE_EXISTS)throw std::runtime_error("Cannot create HDD temporary file.");}
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot allocate HDD temporary file.");
    try {
        std::size_t done=0;
        while(done<bytes.size()){DWORD written=0;const auto n=DWORD((std::min)(bytes.size()-done,std::size_t(1u<<20)));
            if(!WriteFile(file,bytes.data()+done,n,&written,nullptr)||!written)throw std::runtime_error("HDD write failed; original image preserved.");
            done+=written;}
        if(!FlushFileBuffers(file))throw std::runtime_error("HDD flush failed; original image preserved.");
        if(!CloseHandle(file)){file=INVALID_HANDLE_VALUE;throw std::runtime_error("HDD close failed.");}file=INVALID_HANDLE_VALUE;
        if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("HDD copy not saved. Choose a new filename; existing files are never replaced.");
    }catch(...){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);DeleteFileW(temp.c_str());throw;}
#else
    int file=-1;
    for(unsigned i=0;i<64;++i){temp=path;temp+=".tmp-"+std::to_string(getpid())+"-"+std::to_string(++sequence);
        file=::open(temp.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);if(file>=0)break;
        if(errno!=EEXIST)throw std::system_error(errno,std::generic_category(),"HDD temporary file");}
    if(file<0)throw std::runtime_error("Cannot allocate HDD temporary file.");
    try {
        std::size_t done=0;
        while(done<bytes.size()){auto n=::write(file,bytes.data()+done,bytes.size()-done);
            if(n<0&&errno==EINTR)continue;
            if(n<=0)throw std::runtime_error("HDD write failed; original image preserved.");
            done+=std::size_t(n);}
        if(::fsync(file))throw std::runtime_error("HDD flush failed.");
        const auto closed=::close(file);file=-1;if(closed)throw std::runtime_error("HDD close failed.");
        if(::link(temp.c_str(),path.c_str()))throw std::runtime_error("HDD copy not saved. Choose a new filename; existing files are never replaced.");
        ::unlink(temp.c_str());
        auto parent=path.parent_path();if(parent.empty())parent=".";
        const int directory=::open(parent.c_str(),O_RDONLY|O_DIRECTORY);
        if(directory<0)throw std::runtime_error("HDD copy created but directory durability could not be confirmed.");
        const auto flushed=::fsync(directory);::close(directory);
        if(flushed)throw std::runtime_error("HDD copy created but directory flush failed.");
    }catch(...){if(file>=0)::close(file);::unlink(temp.c_str());throw;}
#endif
}
inline void saveNewHddImage(const std::filesystem::path& path,std::span<const std::uint8_t> bytes){
    if(bytes.size()!=hddImageBytes)throw std::runtime_error("Invalid HDD image size.");
    saveNewImage(path,bytes);
}

}
