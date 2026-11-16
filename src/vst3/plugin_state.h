#pragma once
// Persistent VST3 configuration. Only references and settings are stored:
// never ROM bytes, disk images, sample RAM or machine RAM.
#include "runtime_audio.h"
#include "filter_calibration.h"
#include "vst3/post_output_gain.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>

namespace eii::vst {
struct PluginSettings {
    std::filesystem::path romFolder,floppy,hdd;
    std::filesystem::path osImage; // OS disk whose OS area is used for .EII bank imports (empty = automatic)
    std::filesystem::path library; // user media root: ROMS, DISKS, HDD subfolders (file dialogs start there)
    bool directImageWrites=false;
    analog::RuntimeAudioMode audioMode=analog::RuntimeAudioMode::BasicVoice; // standalone default
    bool nativeAudio=false,reconstruction=false;
    bool fastPanel=false; // custom scanner averaging bypass, applied only on boot
    bool midiWheels=false,noteOffZero=false;
    bool autoBoot=true;
    bool sliderThrottle=false; // comfort option, not hardware behaviour: sliders A-D send a new value only when the scanner->main queue is nearly empty (latest value wins)
    bool analogDrift=false; // ESTIMATED die-temperature drift of the voice filters (thermal_drift.h); not original-measured, off = exact
    bool bootFloppy=false; // boot order with HDD and floppy both set: false = HDD first (the floppy goes in once the OS runs), true = floppy first (the HD PROM 2.3 own order); next boot
    bool memory1mb=true; // EII+ / EII+ HD: 1 MB as two 512 KB banks (docs/MEMORY_1MB.md); next boot
    analog::FilterCalibration calibration,legacyCalibration,eiiiCalibration,ssm2Calibration; // independent model profiles, nominal defaults
    std::array<double,6> controls{0,0,0,0,2.5,0}; // sliders A..D, left/right wheel (volts)
    double postOutputDb=0; // final host output only, outside emulated signal chain
    double volume=1,inputGain=1,sampleInputTrimDb=0; // host input calibration before +INPUT.V
    int recallProgram=-1; // last host program change: (status<<8)|program, replayed after boot
    int marginMs=30;      // realtime safety margin added to the reported latency (10/20/30/40)
    int editorWidth=0,editorHeight=0; // last editor size in pixels (0 = default)
    bool operator==(const PluginSettings&)const=default;
};

inline std::string pathText(const std::filesystem::path& p){auto u=p.u8string();return std::string(u.begin(),u.end());}
inline std::filesystem::path textPath(const std::string& s){return std::filesystem::path(std::u8string(s.begin(),s.end()));}
inline unsigned audioModeIndex(analog::RuntimeAudioMode m){return m==analog::RuntimeAudioMode::NonlinearChip?1:m==analog::RuntimeAudioMode::NonlinearChip192?2:m==analog::RuntimeAudioMode::LegacyQ?3:m==analog::RuntimeAudioMode::EiiiCustom?4:m==analog::RuntimeAudioMode::Ssm2Chip?5:m==analog::RuntimeAudioMode::Ssm2Chip192?6:0;}
inline analog::RuntimeAudioMode audioModeFromIndex(unsigned i){return i==1?analog::RuntimeAudioMode::NonlinearChip:i==2?analog::RuntimeAudioMode::NonlinearChip192:i==3?analog::RuntimeAudioMode::LegacyQ:i==4?analog::RuntimeAudioMode::EiiiCustom:i==5?analog::RuntimeAudioMode::Ssm2Chip:i==6?analog::RuntimeAudioMode::Ssm2Chip192:analog::RuntimeAudioMode::BasicVoice;}

inline const char* audioModelName(analog::RuntimeAudioMode mode){
    switch(mode){
    case analog::RuntimeAudioMode::EiiiCustom:return "EIII CEM3387 VCF - custom / EII VCA (96k)";
    case analog::RuntimeAudioMode::Ssm2Chip:return "SSM2045 model 2 - block-diagram VCF / VCA (96k)";
    case analog::RuntimeAudioMode::Ssm2Chip192:return "SSM2045 model 2 - block-diagram VCF / VCA (192k)";
    case analog::RuntimeAudioMode::LegacyQ:return "Legacy + Q - fast VCF / linear VCA (96k)";
    case analog::RuntimeAudioMode::NonlinearChip:return "SSM2045 - nonlinear VCF / VCA (96k)";
    case analog::RuntimeAudioMode::NonlinearChip192:return "SSM2045 - nonlinear VCF / VCA (192k)";
    default:return "Historical Legacy - zero-Q VCF / linear VCA";
    }
}
inline const analog::FilterCalibration& activeCalibration(const PluginSettings& s){return analog::ssm2AudioMode(s.audioMode)?s.ssm2Calibration:s.audioMode==analog::RuntimeAudioMode::EiiiCustom?s.eiiiCalibration:s.audioMode==analog::RuntimeAudioMode::LegacyQ?s.legacyCalibration:s.calibration;}
inline constexpr const char* stateMagic="EIIVST3 1";
inline std::string serializeSettings(const PluginSettings& s){
    std::ostringstream o;o.imbue(std::locale::classic());o.precision(17);
    o<<stateMagic<<"\n";
    o<<"library="<<pathText(s.library)<<"\nosimage="<<pathText(s.osImage)<<"\n";
    o<<"rom="<<pathText(s.romFolder)<<"\nfloppy="<<pathText(s.floppy)<<"\nhdd="<<pathText(s.hdd)<<"\n";
    o<<"direct="<<s.directImageWrites<<"\nmodel="<<audioModeIndex(s.audioMode)<<"\nnative="<<s.nativeAudio<<"\ns3528="<<s.reconstruction<<"\n";
    o<<"fastpanel="<<s.fastPanel<<"\nwheels="<<s.midiWheels<<"\nnoteoffzero="<<s.noteOffZero<<"\nautoboot="<<s.autoBoot<<"\nmemory1mb="<<s.memory1mb<<"\nsliderthrottle="<<s.sliderThrottle<<"\nbootfloppy="<<s.bootFloppy<<"\nanalogdrift="<<s.analogDrift<<"\n";
    o<<"controls=";for(unsigned i=0;i<6;++i)o<<(i?",":"")<<s.controls[i];o<<"\n";
    o<<"postoutputdb="<<s.postOutputDb<<"\nvolume="<<s.volume<<"\nsampleinputtrimdb="<<s.sampleInputTrimDb<<"\ninputgain="<<s.inputGain<<"\nprogram="<<s.recallProgram<<"\nmargin="<<s.marginMs<<"\neditor="<<s.editorWidth<<"x"<<s.editorHeight<<"\n";
    o<<"legacycalibration=";for(unsigned i=0;i<s.legacyCalibration.values.size();++i)o<<(i?",":"")<<s.legacyCalibration.values[i];o<<"\n";
    o<<"eiiicalibration=";for(unsigned i=0;i<s.eiiiCalibration.values.size();++i)o<<(i?",":"")<<s.eiiiCalibration.values[i];o<<"\n";
    o<<"ssm2calibration=";for(unsigned i=0;i<s.ssm2Calibration.values.size();++i)o<<(i?",":"")<<s.ssm2Calibration.values[i];o<<"\n";
    o<<"levelmodel=2\n"; // common level model (level_model.h); absent = older state, migrated on load
    o<<"calibration:\n"<<s.calibration.serialize();
    return o.str();
}
// Throws on a foreign/corrupt blob; unknown keys are ignored for forward compatibility.
// Per-user defaults for NEW plug-in instances (a project state still replaces
// everything). Written on every settings change and when the plug-in closes.
inline std::filesystem::path pluginDefaultsFile(){
    std::filesystem::path base;
#ifdef _WIN32
    if(const char* a=std::getenv("LOCALAPPDATA"))base=a;
#endif
    if(base.empty())base=std::filesystem::temp_directory_path();
    return base/"EmulatorII-VST3"/"plugin-defaults.txt";
}
inline PluginSettings parseSettings(const std::string& text);
inline bool loadPluginDefaults(PluginSettings& out){
    std::FILE* f=nullptr;
#ifdef _WIN32
    f=_wfopen(pluginDefaultsFile().c_str(),L"rb");
#else
    f=std::fopen(pluginDefaultsFile().c_str(),"rb");
#endif
    if(!f)return false;std::string text;char b[4096];std::size_t n;
    while((n=std::fread(b,1,sizeof b,f))>0&&text.size()<(1u<<20))text.append(b,n);std::fclose(f);
    try{out=parseSettings(text);return true;}catch(...){return false;}
}
inline bool savePluginDefaults(const PluginSettings& s){
    const auto file=pluginDefaultsFile();std::error_code e;std::filesystem::create_directories(file.parent_path(),e);
    auto tmp=file;tmp+=".tmp";const auto text=serializeSettings(s);
    std::FILE* f=nullptr;
#ifdef _WIN32
    f=_wfopen(tmp.c_str(),L"wb");
#else
    f=std::fopen(tmp.c_str(),"wb");
#endif
    if(!f)return false;const bool ok=std::fwrite(text.data(),1,text.size(),f)==text.size();if(std::fclose(f)!=0||!ok)return false;
    std::filesystem::rename(tmp,file,e);return !e;
}
inline PluginSettings parseSettings(const std::string& text){
    std::istringstream in(text);in.imbue(std::locale::classic());std::string line;
    if(!std::getline(in,line)||line!=stateMagic)throw std::runtime_error("Not an Emulator II VST3 state");
    PluginSettings s;s.memory1mb=false; // states older than the option: the 512 KB they ran with
    bool levelModel2=false,hasCalibration=false,hasLegacy=false,hasEiii=false,hasSsm2=false;
    auto flag=[](const std::string& v){return v=="1";};
    auto number=[](const std::string& v){std::istringstream n(v);n.imbue(std::locale::classic());double d=0;n>>d;if(!n||!std::isfinite(d))throw std::runtime_error("Bad number in state");return d;};
    while(std::getline(in,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line=="calibration:"){std::string rest((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
            if(!rest.empty()){s.calibration=analog::FilterCalibration::parse(rest);hasCalibration=true;}break;}
        const auto eq=line.find('=');if(eq==std::string::npos)continue;
        const auto key=line.substr(0,eq),v=line.substr(eq+1);
        if(key=="library")s.library=textPath(v);else if(key=="osimage")s.osImage=textPath(v);else if(key=="rom")s.romFolder=textPath(v);else if(key=="floppy")s.floppy=textPath(v);else if(key=="hdd")s.hdd=textPath(v);
        else if(key=="direct")s.directImageWrites=flag(v);else if(key=="model")s.audioMode=audioModeFromIndex(unsigned(number(v)));
        else if(key=="native")s.nativeAudio=flag(v);else if(key=="s3528")s.reconstruction=flag(v);
        else if(key=="fastpanel")s.fastPanel=flag(v);else if(key=="levelmodel")levelModel2=number(v)==2;
        else if(key=="wheels")s.midiWheels=flag(v);else if(key=="noteoffzero")s.noteOffZero=flag(v);else if(key=="autoboot")s.autoBoot=flag(v);else if(key=="memory1mb")s.memory1mb=flag(v);else if(key=="sliderthrottle")s.sliderThrottle=flag(v);else if(key=="bootfloppy")s.bootFloppy=flag(v);else if(key=="analogdrift")s.analogDrift=flag(v);
        else if(key=="legacycalibration"||key=="eiiicalibration"||key=="ssm2calibration"){
            auto& profile=key=="legacycalibration"?s.legacyCalibration:key=="eiiicalibration"?s.eiiiCalibration:s.ssm2Calibration;
            if(key=="legacycalibration")hasLegacy=true;else if(key=="eiiicalibration")hasEiii=true;else hasSsm2=true;
            std::istringstream values(v);std::string item;unsigned i=0;
            while(std::getline(values,item,',')){if(i>=profile.values.size())throw std::runtime_error("Too many Legacy Q parameters");profile.values[i++]=number(item);}
            // Older states stored the 0.3.14 (base) or 0.3.19 (CEM) parameter count; newer fields keep defaults.
            if(i!=profile.values.size()&&i!=analog::baseFilterParameterCount&&i!=analog::cemFilterParameterCount)throw std::runtime_error("Incomplete Legacy Q parameters");
        }
        else if(key=="controls"){std::istringstream c(v);std::string item;for(unsigned i=0;i<6&&std::getline(c,item,',');++i)s.controls[i]=std::clamp(number(item),0.0,5.0);}
        else if(key=="volume")s.volume=std::clamp(number(v),0.0,1.0);else if(key=="inputgain")s.inputGain=std::clamp(number(v),0.0,1.0);
        else if(key=="sampleinputtrimdb")s.sampleInputTrimDb=std::clamp(number(v),-24.0,24.0);
        else if(key=="postoutputdb")s.postOutputDb=std::clamp(number(v),0.0,postOutputMaxDb);
        else if(key=="program")s.recallProgram=int(number(v));
        else if(key=="margin")s.marginMs=std::clamp(int(number(v)),5,100);
        else if(key=="editor"){const auto x=v.find('x');if(x!=std::string::npos){s.editorWidth=std::clamp(int(number(v.substr(0,x))),0,16384);s.editorHeight=std::clamp(int(number(v.substr(x+1))),0,16384);}}
    }
    // Older states: deliberate native trims keep their loudness (level_model.h).
    if(!levelModel2){if(hasCalibration)analog::migrateLevelModel(s.calibration);if(hasLegacy)analog::migrateLevelModel(s.legacyCalibration);if(hasEiii)analog::migrateLevelModel(s.eiiiCalibration);if(hasSsm2)analog::migrateLevelModel(s.ssm2Calibration);}
    s.calibration.validate();s.legacyCalibration.validate();s.eiiiCalibration.validate();s.ssm2Calibration.validate();return s;
}
}
