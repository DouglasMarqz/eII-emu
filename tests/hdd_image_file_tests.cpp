#include "hdd_image_file.h"
#include <chrono>
#include <iostream>
#include <thread>
namespace fs=std::filesystem;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main()try{
    const auto root=fs::temp_directory_path()/("eii-hdd-files-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(fs::create_directory(root),"unique test directory");
    struct Cleanup {fs::path path;~Cleanup(){std::error_code e;fs::remove_all(path,e);}} cleanup{root};
    std::vector<std::uint8_t> bytes(eii::host::hddImageBytes);
    for(std::size_t i=0;i<bytes.size();++i)bytes[i]=std::uint8_t((i*17)^(i>>12));
    const auto original=root/fs::path(u8"HDD-árvíztűrő.img");
    eii::host::saveNewHddImage(original,bytes);check(eii::host::loadHddImage(original)==bytes,"unicode image roundtrip");
    auto other=bytes;other[2048]^=0xff;bool rejected=false;
    try{eii::host::saveNewHddImage(original,other);}catch(...){rejected=true;}
    check(rejected&&eii::host::loadHddImage(original)==bytes,"existing destination preserved");
    const auto raced=root/"race.img";std::atomic<unsigned> wins=0;
    auto writer=[&](const auto& image){try{eii::host::saveNewHddImage(raced,image);++wins;}catch(...) {}};
    std::thread first([&]{writer(bytes);}),second([&]{writer(other);});first.join();second.join();
    const auto result=eii::host::loadHddImage(raced);check(wins==1&&(result==bytes||result==other),"one complete winner under concurrent publication");
    rejected=false;try{eii::host::saveNewHddImage(root/"absent"/"bad.img",bytes);}catch(...){rejected=true;}
    check(rejected&&eii::host::loadHddImage(original)==bytes,"failed output preserves original");
    std::ofstream(root/"short.img",std::ios::binary).put(0);rejected=false;
    try{eii::host::loadHddImage(root/"short.img");}catch(...){rejected=true;}check(rejected,"truncated input rejected");
    for(const auto& entry:fs::directory_iterator(root))check(entry.path().filename().native().find(fs::path(".tmp-").native())==fs::path::string_type::npos,"temporary file cleanup");
    std::cout<<"PASS HDD file Unicode roundtrip, no-overwrite, concurrent complete publication, failure preservation, truncated input and cleanup\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
