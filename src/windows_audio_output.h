#pragma once
#ifdef _WIN32
#include "windows_audio_endpoints.h"
#include "runtime_audio.h"
#include "host_audio_schedule.h"
#include "host_thread_cpu.h"
#include <future>
#include <thread>
#include <mutex>
#include <atomic>

namespace eii::host {
class WindowsAudioOutput {
public:
    WindowsAudioOutput():stop_(CreateEventW(nullptr,TRUE,FALSE,nullptr)) {
        if(!stop_)throw std::runtime_error("Cannot create audio stop event");
    }
    ~WindowsAudioOutput(){stop();CloseHandle(stop_);}
    WindowsAudioOutput(const WindowsAudioOutput&)=delete;
    WindowsAudioOutput& operator=(const WindowsAudioOutput&)=delete;
    void start(std::wstring id,std::shared_ptr<AudioQueue> queue) {
        stop();ResetEvent(stop_);
        {std::lock_guard lock(mutex_);error_.clear();lastError_.clear();}
        submitted_=events_=timeouts_=restarts_=lastSubmit_=0;
        auto ready=std::make_shared<std::promise<void>>();auto result=ready->get_future();
        thread_=std::thread([this,id=std::move(id),queue=std::move(queue),ready]{run(id,queue,ready);});
        try {result.get();} catch(...) {stop();throw;}
    }
    void stop() noexcept {SetEvent(stop_);if(thread_.joinable())thread_.join();bufferFrames_=0;running_=false;multimedia_=false;timer_=false;}
    std::string error() const {std::lock_guard lock(mutex_);return error_;}
    bool multimedia()const noexcept{return multimedia_.load();}
    bool timerResolution()const noexcept{return timer_.load();}
    unsigned bufferFrames() const noexcept {return bufferFrames_.load();}
    std::string diagnostics()const {
        std::ostringstream s;s<<"HOST WASAPI running="<<running_.load()<<" multimedia="<<multimedia_.load()<<" timer1ms="<<timer_.load()
          <<" submitted="<<submitted_.load()<<" events="<<events_.load()<<" timeouts="<<timeouts_.load()<<" restarts="<<restarts_.load();
        const auto last=lastSubmit_.load();if(last)s<<" last-submit-ms="<<(GetTickCount64()-last);
        std::lock_guard lock(mutex_);s<<" error=["<<error_<<"] last-error=["<<lastError_<<"]";return s.str();
    }
private:
    struct Event {
        HANDLE value=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        ~Event(){if(value)CloseHandle(value);}
    };
    struct Apartment {
        Apartment(){audioCheck(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"Audio COM initialize");}
        ~Apartment(){CoUninitialize();}
    };
    HANDLE stop_;
    std::thread thread_;
    mutable std::mutex mutex_;
    std::string error_,lastError_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> submitted_{0},events_{0},timeouts_{0},restarts_{0},lastSubmit_{0};
    std::atomic<unsigned> bufferFrames_{0};std::atomic<bool> multimedia_{false},timer_{false};
    void renderOnce(const std::wstring& id,const std::shared_ptr<AudioQueue>& queue,
                    const std::shared_ptr<std::promise<void>>& ready,bool& announced) {
            Apartment apartment;
            ComOwner<IMMDeviceEnumerator> enumerator;
            audioCheck(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,
                __uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(enumerator.put())),"Audio enumerator");
            ComOwner<IMMDevice> device;
            if(id.empty())audioCheck(enumerator->GetDefaultAudioEndpoint(eRender,eMultimedia,device.put()),"Default audio output");
            else audioCheck(enumerator->GetDevice(id.c_str(),device.put()),"Audio output device");
            // Keep event handle alive until after the client is released, also
            // on errors/device removal during rendering.
            Event event;if(!event.value)throw std::runtime_error("Cannot create audio render event");
            ComOwner<IAudioClient> client;
            audioCheck(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,
                reinterpret_cast<void**>(client.put())),"Audio client activate");
            WAVEFORMATEX format{};
            format.wFormatTag=WAVE_FORMAT_IEEE_FLOAT;format.nChannels=2;
            format.nSamplesPerSec=analog::RuntimeAudioEngine::rate;format.wBitsPerSample=32;
            format.nBlockAlign=8;format.nAvgBytesPerSec=format.nSamplesPerSec*format.nBlockAlign;
            audioCheck(client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK|AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|
                AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,0,0,&format,nullptr),"Audio stream initialize");
            audioCheck(client->SetEventHandle(event.value),"Audio render event");
            UINT32 capacity=0;audioCheck(client->GetBufferSize(&capacity),"Audio buffer size");
            bufferFrames_=capacity;
            ComOwner<IAudioRenderClient> render;
            audioCheck(client->GetService(__uuidof(IAudioRenderClient),reinterpret_cast<void**>(render.put())),"Audio render service");
            // Prime with silence, then event-driven output. No spin/poll loop.
            BYTE* buffer=nullptr;
            audioCheck(render->GetBuffer(capacity,&buffer),"Audio prime buffer");
            audioCheck(render->ReleaseBuffer(capacity,AUDCLNT_BUFFERFLAGS_SILENT),"Audio prime release");
            std::vector<StereoFrame> frames(capacity);
            // Keep a20ms default bound, but allow one actual endpoint buffer
            // when a device requires more. Never starve a long render period
            // merely because the generic queue limit is smaller than it.
            queue->setLimit(std::max(AudioQueue::realtimeLimit,
                                    std::min(unsigned(capacity),AudioQueue::capacity)));
            queue->clear();
            audioCheck(client->Start(),"Audio start");
            running_=true;lastSubmit_=GetTickCount64();
            {std::lock_guard lock(mutex_);error_.clear();}
            if(!announced){announced=true;ready->set_value();}
            HANDLE events[]{stop_,event.value};
            while(true) {
                const auto wait=WaitForMultipleObjects(2,events,FALSE,500);
                if(wait==WAIT_OBJECT_0)break;
                if(wait==WAIT_TIMEOUT){++timeouts_;throw std::runtime_error("WASAPI render event timed out (500ms)");}
                if(wait!=WAIT_OBJECT_0+1)throw std::runtime_error("Audio event wait failed");
                ++events_;
                UINT32 padding=0;audioCheck(client->GetCurrentPadding(&padding),"Audio padding");
                if(padding>capacity)throw std::runtime_error("Invalid audio buffer padding");
                const UINT32 count=capacity-padding;
                if(!count){if(GetTickCount64()-lastSubmit_.load()>500)throw std::runtime_error("WASAPI buffer made no render progress (500ms)");continue;}
                queue->pop(std::span(frames.data(),count));
                audioCheck(render->GetBuffer(count,&buffer),"Audio render buffer");
                // Explicit interleave avoids relying on structure padding.
                auto* out=reinterpret_cast<float*>(buffer);
                for(UINT32 i=0;i<count;++i){out[2*i]=frames[i].left;out[2*i+1]=frames[i].right;}
                audioCheck(render->ReleaseBuffer(count,0),"Audio release buffer");
                submitted_+=count;lastSubmit_=GetTickCount64();
            }
            audioCheck(client->Stop(),"Audio stop");
    }
    void run(const std::wstring& id,const std::shared_ptr<AudioQueue>& queue,
             const std::shared_ptr<std::promise<void>>& ready)noexcept {
        nameCurrentThread(L"EII WASAPI");
        bool announced=false;
        {
            const AudioThreadSchedule schedule(true);multimedia_=schedule.multimedia();timer_=schedule.timerResolution();
            for(unsigned attempt=0;;++attempt) {
                try {renderOnce(id,queue,ready,announced);break;}
                catch(...) {
                    running_=false;bufferFrames_=0;
                    if(WaitForSingleObject(stop_,0)==WAIT_OBJECT_0)break;
                    auto failure=std::current_exception();std::string message="Unknown audio output error";
                    try {std::rethrow_exception(failure);}catch(const std::exception& e){message=e.what();}catch(...){}
                    {std::lock_guard lock(mutex_);error_=lastError_=message;}
                    if(!announced){ready->set_exception(failure);break;}
                    // Bounded runtime recovery: reopen only this endpoint stream.
                    // Firmware, MIDI and chip state remain on their own owners.
                    if(attempt>=3||WaitForSingleObject(stop_,100)==WAIT_OBJECT_0)break;
                    ++restarts_;
                }
            }
        }
        running_=false;bufferFrames_=0;multimedia_=false;timer_=false;
    }
};
}
#endif
