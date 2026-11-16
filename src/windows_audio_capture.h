#pragma once
#ifdef _WIN32
#include "windows_audio_endpoints.h"
#include "host_audio_schedule.h"
#include "host_thread_cpu.h"
#include "sample_input_live.h"
#include <mmreg.h>
#include <cstring>
#include <sstream>
#include <vector>
#include <atomic>
#include <future>
#include <mutex>
#include <thread>

namespace eii::host {
// WASAPI shared-mode capture for the sample input (7-42 +INPUT.V). Own named
// thread with the multimedia schedule; event driven, no polling or spinning.
// Device mix format -> mono (selectable: (L+R)/2 mix, left or right) ->
// 4-point cubic resample to 48 kHz when the device rate differs ->
// LiveSampleInput jitter buffer. Never blocks the machine thread.
class WindowsAudioCapture {
public:
    WindowsAudioCapture():stop_(CreateEventW(nullptr,TRUE,FALSE,nullptr)){if(!stop_)throw std::runtime_error("Cannot create capture stop event");}
    ~WindowsAudioCapture(){stop();CloseHandle(stop_);}
    WindowsAudioCapture(const WindowsAudioCapture&)=delete;
    WindowsAudioCapture& operator=(const WindowsAudioCapture&)=delete;
    // id empty = system default input.
    void start(std::wstring id,std::shared_ptr<analog::LiveSampleInput> sink) {
        stop();ResetEvent(stop_);{std::lock_guard lock(mutex_);error_.clear();}
        packets_=frames_=silentPackets_=discontinuities_=0;
        auto ready=std::make_shared<std::promise<void>>();auto result=ready->get_future();
        thread_=std::thread([this,id=std::move(id),sink=std::move(sink),ready]{run(id,sink,ready);});
        try{result.get();}catch(...){stop();throw;}
    }
    void stop() noexcept {SetEvent(stop_);if(thread_.joinable())thread_.join();running_=false;}
    bool running()const noexcept{return running_.load();}
    // 0 = mix (L+R)/2 (all channels averaged), 1 = left, 2 = right.
    std::atomic<unsigned> channelMode{0};
    unsigned deviceRate()const noexcept{return rate_.load();}
    unsigned deviceChannels()const noexcept{return channels_.load();}
    std::string error()const{std::lock_guard lock(mutex_);return error_;}
    std::string diagnostics()const{
        std::ostringstream s;s<<"CAPTURE running="<<running_.load()<<" rate="<<rate_.load()<<" ch="<<channels_.load()<<" packets="<<packets_.load()
         <<" frames="<<frames_.load()<<" silent="<<silentPackets_.load()<<" discontinuities="<<discontinuities_.load();
        std::lock_guard lock(mutex_);s<<" error=["<<error_<<"]";return s.str();
    }
private:
    struct Event{HANDLE value=CreateEventW(nullptr,FALSE,FALSE,nullptr);~Event(){if(value)CloseHandle(value);}};
    struct Apartment{Apartment(){audioCheck(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"Capture COM initialize");}~Apartment(){CoUninitialize();}};
    HANDLE stop_;std::thread thread_;mutable std::mutex mutex_;std::string error_;
    std::atomic<bool> running_{false};std::atomic<unsigned> rate_{0},channels_{0};
    std::atomic<std::uint64_t> packets_{0},frames_{0},silentPackets_{0},discontinuities_{0};
    void run(const std::wstring& id,const std::shared_ptr<analog::LiveSampleInput>& sink,const std::shared_ptr<std::promise<void>>& ready) noexcept {
        nameCurrentThread(L"EII WASAPI capture");
        const AudioThreadSchedule schedule(false);
        bool announced=false;
        try {
            Apartment apartment;
            ComOwner<IMMDeviceEnumerator> enumerator;
            audioCheck(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(enumerator.put())),"Capture enumerator");
            ComOwner<IMMDevice> device;
            if(id.empty())audioCheck(enumerator->GetDefaultAudioEndpoint(eCapture,eMultimedia,device.put()),"Default audio input");
            else audioCheck(enumerator->GetDevice(id.c_str(),device.put()),"Audio input device");
            Event event;if(!event.value)throw std::runtime_error("Cannot create capture event");
            ComOwner<IAudioClient> client;
            audioCheck(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.put())),"Capture client activate");
            WAVEFORMATEX* raw=nullptr;audioCheck(client->GetMixFormat(&raw),"Capture mix format");
            std::unique_ptr<WAVEFORMATEX,decltype(&CoTaskMemFree)> format(raw,&CoTaskMemFree);
            const unsigned channels=raw->nChannels,bits=raw->wBitsPerSample,block=raw->nBlockAlign,rate=raw->nSamplesPerSec;
            bool isFloat=raw->wFormatTag==WAVE_FORMAT_IEEE_FLOAT;
            if(raw->wFormatTag==WAVE_FORMAT_EXTENSIBLE)isFloat=reinterpret_cast<WAVEFORMATEXTENSIBLE*>(raw)->SubFormat.Data1==WAVE_FORMAT_IEEE_FLOAT; // KSDATAFORMAT_SUBTYPE_* Data1 = tag
            if(!channels||!rate||(isFloat&&bits!=32)||(!isFloat&&bits!=16&&bits!=24&&bits!=32))throw std::runtime_error("Unsupported capture mix format");
            audioCheck(client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_EVENTCALLBACK,1000000,0,raw,nullptr),"Capture initialize");
            audioCheck(client->SetEventHandle(event.value),"Capture event");
            ComOwner<IAudioCaptureClient> capture;
            audioCheck(client->GetService(__uuidof(IAudioCaptureClient),reinterpret_cast<void**>(capture.put())),"Capture service");
            rate_=rate;channels_=channels;
            audioCheck(client->Start(),"Capture start");running_=true;
            announced=true;ready->set_value();
            const double step=double(rate)/analog::LiveSampleInput::rate; // input samples per output sample
            double phase=0,h[4]{};std::vector<float> mono,out;
            HANDLE events[]{stop_,event.value};
            while(true) {
                const auto wait=WaitForMultipleObjects(2,events,FALSE,1000);
                if(wait==WAIT_OBJECT_0)break;
                if(wait!=WAIT_OBJECT_0+1&&wait!=WAIT_TIMEOUT)throw std::runtime_error("Capture wait failed");
                UINT32 packet=0;audioCheck(capture->GetNextPacketSize(&packet),"Capture packet size");
                while(packet) {
                    BYTE* data=nullptr;UINT32 frames=0;DWORD flags=0;
                    audioCheck(capture->GetBuffer(&data,&frames,&flags,nullptr,nullptr),"Capture buffer");
                    ++packets_;frames_+=frames;
                    if(flags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)++discontinuities_;
                    mono.assign(frames,0.f);
                    if(flags&AUDCLNT_BUFFERFLAGS_SILENT)++silentPackets_;
                    else {
                        const unsigned mode=channelMode.load(std::memory_order_relaxed);
                        const unsigned c0=mode==2&&channels>1?1:0,c1=mode==0?channels:c0+1;
                        for(UINT32 f=0;f<frames;++f){double sum=0;const BYTE* p=data+std::size_t(f)*block;
                            for(unsigned c=c0;c<c1;++c){const BYTE* q=p+c*(bits/8);double v;
                                if(isFloat){float x;std::memcpy(&x,q,4);v=x;}
                                else if(bits==16)v=std::int16_t(q[0]|(q[1]<<8))/32768.0;
                                else if(bits==24)v=double(std::int32_t((q[0]<<8)|(q[1]<<16)|(q[2]<<24))>>8)/8388608.0;
                                else{std::int32_t x;std::memcpy(&x,q,4);v=x/2147483648.0;}
                                sum+=v;}
                            mono[f]=float(sum/double(c1-c0));}
                    }
                    audioCheck(capture->ReleaseBuffer(frames),"Capture release");
                    out.clear();
                    if(rate==48000)out.assign(mono.begin(),mono.end());
                    else {
                        // 4-point cubic (Catmull-Rom) resampler, history carried
                        // across packets: output between h[1] and h[2].
                        for(UINT32 i=0;i<frames;++i){h[0]=h[1];h[1]=h[2];h[2]=h[3];h[3]=mono[i];
                            while(phase<1.0){const double t=phase,a=h[0],b=h[1],c=h[2],d=h[3];
                                out.push_back(float(b+0.5*t*(c-a+t*(2*a-5*b+4*c-d+t*(3*(b-c)+d-a)))));phase+=step;}
                            phase-=1.0;}
                    }
                    if(!out.empty())sink->push(out);
                    audioCheck(capture->GetNextPacketSize(&packet),"Capture packet size");
                }
            }
            client->Stop();
        } catch(const std::exception& e) {
            std::lock_guard lock(mutex_);error_=e.what();
            if(!announced){try{ready->set_exception(std::current_exception());}catch(...){}}
        }
        running_=false;
    }
};
}
#endif
