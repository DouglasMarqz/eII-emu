#pragma once
#ifdef _WIN32
#include <windows.h>
#include "filter_calibration.h"
#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace eii::host {
// Per-user UI preferences only. No emulated state or media contents are saved.
struct WindowsPanelPreferences {
    analog::FilterCalibration calibration;
    std::wstring calibrationError;
    std::wstring romFolder, disk, hddImage, audioId, midiId, inputId; // inputId used only when inputEnabled
    bool directImageWrites=false;
    bool liveSerial = false,reconstruction=false,nativeAudio=false;
    bool diagnostic = false, concreteStartup = true;
    bool midiWheels=false,noteOffZero=false;
    DWORD outputVolume=1000;
    DWORD inputEnabled=0,inputGain=1000,inputChannel=0; // sample input: no capture by default; GAIN pot 0..1000; 0 mix,1 left,2 right
    DWORD chipModel=0; //0legacy,1nonlinear96k,2nonlinear192k; stable legacy default.
    std::array<DWORD, 6> sliders{0, 0, 0, 0, 500, 0};

    void load() {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
            return;
        const auto extended=readString(key,L"FilterCalibrationV2"),legacy=readString(key,L"FilterCalibrationV1");
        if(!extended.empty())try{const std::string text(extended.begin(),extended.end());calibration=analog::FilterCalibration::parse(text);
            if(analog::FilterCalibration::preLevelModel(text))analog::migrateLevelModel(calibration);} // keep native-route loudness (level_model.h)
            catch(...){calibrationError=L"Saved extended calibration invalid; legacy/default values loaded.";}
        // Older executables may have edited V1 after V2 was saved. Merge their
        //54 chip values, retaining the native-only settings from V2.
        if(!legacy.empty())try{const auto old=analog::FilterCalibration::parse(std::string(legacy.begin(),legacy.end()));
            for(unsigned i=0;i<analog::legacyFilterParameterCount;++i)calibration.values[i]=old.values[i];}
            catch(...){calibrationError=L"Saved legacy calibration invalid; extended/default values retained.";}
        romFolder = readString(key, L"RomFolder");
        disk = readString(key, L"Disk");
        hddImage = readString(key, L"HddImage");
        directImageWrites=readNumber(key,L"DirectImageWrites",0)!=0;
        audioId = readString(key, L"AudioEndpoint");
        inputId = readString(key, L"SampleInputEndpoint");
        midiId = readString(key, L"MidiInput");
        midiWheels=readNumber(key,L"MidiPanelWheels",0)!=0;
        noteOffZero=readNumber(key,L"NoteOffZero",0)!=0;
        chipModel=std::min<DWORD>(2,readNumber(key,L"ChipModel",0));
        outputVolume=std::min<DWORD>(1000,readNumber(key,L"OutputVolume",1000));
        inputEnabled=std::min<DWORD>(1,readNumber(key,L"SampleInputEnabled",0));
        inputGain=std::min<DWORD>(1000,readNumber(key,L"SampleInputGain",1000));
        inputChannel=std::min<DWORD>(2,readNumber(key,L"SampleInputChannel",0));
        reconstruction=readNumber(key,L"S3528Runtime",0)!=0;
        nativeAudio=readNumber(key,L"NativeDacDeglitch",0)!=0;
        liveSerial = readNumber(key,L"LiveSerialView",0)!=0;
        diagnostic = readNumber(key, L"Diagnostic", 0) == 1;
        concreteStartup = readNumber(key, L"ConcreteStartup", 1) != 0;
        for (unsigned i = 0; i < sliders.size(); ++i)
            sliders[i] = std::min<DWORD>(1000, readNumber(key, sliderKey(i).c_str(), sliders[i]));
        RegCloseKey(key);
    }
    bool save() const {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath, 0, nullptr, 0, KEY_SET_VALUE,
                           nullptr, &key, nullptr) != ERROR_SUCCESS)
            return false;
        bool ok = true;
        auto text = [&](const wchar_t *name, const std::wstring &value) {
            if (RegSetValueExW(key, name, 0, REG_SZ,
                              reinterpret_cast<const BYTE *>(value.c_str()),
                              DWORD((value.size() + 1) * sizeof(wchar_t))) != ERROR_SUCCESS)
                ok = false;
        };
        auto number = [&](const wchar_t *name, DWORD value) {
            if (RegSetValueExW(key, name, 0, REG_DWORD,
                              reinterpret_cast<const BYTE *>(&value), sizeof(value)) != ERROR_SUCCESS)
                ok = false;
        };
        const auto profile=calibration.serialize(),legacyProfile=calibration.serialize(true);
        text(L"FilterCalibrationV2",std::wstring(profile.begin(),profile.end()));
        text(L"FilterCalibrationV1",std::wstring(legacyProfile.begin(),legacyProfile.end()));
        text(L"RomFolder", romFolder);
        text(L"Disk", disk);
        text(L"HddImage", hddImage);
        number(L"DirectImageWrites",directImageWrites);
        text(L"AudioEndpoint", audioId);
        text(L"SampleInputEndpoint", inputId);
        text(L"MidiInput", midiId);
        number(L"MidiPanelWheels",midiWheels);
        number(L"NoteOffZero",noteOffZero);
        number(L"ChipModel",chipModel);number(L"S3528Runtime",reconstruction);number(L"NativeDacDeglitch",nativeAudio);
        number(L"OutputVolume",outputVolume);
        number(L"SampleInputEnabled",inputEnabled);
        number(L"SampleInputGain",inputGain);
        number(L"SampleInputChannel",inputChannel);
        number(L"LiveSerialView",liveSerial);
        number(L"Diagnostic", diagnostic);
        number(L"ConcreteStartup", concreteStartup);
        for (unsigned i = 0; i < sliders.size(); ++i)
            number(sliderKey(i).c_str(), sliders[i]);
        RegCloseKey(key);
        return ok;
    }

  private:
    static constexpr const wchar_t *keyPath = L"Software\\Tuth\\EIIEmulator\\FrontPanel";
    static std::wstring sliderKey(unsigned i) { return L"Slider" + std::to_wstring(i); }
    static DWORD readNumber(HKEY key, const wchar_t *name, DWORD fallback) {
        DWORD value = 0, type = 0, bytes = sizeof(value);
        if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE *>(&value), &bytes) ==
                ERROR_SUCCESS && type == REG_DWORD && bytes == sizeof(value))
            return value;
        return fallback;
    }
    static std::wstring readString(HKEY key, const wchar_t *name) {
        DWORD type = 0, bytes = 0;
        if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS ||
            type != REG_SZ || bytes > 65536 || bytes % sizeof(wchar_t))
            return {};
        std::vector<wchar_t> value(bytes / sizeof(wchar_t) + 1, 0);
        if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE *>(value.data()),
                            &bytes) != ERROR_SUCCESS || type != REG_SZ)
            return {};
        return value.data();
    }
};
} // namespace eii::host
#endif

