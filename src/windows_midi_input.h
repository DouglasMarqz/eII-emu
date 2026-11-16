#pragma once
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#include "midi_serial.h"
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include <stdexcept>
namespace eii::host {
class WindowsMidiInput {
public:
    struct Device {UINT number=0;std::wstring id,name;};
    std::vector<Device> devices;
    std::wstring selectedId;
    std::atomic<std::uint64_t> messages{0},dropped{0},unsupported{0},errors{0},moreData{0};
    explicit WindowsMidiInput(std::function<bool(std::span<const std::uint8_t>)> sink):sink_(std::move(sink)) {}
    ~WindowsMidiInput(){close();}
    WindowsMidiInput(const WindowsMidiInput&)=delete;
    WindowsMidiInput& operator=(const WindowsMidiInput&)=delete;
    void refresh() {
        devices.clear();
        for(UINT i=0;i<midiInGetNumDevs();++i) {
            MIDIINCAPSW caps{};
            if(midiInGetDevCapsW(i,&caps,sizeof(caps))!=MMSYSERR_NOERROR)continue;
            const std::wstring name=caps.szPname;
            unsigned duplicate=0;for(const auto& d:devices)if(d.name==name)++duplicate;
            devices.push_back({i,std::to_wstring(caps.wMid)+L":"+std::to_wstring(caps.wPid)+L":"+name+L":"+std::to_wstring(duplicate),name});
        }
    }
    void close() noexcept {
        if(handle_){midiInStop(handle_);midiInReset(handle_);midiInClose(handle_);handle_=nullptr;}
    }
    void select(const std::wstring& id) {
        close();selectedId.clear();
        if(id.empty())return;
        const Device* chosen=nullptr;for(const auto& d:devices)if(d.id==id)chosen=&d;
        if(!chosen)throw std::runtime_error("MIDI input is unavailable; refresh ports");
        check(midiInOpen(&handle_,chosen->number,reinterpret_cast<DWORD_PTR>(&callback),
                         reinterpret_cast<DWORD_PTR>(this),CALLBACK_FUNCTION|MIDI_IO_STATUS),"Open MIDI input");
        const auto result=midiInStart(handle_);
        if(result!=MMSYSERR_NOERROR){close();check(result,"Start MIDI input");}
        selectedId=id;
    }
private:
    HMIDIIN handle_=nullptr;
    std::function<bool(std::span<const std::uint8_t>)> sink_;
    static void check(MMRESULT result,const char* what) {
        if(result!=MMSYSERR_NOERROR)throw std::runtime_error(std::string(what)+" (WinMM error "+std::to_string(result)+")");
    }
    static void CALLBACK callback(HMIDIIN,UINT message,DWORD_PTR instance,DWORD_PTR data,DWORD_PTR) noexcept {
        auto& self=*reinterpret_cast<WindowsMidiInput*>(instance);
        if(message==MIM_ERROR || message==MIM_LONGERROR){++self.errors;return;}
        if(message!=MIM_DATA && message!=MIM_MOREDATA)return;
        if(message==MIM_MOREDATA)++self.moreData;
        const std::array<std::uint8_t,3> bytes{std::uint8_t(data),std::uint8_t(data>>8),std::uint8_t(data>>16)};
        const auto size=midiMessageLength(bytes[0]);
        if(!size){++self.unsupported;return;}
        ++self.messages;
        try {if(!self.sink_(std::span(bytes.data(),size)))++self.dropped;}catch(...){++self.dropped;}
    }
};
}
#endif
