#pragma once
#include "floppy_hfe.h"
#include "direct_image_file.h"
#include "hdd_image_file.h"
#include <cctype>
#include <memory>
namespace eii::host {
class FloppyImageBacking {
public:
 std::filesystem::path path;bool direct=false,writable=true,dirty=false,imported=false,bankImport=false;
 // .EII bank file = the EMUIIFD payload from byte 78848 on (the area after the OS);
 // verified byte-exact against factory disks. The OS area comes from another OS 3.1 disk.
 static constexpr std::size_t bankOffset=78848;
 std::shared_ptr<EmuiiImage> image;
 std::unique_ptr<HfeImage> hfe;
 std::shared_ptr<DirectImageFile> lease;
 FloppyImageBacking(std::filesystem::path source,bool directWrites,std::shared_ptr<DirectImageFile> reuse={},std::span<const std::uint8_t> osArea={}):path(std::move(source)){
  std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f||f.tellg()>32*1024*1024||f.tellg()<0)throw std::runtime_error("Cannot read supported floppy image (maximum32 MiB).");
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(f.tellg()));f.seekg(0);if(!f.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("Floppy image read failed.");f.close();
  auto ext=path.extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char v){return char(std::tolower(v));});
  if(bytes.size()>=8&&std::equal(bytes.begin(),bytes.begin()+8,"HXCPICFE")){hfe=std::make_unique<HfeImage>(std::move(bytes));image=std::make_shared<EmuiiImage>(hfe->extractEmuii());}
  else if(ext==".eii"){
   if(bytes.empty()||bytes.size()>EmuiiImage::imageBytes-bankOffset)throw std::runtime_error("EII bank import requires at most 494592 bytes.");
   std::vector<std::uint8_t> floppy(EmuiiImage::imageBytes);
   if(osArea.size()>=bankOffset)std::copy_n(osArea.begin(),bankOffset,floppy.begin());
   std::copy(bytes.begin(),bytes.end(),floppy.begin()+bankOffset);image=std::make_shared<EmuiiImage>(std::move(floppy));imported=bankImport=true;writable=false;
  }
  else if(ext==".e2o"){
   if(bytes.size()!=72704)throw std::runtime_error("HD OS E2O import requires72704 bytes.");
   std::vector<std::uint8_t> floppy(EmuiiImage::imageBytes);std::copy(bytes.begin(),bytes.end(),floppy.begin());image=std::make_shared<EmuiiImage>(std::move(floppy));imported=true;writable=false;
  }else image=std::make_shared<EmuiiImage>(std::move(bytes));
  if(directWrites&&writable){lease=reuse?std::move(reuse):std::make_shared<DirectImageFile>(path,serialized());direct=true;}
 }
 std::span<const std::uint8_t> serialized()const noexcept{return hfe?hfe->bytes():image->bytes();}
 bool trackWritten(unsigned cylinder,unsigned side,std::span<const std::uint8_t> data){
  dirty=true;if(hfe)hfe->replaceSector(cylinder,side,data);
  if(direct){lease->persist(serialized());dirty=false;return true;}return false;
 }
 void saveCopy(const std::filesystem::path& output){saveNewImage(output,serialized());dirty=false;}
};
}
