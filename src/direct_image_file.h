#pragma once
#include <filesystem>
#include <span>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <cstdint>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#endif
namespace eii::host {
// Exclusive writer lease. Caller supplies the original complete bytes; opening
// never truncates. Persist only changed ranges, then flush before returning.
class DirectImageFile {
public:
 DirectImageFile(const std::filesystem::path& path,std::span<const std::uint8_t> expected):shadow_(expected.begin(),expected.end()){
#ifdef _WIN32
  file_=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file_==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open image for exclusive direct writes (read-only or in use).");
#else
  file_=::open(path.c_str(),O_RDWR);if(file_<0)throw std::runtime_error("Cannot open image for direct writes.");
  if(::flock(file_,LOCK_EX|LOCK_NB)){closeFile();throw std::runtime_error("Image already has a direct writer.");}
#endif
  try {std::vector<std::uint8_t> actual(expected.size()+1);std::size_t done=0;
   while(done<actual.size()){
#ifdef _WIN32
    DWORD n=0;if(!ReadFile(file_,actual.data()+done,DWORD((std::min)(actual.size()-done,std::size_t(1u<<20))),&n,nullptr))throw std::runtime_error("Direct image read failed.");
#else
    auto n=::read(file_,actual.data()+done,actual.size()-done);if(n<0&&errno==EINTR)continue;if(n<0)throw std::runtime_error("Direct image read failed.");
#endif
    if(!n)break;
    done+=std::size_t(n);
   }
   if(done!=expected.size()||!std::equal(expected.begin(),expected.end(),actual.begin()))throw std::runtime_error("Image changed since mount; direct writer was not enabled.");
  }catch(...){closeFile();throw;}
 }
 ~DirectImageFile(){closeFile();}
 DirectImageFile(const DirectImageFile&)=delete;DirectImageFile& operator=(const DirectImageFile&)=delete;
 void persist(std::span<const std::uint8_t> bytes){
  if(bytes.size()!=shadow_.size())throw std::runtime_error("Direct image size change refused.");
#ifdef _WIN32
  LARGE_INTEGER size;if(!GetFileSizeEx(file_,&size)||size.QuadPart!=static_cast<LONGLONG>(shadow_.size()))throw std::runtime_error("Direct image size changed externally.");
#else
  struct stat st{};if(::fstat(file_,&st)||st.st_size!=static_cast<off_t>(shadow_.size()))throw std::runtime_error("Direct image size changed externally.");
#endif
  std::size_t p=0;
  while(p<bytes.size()){
   const auto first=p;p=(std::min)(p+512,bytes.size());
   if(std::equal(bytes.begin()+first,bytes.begin()+p,shadow_.begin()+first))continue;
   auto at=first;
   while(at<p){
#ifdef _WIN32
    LARGE_INTEGER offset;offset.QuadPart=static_cast<LONGLONG>(at);
    if(!SetFilePointerEx(file_,offset,nullptr,FILE_BEGIN))throw std::runtime_error("Direct image seek failed.");
    DWORD n=0;if(!WriteFile(file_,bytes.data()+at,DWORD((std::min)(p-at,std::size_t(1u<<20))),&n,nullptr)||!n)throw std::runtime_error("Direct image write failed; image may contain a partial write.");
#else
    const auto n=::pwrite(file_,bytes.data()+at,p-at,at);if(n<0&&errno==EINTR)continue;if(n<=0)throw std::runtime_error("Direct image write failed; image may contain a partial write.");
#endif
    at+=std::size_t(n);
   }
  }
#ifdef _WIN32
  if(!FlushFileBuffers(file_))throw std::runtime_error("Direct image flush failed; durability is unconfirmed.");
#else
  if(::fsync(file_))throw std::runtime_error("Direct image flush failed; durability is unconfirmed.");
#endif
  std::copy(bytes.begin(),bytes.end(),shadow_.begin());
 }
private:
 std::vector<std::uint8_t> shadow_;
#ifdef _WIN32
 HANDLE file_=INVALID_HANDLE_VALUE;
 void closeFile()noexcept{if(file_!=INVALID_HANDLE_VALUE){CloseHandle(file_);file_=INVALID_HANDLE_VALUE;}}
#else
 int file_=-1;void closeFile()noexcept{if(file_>=0){::close(file_);file_=-1;}}
#endif
};
}
