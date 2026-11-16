#pragma once
#ifdef _WIN32
// Endpoint selection/format discovery. WindowsAudioOutput owns the render stream.
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace eii::host {
template <class T> class ComOwner {
  public:
    ~ComOwner() {
        if (p_)
            p_->Release();
    }
    ComOwner() = default;
    ComOwner(const ComOwner &) = delete;
    ComOwner &operator=(const ComOwner &) = delete;
    T *operator->() const { return p_; }
    T **put() {
        if (p_) {
            p_->Release();
            p_ = nullptr;
        }
        return &p_;
    }

  private:
    T *p_ = nullptr;
};
inline void audioCheck(HRESULT value, const char *operation) {
    if (FAILED(value)) {
        std::ostringstream s;
        s << operation << " failed (0x" << std::hex << unsigned(value) << ")";
        throw std::runtime_error(s.str());
    }
}
struct AudioEndpoint {
    std::wstring id, name;
};
class WindowsAudioEndpoints {
  public:
    explicit WindowsAudioEndpoints(EDataFlow flow = eRender) : flow_(flow) {}
    std::vector<AudioEndpoint> devices;
    std::wstring selectedId;
    void refresh() {
        auto fresh = std::vector<AudioEndpoint>{
            {L"", flow_ == eCapture ? L"System default input (WASAPI)" : L"System default output (WASAPI)"}};
        ComOwner<IMMDeviceEnumerator> enumerator;
        create(enumerator);
        ComOwner<IMMDeviceCollection> collection;
        audioCheck(enumerator->EnumAudioEndpoints(flow_, DEVICE_STATE_ACTIVE, collection.put()),
                   "EnumAudioEndpoints");
        UINT count = 0;
        audioCheck(collection->GetCount(&count), "GetCount");
        for (UINT i = 0; i < count; ++i) {
            ComOwner<IMMDevice> device;
            audioCheck(collection->Item(i, device.put()), "Endpoint Item");
            LPWSTR rawId = nullptr;
            audioCheck(device->GetId(&rawId), "GetId");
            std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> id(rawId, &CoTaskMemFree);
            std::wstring name = rawId;
            ComOwner<IPropertyStore> properties;
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, properties.put()))) {
                PROPVARIANT value{};
                if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) &&
                    value.vt == VT_LPWSTR && value.pwszVal)
                    name = value.pwszVal;
                PropVariantClear(&value);
            }
            fresh.push_back({rawId, name});
        }
        devices = std::move(fresh);
    }
    std::wstring select(unsigned index) {
        if (index >= devices.size())
            throw std::invalid_argument("audio endpoint selection outside list");
        ComOwner<IMMDeviceEnumerator> enumerator;
        create(enumerator);
        ComOwner<IMMDevice> device;
        const auto &id = devices[index].id;
        if (id.empty())
            audioCheck(enumerator->GetDefaultAudioEndpoint(flow_, eMultimedia, device.put()),
                       "Default endpoint");
        else
            audioCheck(enumerator->GetDevice(id.c_str(), device.put()), "GetDevice");
        ComOwner<IAudioClient> client;
        audioCheck(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                    reinterpret_cast<void **>(client.put())),
                   "AudioClient Activate");
        WAVEFORMATEX *raw = nullptr;
        audioCheck(client->GetMixFormat(&raw), "GetMixFormat");
        std::unique_ptr<WAVEFORMATEX, decltype(&CoTaskMemFree)> format(raw, &CoTaskMemFree);
        selectedId = id;
        return std::to_wstring(raw->nSamplesPerSec) + L" Hz / " + std::to_wstring(raw->nChannels) +
               L" channels mix format";
    }

  private:
    EDataFlow flow_ = eRender;
    static void create(ComOwner<IMMDeviceEnumerator> &out) {
        audioCheck(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                    __uuidof(IMMDeviceEnumerator),
                                    reinterpret_cast<void **>(out.put())),
                   "DeviceEnumerator");
    }
};
} // namespace eii::host
#endif
