#pragma once
#include "vst3/scanner_panel_response.h"
// VST3 machine owner. Separate implementation from FrontPanelSession (the
// standalone stays untouched): one owner thread constructs/runs the Machine,
// the host audio thread only exchanges atomics, SPSC events and rendered frames.
//
// Pacing: emulated time follows the host's audio clock, never wall time. The
// host sample counter h maps to CPU tick G = anchorTick + h*4e6/hostRate and to
// 48 kHz stream frame F = anchorFrame + h*48000/hostRate. process() publishes the
// end of its block as the machine target; the owner thread runs real firmware up
// to exactly that tick, delivering each host event at its own tick through the
// standalone MIDI path (MidiHostTools adaptation -> Machine::receiveMidi -> MIDI
// wire -> SCN2651/CTC firmware). Output frame for host sample h is stream frame
// at h-latency, so a realtime machine gets one block period plus a margin to
// render, and an offline/freewheel host simply waits for it.
#include "front_panel_session.h"
#include "vst3/rt_primitives.h"
#include "vst3/resampler.h"
#include "vst3/plugin_state.h"
#include "vst3/host_sample_input.h"
#include "vst3/sampling_trace.h"
#include "gui_stats.h"
#include "eii_bank_format.h"
#ifdef _WIN32
#include "vst3/cpu_placement.h"
#endif
#include <cstdlib>
#include <ctime>
#include <deque>
#include <functional>
#include <limits>

namespace eii::vst {
struct HostEvent {
    std::uint64_t epoch=0,pos=0;       // host sample position in that activation epoch
    std::uint8_t kind=0,size=0;        // 0 MIDI short message, 1 panel control voltage
    std::array<std::uint8_t,3> bytes{};
    std::uint8_t control=0;float volts=0;
};
struct EngineStatus {
    PanelSnapshot panel;
    std::uint64_t underruns=0,ringOverflows=0,eventDrops=0,dspDrops=0,framesWritten=0;float outputPeak=0;
    double machineLoad=0,dspWait=0; // wall seconds per emulated second: machine run(), waiting for the DSP queue
    double hostRate=48000;unsigned latency=0;bool anchored=false,paused=false,halted=false;
    std::uint64_t inputFrames=0,inputDrops=0,inputMissing=0,sampleReads=0,sampleUnknownReads=0,sampleRailClips=0;double inputTrimDb=0;
    int recallProgram=-1;bool recallPending=false;
    double callbackMaxGapMs=0,ownerLagMs=0;std::uint64_t callbacks=0;
    bool ownerMmcss=false,ownerHighQos=false,highResolutionWait=false,fastPanelActive=false,memory1mbActive=false;
};

class PluginEngine {
public:
    // Owner runs: 5 ms (20000 ticks) / 1 ms when urgent; SSM2045 models use whole
    // 96-frame DSP blocks (3 x 8000 / 8000 ticks) so only the run ending at the
    // host target flushes a partial worker block.
    static constexpr std::uint64_t cpuRate=4000000,quantum=20000,fineQuantum=4000,dspBlockTicks=8000;
    // A stall of the owner thread is absorbed up to about margin - 1 ms (frames
    // ready beyond the next block = margin). 30 ms rides through the 5-20 ms
    // preemptions of the soak test; 10/20/40 are selectable.
    static constexpr int defaultMarginMs=30;

    PluginEngine():thread_([this]{loop();}){}
    ~PluginEngine(){
        {std::lock_guard l(mutex_);quit_=true;}
        wake_.set();thread_.join();
    }
    PluginEngine(const PluginEngine&)=delete;PluginEngine& operator=(const PluginEngine&)=delete;

    // ---- Host configuration (non-realtime thread, not while processing) ----
    void setupProcessing(double sampleRate,int maxBlock){
        const auto rate=std::uint32_t(std::lround(std::clamp(sampleRate,8000.0,768000.0)));
        rate_.store(rate);maxBlock_=std::max(1,maxBlock);resampler_.configure(rate);updateLatency();
    }
    // Realtime safety margin (ms). Changes the reported latency: the plug-in asks
    // the host to re-query it (kLatencyChanged); takes effect at the next activation.
    void setMarginMs(int ms){marginMs_=std::clamp(ms,5,100);updateLatency();}
    unsigned latencySamples()const noexcept{return latency_.load();}
    double sampleRate()const noexcept{return rate_.load();}
    // New activation epoch: host sample counter restarts at 0, stream re-anchors.
    void activate(){lastCallbackNs_=0;callbackMaxGapUs_.store(0);callbackCount_.store(0);minLead_.store(std::numeric_limits<std::int64_t>::max());hostPos_=0;hostTarget_.store(0,std::memory_order_relaxed);consumerEpoch_=0;epoch_.fetch_add(1,std::memory_order_acq_rel);wake_.set();}

    // ---- Realtime (host audio thread). No locks, no allocation. ----
    void queueMidi(int offset,const std::uint8_t* bytes,unsigned size)noexcept{
        HostEvent e;e.epoch=epoch_.load(std::memory_order_relaxed);e.pos=hostPos_+std::uint64_t(std::max(0,offset));e.kind=0;e.size=std::uint8_t(size);
        for(unsigned i=0;i<size&&i<3;++i)e.bytes[i]=bytes[i];
        if(!events_.push(e))eventDrops_.fetch_add(1,std::memory_order_relaxed);
    }
    bool queueVoltage(int offset,unsigned control,double volts)noexcept{
        HostEvent e;e.epoch=epoch_.load(std::memory_order_relaxed);e.pos=hostPos_+std::uint64_t(std::max(0,offset));e.kind=1;e.control=std::uint8_t(control);e.volts=float(std::clamp(volts,0.0,5.0));
        const bool accepted=events_.push(e);if(!accepted)eventDrops_.fetch_add(1,std::memory_order_relaxed);return accepted;
    }
    void pushInput(const float* left,const float* right,int n,bool stereo,bool silentLeft=false,bool silentRight=false)noexcept {
        hostInput_->push(epoch_.load(std::memory_order_relaxed),hostPos_,left,right,n,stereo,silentLeft,silentRight);
    }
    void setOutputGain(float g)noexcept{gain_.store(std::clamp(g,0.f,1.f),std::memory_order_relaxed);}
    // Renders n host frames. Realtime never blocks: unavailable frames are silent
    // (counted as underruns). Offline waits until the owner has produced them.
    // channels: optional 8 output pointers (individual outputs J1..J8 = firmware
    // channels 7..0 per sheet 7-52); null entries are skipped.
    void render(float* left,float* right,int n,bool offline,float* const* channels=nullptr)noexcept{
        if(samplingTraceRecording_.load(std::memory_order_relaxed)){
            const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            if(!offline&&lastCallbackNs_){const auto gap=std::uint64_t(std::max<std::int64_t>(0,now-lastCallbackNs_)/1000);
                if(gap>callbackMaxGapUs_.load(std::memory_order_relaxed))callbackMaxGapUs_.store(gap,std::memory_order_relaxed);}
            lastCallbackNs_=now;
        }else lastCallbackNs_=0;
        callbackCount_.fetch_add(1,std::memory_order_relaxed);
        const auto epoch=epoch_.load(std::memory_order_relaxed);offline_.store(offline,std::memory_order_relaxed);
        hostTarget_.store(hostPos_+std::uint64_t(n),std::memory_order_release);wake_.set();
        if(anchorEpoch_.load(std::memory_order_acquire)!=epoch&&offline)
            for(int i=0;i<2000&&anchorEpoch_.load(std::memory_order_acquire)!=epoch;++i)anchorSignal_.wait(1);
        if(anchorEpoch_.load(std::memory_order_acquire)!=epoch){silence(left,right,n);hostPos_+=std::uint64_t(n);return;}
        const std::uint64_t base=anchorFrame_.load(std::memory_order_relaxed);
        if(consumerEpoch_!=epoch){consumerEpoch_=epoch;nextPull_=base;history_.fill({});channelHistory_.fill({});}
        const std::int64_t latency=latency_.load(std::memory_order_relaxed);const std::uint32_t rate=rate_.load(std::memory_order_relaxed);
        const float target=gain_.load(std::memory_order_relaxed);
        if(rate==48000){
            const std::int64_t first=std::int64_t(hostPos_)-latency;
            if(offline&&first+n>0)ring_.waitWritten(base+std::uint64_t(first+n),20000);
            for(int j=0;j<n;++j){
                host::StereoFrame f{};ChannelRing::Frame c{};const std::int64_t rel=first+j;
                if(rel>=0){const auto index=base+std::uint64_t(rel);if(index>=nextPull_){if(!ring_.read(index,f))underruns_.fetch_add(1,std::memory_order_relaxed);if(channels)channelRing_.read(index,c);nextPull_=index+1;}}
                gainNow_+=(target-gainNow_)*0.002f;left[j]=f.left*gainNow_;if(right)right[j]=f.right*gainNow_;
                if(channels)for(unsigned k=0;k<8;++k)if(channels[k])channels[k][j]=c[jackChannel(k)]*gainNow_;
            }
        }else{
            auto position=[&](int j,std::uint64_t& ip,double& frac){
                const std::int64_t rel=std::int64_t(hostPos_)+j-latency;if(rel<0)return false;
                const std::uint64_t num=std::uint64_t(rel)*48000u;ip=base+num/rate;frac=double(num%rate)/rate;return true;};
            if(offline){std::uint64_t ip;double frac;if(position(n-1,ip,frac))ring_.waitWritten(ip+SincResampler::K+1,20000);}
            for(int j=0;j<n;++j){
                std::uint64_t ip=0;double frac=0;float l=0,r=0;
                if(position(j,ip,frac)){
                    while(nextPull_<=ip+SincResampler::K){host::StereoFrame f{};if(!ring_.read(nextPull_,f))underruns_.fetch_add(1,std::memory_order_relaxed);history_[nextPull_&(history_.size()-1)]=f;
                        if(channels){ChannelRing::Frame c{};channelRing_.read(nextPull_,c);channelHistory_[nextPull_&(channelHistory_.size()-1)]=c;}++nextPull_;}
                    const auto first=ip+1-SincResampler::K;
                    l=resampler_.interpolate(frac,[&](int k){return history_[(first+std::uint64_t(k))&(history_.size()-1)].left;});
                    r=resampler_.interpolate(frac,[&](int k){return history_[(first+std::uint64_t(k))&(history_.size()-1)].right;});
                    if(channels)for(unsigned q=0;q<8;++q)if(channels[q]){const unsigned c=jackChannel(q);
                        channels[q][j]=resampler_.interpolate(frac,[&](int k){return channelHistory_[(first+std::uint64_t(k))&(channelHistory_.size()-1)][c];});}
                }else if(channels)for(unsigned q=0;q<8;++q)if(channels[q])channels[q][j]=0;
                gainNow_+=(target-gainNow_)*0.002f;left[j]=l*gainNow_;if(right)right[j]=r*gainNow_;
                if(channels)for(unsigned q=0;q<8;++q)if(channels[q])channels[q][j]*=gainNow_;
            }
        }
        readIndex_.store(nextPull_,std::memory_order_relaxed);
        if(!offline){ // frames already produced beyond the last one needed now
            const auto lead=std::int64_t(ring_.written())-std::int64_t(nextPull_);
            if(lead<minLead_.load(std::memory_order_relaxed))minLead_.store(lead,std::memory_order_relaxed);
        }
        hostPos_+=std::uint64_t(n);
    }
    // Smallest realtime lead (frames ready beyond the read point) since activation.
    std::int64_t minLead()const noexcept{return minLead_.load(std::memory_order_relaxed);}
    std::uint64_t rebases()const noexcept{return rebases_.load(std::memory_order_relaxed);}

    // ---- Commands (any non-realtime thread; executed by the owner between quanta) ----
    // Settings that can change while the machine runs (model, DAC route, S3528,
    // calibration) are applied at once, so the menu always shows what runs.
    // Media, ROM folder, HDD and fast panel apply at the next boot (menu says so).
    void configure(const PluginSettings& s){post([this,s]{const auto before=settings_;settings_=s;midiTools_.wheelBridge=s.midiWheels;midiTools_.noteOffZero=s.noteOffZero;recallProgram_.store(s.recallProgram);applyAudio(before);});}
    // reason: user boot (dirty media refuse unless discard), or project/state load
    // (dirty protected copies are first rescued to new recovery files).
    void boot(const PluginSettings& s,bool discardDirty,bool fromState=false){post([this,s,discardDirty,fromState]{settings_=s;bootNow(discardDirty,fromState);});}
    void button(ScannerButton b,bool pressed){post([this,b,pressed]{contact(unsigned(b),pressed);});}
    void voltage(unsigned control,double volts){post([this,control,volts]{if(control<6&&machine_)machine_->scannerControls.setVoltage(input(control),std::clamp(volts,0.0,5.0));});}
    // Idempotent: a command that matches the applied state does not restart audio.
    void audioModel(analog::RuntimeAudioMode mode){post([this,mode]{settings_.audioMode=mode;if(!machine_||halted_||paused_||machine_->runtimeAudioMode()==mode)return;quiesce();installAudio();});}
    void nativeAudio(bool on){post([this,on]{settings_.nativeAudio=on;if(!machine_||halted_||machine_->nativeAudioEnabled()==(on&&nonlinear()))return;quiesce();machine_->nativeAudio(on);restartPipe();});}
    void reconstruction(bool on){post([this,on]{settings_.reconstruction=on;if(machine_)machine_->reconstruction(on);});}
    void calibrate(const analog::FilterCalibration& c){c.validate();post([this,c]{if(analog::ssm2AudioMode(settings_.audioMode))settings_.ssm2Calibration=c;else if(settings_.audioMode==analog::RuntimeAudioMode::EiiiCustom)settings_.eiiiCalibration=c;else if(settings_.audioMode==analog::RuntimeAudioMode::LegacyQ)settings_.legacyCalibration=c;else settings_.calibration=c;if(machine_)machine_->calibrateAudio(c);});}
    void midiOptions(bool wheels,bool noteOffZero){post([this,wheels,noteOffZero]{settings_.midiWheels=wheels;settings_.noteOffZero=noteOffZero;midiTools_.wheelBridge=wheels;midiTools_.noteOffZero=noteOffZero;});}
    // Individual outputs (VST3 aux buses): compute them only while a host enables one.
    void channelOutputs(bool on){if(channelsWanted_.exchange(on)==on)return;post([this,on]{if(machine_)machine_->channelOutputs(on);});}
    bool channelOutputsEnabled()const noexcept{return channelsWanted_.load(std::memory_order_relaxed);}
    void allNotesOff(){post([this]{for(const auto& m:host::MidiHostTools::releaseAllMessages())applyMidi(m.data(),3);});}
    void setPaused(bool paused){post([this,paused]{if(paused==paused_||!machine_||halted_)return;if(paused){quiesce();paused_=true;}else{paused_=false;installAudio();}});}
    // Optional diagnostic source override; null restores the host audio input.
    void sampleSource(std::shared_ptr<analog::SampleInputSource> s){post([this,s]{sampleSource_=s;if(machine_)machine_->sampleInput=s?s:hostInput_;});}
    std::future<PanelMediaState> mediaState(){auto p=std::make_shared<std::promise<PanelMediaState>>();auto f=p->get_future();post([this,p]{p->set_value({hddState(),floppyState()});});return f;}
    std::future<std::string> mountFloppy(std::filesystem::path path,bool direct,bool discard){
        auto p=std::make_shared<std::promise<std::string>>();auto f=p->get_future();
        post([this,p,path,direct,discard]{try{p->set_value(mountFloppyNow(path,direct,discard));}catch(...){p->set_exception(std::current_exception());}});return f;
    }
    // .EII banks into the next free HD OS 3.1 slots (eii_bank_format.h, byte-exact
    // with firmware-written slots). newImage empty = write through the direct-write
    // lease; otherwise a NEW image file (existing files are never replaced) becomes
    // the HDD selection. The machine then reboots so the firmware reads the disk.
    std::future<std::string> importEiiToHdd(std::vector<std::filesystem::path> files,std::filesystem::path newImage){
        auto p=std::make_shared<std::promise<std::string>>();auto f=p->get_future();
        post([this,p,files,newImage]{try{p->set_value(importEiiNow(files,newImage));}catch(...){p->set_exception(std::current_exception());}});return f;
    }
    std::future<std::string> saveFloppyCopy(std::filesystem::path path){
        auto p=std::make_shared<std::promise<std::string>>();auto f=p->get_future();
        post([this,p,path]{try{
            if(!floppyBacking_)throw std::runtime_error("No floppy mounted.");
            if(!floppyState().idle)throw std::runtime_error("Finish the sampler floppy operation before saving.");
            floppyBacking_->saveCopy(path);floppyLastSaved_=path;if(machine_&&machine_->diskRead)machine_->diskRead->dirty[0]=false;p->set_value("Floppy copy saved.");
        }catch(...){p->set_exception(std::current_exception());}});return f;
    }
    std::future<std::string> saveHddCopy(std::filesystem::path path){
        auto p=std::make_shared<std::promise<std::string>>();auto f=p->get_future();
        post([this,p,path]{try{
            if(!machine_||!machine_->hardDisk)throw std::runtime_error("No HDD mounted.");
            if(!hddState().idle)throw std::runtime_error("HDD transfer is active. Let the sampler finish, then save.");
            host::saveNewHddImage(path,machine_->hardDisk->image);hddLastSaved_=path;machine_->hardDisk->dirty=false;p->set_value("HDD copy saved.");
        }catch(...){p->set_exception(std::current_exception());}});return f;
    }
    std::future<void> startSamplingTrace(){
        auto p=std::make_shared<std::promise<void>>();auto f=p->get_future();post([this,p]{try{samplingTrace_.start();callbackMaxGapUs_.store(0,std::memory_order_relaxed);outputPeak_.store(0,std::memory_order_relaxed);hostInput_->peak.store(0,std::memory_order_relaxed);hostInput_->peakDiagnostics.store(true,std::memory_order_relaxed);samplingTraceRecording_.store(true,std::memory_order_release);p->set_value();}catch(...){p->set_exception(std::current_exception());}});return f;
    }
    bool samplingTraceRecording()const noexcept{return samplingTraceRecording_.load(std::memory_order_acquire);}
    std::future<std::string> samplingTrace(){
        auto p=std::make_shared<std::promise<std::string>>();auto f=p->get_future();post([this,p]{samplingTrace_.stop();hostInput_->peakDiagnostics.store(false,std::memory_order_relaxed);samplingTraceRecording_.store(false,std::memory_order_release);try{p->set_value(samplingTrace_.csv());}catch(...){p->set_exception(std::current_exception());}});return f;
    }
    EngineStatus status()const{std::lock_guard l(statusMutex_);return status_;}
    int recallProgram()const noexcept{return recallProgram_.load();}
    std::uint64_t underruns()const noexcept{return underruns_.load();}

private:
    // ---------------- shared ----------------
    std::atomic<std::uint32_t> rate_{48000};std::atomic<unsigned> latency_{0};int maxBlock_=512;std::atomic<int> marginMs_{defaultMarginMs};
    // One host block for the owner to render after process() publishes it, a
    // margin for scheduling/DSP jitter, plus the interpolator delay.
    void updateLatency(){const auto rate=rate_.load();const unsigned kernel=rate==48000?0:unsigned(std::ceil(double(SincResampler::K+1)*rate/48000.0));
        latency_.store(unsigned(maxBlock_)+unsigned(std::ceil(rate*marginMs_.load()/1000.0))+kernel);}
    std::atomic<std::uint64_t> epoch_{1},hostTarget_{0}; // epoch 1 != anchorEpoch_ 0: unanchored until the owner runs
    std::atomic<std::uint64_t> anchorEpoch_{0},anchorFrame_{0},anchorTick_{0};
    std::atomic<std::uint64_t> underruns_{0},eventDrops_{0},rebases_{0},readIndex_{0};std::atomic<bool> offline_{false};std::atomic<std::int64_t> minLead_{std::numeric_limits<std::int64_t>::max()};
    std::atomic<std::uint64_t> callbackMaxGapUs_{0},callbackCount_{0};
    std::atomic<float> gain_{1},outputPeak_{0};std::atomic<int> recallProgram_{-1};
    SpscQueue<HostEvent,8192> events_;
    FrameRing ring_;
    Signal wake_,anchorSignal_,dspSignal_;
    mutable std::mutex mutex_;std::deque<std::function<void()>> commands_;bool quit_=false;
    mutable std::mutex statusMutex_;EngineStatus status_;
    // ---------------- audio thread only ----------------
    std::int64_t lastCallbackNs_=0;
    std::uint64_t hostPos_=0,consumerEpoch_=0,nextPull_=0;float gainNow_=1;
    std::array<host::StereoFrame,128> history_{};SincResampler resampler_;
    std::array<ChannelRing::Frame,128> channelHistory_{};
    ChannelRing channelRing_;std::atomic<bool> channelsWanted_{false};
    // Jack Jk carries CnOUT.V of firmware channel 8-k (7-52: C7OUT->R156->J1 ... C0OUT->R163->J8).
    static constexpr unsigned jackChannel(unsigned jack)noexcept{return 7-jack;}
    // ---------------- owner thread only ----------------
    bool ownerMmcss_=false,ownerHighQos_=false;
    SamplingTrace samplingTrace_;
    std::atomic<bool> samplingTraceRecording_{false};
    PluginSettings settings_;
    bool fastPanelActive_=false,memory1mbActive_=false;
    std::unique_ptr<Machine> machine_;bool halted_=false,paused_=false;bool floppyHeld_=false; // boot order "hard disk first": floppy inserted once the OS runs
    std::uint64_t G_=0,gOffset_=0,pipeStartG_=0,pipeStartFrame_=0,localAnchorEpoch_=0,anchorTickLocal_=0;
    host::MidiHostTools midiTools_;
    std::shared_ptr<HostSampleInput> hostInput_=std::make_shared<HostSampleInput>();
    std::shared_ptr<analog::SampleInputSource> sampleSource_;
    std::filesystem::path hddSource_,hddLastSaved_,floppyLastSaved_;
    std::shared_ptr<host::DirectImageFile> hddLease_;
    std::shared_ptr<host::FloppyImageBacking> floppyBacking_;
    struct Contact{bool held=false;std::uint64_t tick=~std::uint64_t(0);std::deque<bool> pending;};
    std::array<Contact,30> contacts_{};
    std::string startup_,media_,error_;bool recallPending_=false;std::uint64_t recallDeadline_=0;
    double runSeconds_=0,waitSeconds_=0,machineLoad_=0,dspWait_=0;std::uint64_t ranTicks_=0; // owner CPU per emulated second (load)
    std::uint64_t revision_=0,speedTick_=0;std::chrono::steady_clock::time_point speedTime_{},published_{};double speedRatio_=0;
    std::thread thread_; // last: starts after every member it uses exists

    void post(std::function<void()> f){{std::lock_guard l(mutex_);if(quit_)return;commands_.push_back(std::move(f));}wake_.set();}
    static ScannerAnalogInput input(unsigned n){return n<4?ScannerAnalogInput(unsigned(ScannerAnalogInput::SliderA)+n):n==4?ScannerAnalogInput::LeftWheel:ScannerAnalogInput::RightWheel;}
    static void silence(float* l,float* r,int n)noexcept{for(int i=0;i<n;++i){l[i]=0;if(r)r[i]=0;}}
    std::uint64_t expectedFrames(std::uint64_t g)const noexcept{return pipeStartFrame_+(g-pipeStartG_)*3/250;} // 48000/4e6
    std::uint64_t tickOf(std::uint64_t pos)const noexcept{return anchorTickLocal_+pos*cpuRate/rate_.load(std::memory_order_relaxed);}
    bool nonlinear()const noexcept{return machine_&&analog::workerAudioMode(machine_->runtimeAudioMode());}
    bool producing()const noexcept{return machine_&&!halted_&&!paused_;}

    void restartPipe(){pipeStartG_=G_;pipeStartFrame_=ring_.written();}
    // Make the owner the only producer again and align the stream with G_.
    void quiesce(){
        if(machine_&&nonlinear()&&!halted_&&!paused_){
            const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            while(std::chrono::steady_clock::now()<until){
                if(machine_->chipAudioStats().queuedFrames==0&&ring_.written()+2>=expectedFrames(G_))break;
                dspSignal_.wait(2);
            }
            // The worker may hold one block in flight: give it a moment to land.
            if(ring_.written()+2<expectedFrames(G_))dspSignal_.wait(20);
        }
        const auto w=ring_.written(),e=expectedFrames(G_);
        if(w<e)ring_.pushSilence(e-w);
        restartPipe();
    }
    void sink(std::span<const host::StereoFrame> frames){
        if(samplingTraceRecording_.load(std::memory_order_relaxed)){
            float peak=0;for(const auto& f:frames)peak=std::max(peak,std::max(std::abs(f.left),std::abs(f.right)));
            if(peak>outputPeak_.load(std::memory_order_relaxed))outputPeak_.store(peak,std::memory_order_relaxed);
        }
        ring_.push(frames);dspSignal_.set();
    }
    void applyAudio(const PluginSettings& before){
        if(!machine_||halted_)return;
        if(machine_->runtimeAudioMode()!=settings_.audioMode){if(!paused_){quiesce();installAudio();}return;} // installAudio applies everything below
        if(machine_->nativeAudioEnabled()!=(settings_.nativeAudio&&nonlinear())){quiesce();machine_->nativeAudio(settings_.nativeAudio);restartPipe();}
        if(machine_->reconstructionEnabled()!=settings_.reconstruction)machine_->reconstruction(settings_.reconstruction);
        machine_->analogDrift(settings_.analogDrift);
        if(!(activeCalibration(before)==activeCalibration(settings_)))machine_->calibrateAudio(activeCalibration(settings_));
    }
    void installAudio(){
        machine_->enableRuntimeAudio([this](auto frames){sink(frames);},settings_.audioMode, // joins any previous DSP worker
            [this](std::span<const ChannelRing::Frame> frames){channelRing_.alignTo(ring_.written());channelRing_.push(frames);});
        machine_->channelOutputs(channelsWanted_.load(std::memory_order_relaxed));machine_->analogDrift(settings_.analogDrift);
        placeDsp();
        machine_->calibrateAudio(activeCalibration(settings_));machine_->reconstruction(settings_.reconstruction);machine_->nativeAudio(settings_.nativeAudio);
        restartPipe();
    }
#ifdef _WIN32
    int machineClaim_=-1,dspClaim_=-1;DWORD dspThread_=0;
    void releaseDsp(){if(dspThread_){CorePool::forgetDspThread(dspThread_);dspThread_=0;}CorePool::release(dspClaim_);dspClaim_=-1;}
    void placeDsp(){
        releaseDsp();if(!CorePool::enabled()||!nonlinear())return;
        if((dspThread_=CorePool::claimNewDspThread())){const HANDLE h=OpenThread(THREAD_SET_LIMITED_INFORMATION|THREAD_QUERY_LIMITED_INFORMATION,FALSE,dspThread_);if(h){dspClaim_=CorePool::place(h);CloseHandle(h);}}
    }
#else
    void placeDsp(){}void releaseDsp(){}
#endif
    void halt(const std::string& why){error_=why;quiesce();halted_=true;}

    // ---- media (same guards as FrontPanelSession) ----
    static bool sameImage(const std::filesystem::path& a,const std::filesystem::path& b){
        if(a.empty()||b.empty())return false;std::error_code e;return std::filesystem::equivalent(a,b,e)&&!e;}
    PanelHddState floppyState()const{
        PanelHddState s;if(!floppyBacking_)return s;s.mounted=true;s.source=floppyBacking_->path;s.lastSaved=floppyLastSaved_;
        s.format=floppyBacking_->hfe?"hfe":"emuiifd";s.direct=floppyBacking_->direct;s.writable=floppyBacking_->writable;s.dirty=floppyBacking_->dirty;
        s.idle=!machine_||floppyHeld_||(machine_->diskRead&&!machine_->diskRead->writing()&&!machine_->diskDma.granted&&(!machine_->floppy.drives[0]||machine_->floppy.drives[0]->writeGateN()));return s;
    }
    PanelHddState hddState()const{
        PanelHddState s;s.source=hddSource_;s.lastSaved=hddLastSaved_;
        if(machine_&&machine_->hardDisk){s.mounted=true;s.dirty=machine_->hardDisk->dirty;s.direct=bool(hddLease_);
            s.idle=machine_->hardDisk->phase==HardDisk::Free&&!machine_->diskDma.granted;}
        return s;
    }
    static void installFloppy(Machine& m,const std::shared_ptr<host::FloppyImageBacking>& backing){
        if(!m.floppy.drives[0])m.floppy.attach(0,37,400000);
        m.floppy.drives[0]->insertSensorDisk(!backing->writable);
        if(!m.diskRead)m.diskRead=std::make_unique<DiskReadPath>(87,8,m.ticks);
        if(backing->writable)m.diskRead->mountWritable(0,backing->image,EmuiiGap::Hxc2026);
        else m.diskRead->mount(0,backing->image,EmuiiGap::Hxc2026);
        m.diskDma.floppyIo=backing->writable;
        m.diskRead->persistTrack=[backing](unsigned,unsigned c,unsigned side,std::span<const std::uint8_t> bytes){return backing->trackWritten(c,side,bytes);};
    }
    // OS area (first 78848 bytes) for a .EII bank import, in this order:
    // 1) the OS image chosen in the menu, 2) the mounted disk, 3) an OS image in
    // <library>\DISKS[\os-images] whose name says 3.1 (v31 / 3.1 / 31), 4) any OS image there.
    std::vector<std::uint8_t> osArea()const{
        constexpr auto n=host::FloppyImageBacking::bankOffset;
        const auto take=[&](const std::filesystem::path& p)->std::vector<std::uint8_t>{
            try{host::FloppyImageBacking os(p,false);const auto b=os.image->bytes();if(b.size()>=n)return {b.begin(),b.begin()+n};}catch(...){}return {};};
        if(!settings_.osImage.empty())if(auto v=take(settings_.osImage);!v.empty())return v;
        if(floppyBacking_&&floppyBacking_->image&&floppyBacking_->image->bytes().size()>=n){const auto b=floppyBacking_->image->bytes();return {b.begin(),b.begin()+n};}
        if(settings_.library.empty())return {};
        std::filesystem::path any;
        for(const auto& dir:{settings_.library/"DISKS"/"os-images",settings_.library/"DISKS"}){
            std::error_code e;if(!std::filesystem::is_directory(dir,e))continue;
            for(const auto& f:std::filesystem::directory_iterator(dir,e)){
                auto name=f.path().filename().string();for(auto& c:name)c=char(std::tolower((unsigned char)c));
                auto ext=f.path().extension().string();for(auto& c:ext)c=char(std::tolower((unsigned char)c));
                if((ext!=".emuiifd"&&ext!=".hfe")||(name.find("emuiios")==std::string::npos&&name.find("os")==std::string::npos))continue;
                if(name.find("v31")!=std::string::npos||name.find("3.1")!=std::string::npos||name.find("31")!=std::string::npos)if(auto v=take(f.path());!v.empty())return v;
                if(any.empty())any=f.path();
            }
        }
        return any.empty()?std::vector<std::uint8_t>{}:take(any);
    }
    static const char* mediaLabel(const host::FloppyImageBacking& b){
        return b.bankImport?"EII bank import (.eii) / read-only":b.imported?"Experimental HD OS E2O import / read-only":b.hfe?"HFE FM / writable":"EMUIIFD / writable";
    }
    std::string importEiiNow(const std::vector<std::filesystem::path>& files,const std::filesystem::path& newImage){
        if(settings_.hdd.empty())throw std::runtime_error("Select an HDD image first.");
        if(machine_&&machine_->hardDisk&&!hddState().idle)throw std::runtime_error("HDD busy. Finish the sampler disk operation first.");
        std::vector<std::uint8_t> image=machine_&&machine_->hardDisk?machine_->hardDisk->image:host::loadHddImage(settings_.hdd);
        const unsigned slots=bank::hddSlotCount(image.size());std::string report;unsigned next=0,written=0;
        for(const auto& file:files){
            std::ifstream in(file,std::ios::binary);std::vector<std::uint8_t> e((std::istreambuf_iterator<char>(in)),{});
            if(e.size()!=bank::eiiBytes)throw std::runtime_error("Not an EII bank (485887 bytes): "+file.filename().string());
            while(next<slots&&!bank::hddSlotFree(image,next))++next;
            if(next>=slots)throw std::runtime_error("No free HDD bank slot left; nothing was written.");
            bank::hddWriteSlot(image,next,e);++written;
            report+="Bank "+std::to_string(next)+": "+bank::hddSlotName(image,next)+"\n";++next;
        }
        if(!written)return "No banks selected.";
        if(newImage.empty()){
            if(!hddLease_)throw std::runtime_error("Direct image writes are off: choose a new HDD file instead.");
            hddLease_->persist(image);
        }else{host::saveNewHddImage(newImage,image);settings_.hdd=newImage;}
        bootNow(true,false); // the working copy is included in the written image
        return report+(newImage.empty()?"Written to the HDD image; machine rebooted.":"Saved as "+newImage.filename().string()+"; machine rebooted from it.");
    }
    std::string mountFloppyNow(const std::filesystem::path& path,bool direct,bool discard){
        if(!machine_)throw std::runtime_error("Boot the machine before inserting a floppy.");
        if(floppyState().dirty&&!discard)throw std::runtime_error("Floppy has unsaved changes. Save a copy or discard explicitly.");
        if(!floppyState().idle)throw std::runtime_error("Floppy write active. Finish the sampler disk operation first.");
        floppyHeld_=false;
        std::shared_ptr<host::FloppyImageBacking> fresh;
        if(!path.empty()){const auto os=osArea();fresh=std::make_shared<host::FloppyImageBacking>(path,direct,floppyBacking_&&sameImage(path,floppyBacking_->path)?floppyBacking_->lease:nullptr,os);}
        if(fresh)installFloppy(*machine_,fresh);
        else{if(machine_->diskRead){machine_->diskRead->eject(0);machine_->diskRead->persistTrack={};}
            if(machine_->floppy.drives[0])machine_->floppy.drives[0]->eject();machine_->diskDma.floppyIo=false;}
        floppyBacking_=std::move(fresh);floppyLastSaved_.clear();settings_.floppy=path;
        media_=!floppyBacking_?"No disk":mediaLabel(*floppyBacking_);
        return path.empty()?"Floppy ejected.":"Floppy media changed.";
    }
    static std::filesystem::path recoveryFolder(){
        std::filesystem::path base;
#ifdef _WIN32
        if(const char* a=std::getenv("LOCALAPPDATA"))base=a;
#endif
        if(base.empty())base=std::filesystem::temp_directory_path();
        return base/"EmulatorII-VST3"/"recovery";
    }
    static std::filesystem::path recoveryName(const std::filesystem::path& source,const std::string& ext){
        const auto t=std::time(nullptr);char stamp[32]{};std::strftime(stamp,sizeof stamp,"%Y%m%d-%H%M%S",std::localtime(&t));
        auto stem=source.empty()?std::string("untitled"):source.stem().string();
        auto dir=recoveryFolder();std::error_code e;std::filesystem::create_directories(dir,e);
        for(unsigned n=0;;++n){auto p=dir/(stem+"-unsaved-"+stamp+(n?"-"+std::to_string(n):"")+"."+ext);if(!std::filesystem::exists(p,e))return p;}
    }
    // Protected working copies would otherwise be lost when the host tears the
    // instance down or reloads a project: write them to NEW recovery files.
    std::string rescueDirtyMedia(){
        std::string note;
        try{if(floppyBacking_&&floppyBacking_->dirty&&!floppyBacking_->direct){auto p=recoveryName(floppyBacking_->path,floppyBacking_->hfe?"hfe":"emuiifd");floppyBacking_->saveCopy(p);note+="Floppy changes rescued to "+pathText(p)+"\n";}}
        catch(const std::exception& e){note+=std::string("Floppy rescue failed: ")+e.what()+"\n";}
        try{if(machine_&&machine_->hardDisk&&machine_->hardDisk->dirty&&!hddLease_){auto p=recoveryName(hddSource_,"img");host::saveNewHddImage(p,machine_->hardDisk->image);machine_->hardDisk->dirty=false;note+="HDD changes rescued to "+pathText(p)+"\n";}}
        catch(const std::exception& e){note+=std::string("HDD rescue failed: ")+e.what()+"\n";}
        return note;
    }

    // ---- boot (mirrors FrontPanelSession::Boot; concrete production startup) ----
    void bootNow(bool discard,bool fromState){
        try{
            std::string rescued;
            if(fromState)rescued=rescueDirtyMedia();
            if((hddState().dirty||floppyState().dirty)&&!discard&&!fromState)
                throw std::runtime_error("Disk images have unsaved changes. Save copies or explicitly discard before reset.");
            if(settings_.romFolder.empty())throw std::runtime_error("Select the prepared ROM folder (main.bin, scanner.bin, ic135.bin, ic137.bin).");
            const auto& c=settings_;
            auto fresh=std::make_unique<Machine>(0);
            const auto& p=c.romFolder;
            // 1 MB needs a main boot PROM with the bank-swap vector at 0FDBh (the
            // MBMEM kit PROM): 2.1 DM for floppy boot, HD boot 2.3. Plain 2.1 has
            // FFh there and the OS crashes, so floppy boot without main21dm.bin
            // runs 512 KB (docs/MEMORY_1MB.md).
            const bool hd=!c.hdd.empty();
            const bool big=c.memory1mb&&(hd||std::filesystem::exists(p/"main21dm.bin"));
            fresh->load(p/(hd?"main23.bin":big?"main21dm.bin":"main.bin"),p/"scanner.bin",p/"ic135.bin",p/"ic137.bin");
            const bool freshFastPanel=applyFastScannerPanel(fresh->scanner.rom,c.fastPanel);
            fresh->installSecondBank(big);
            fresh->initializeDramPowerOn(1);
            fresh->enableProductionController(true);
            fresh->initializeControllerPowerOn(0xe1122026u);
            std::string media="No disk";
            std::shared_ptr<host::FloppyImageBacking> freshFloppy;
            const bool holdFloppy=hd&&!c.bootFloppy;
            if(!c.floppy.empty()){
                freshFloppy=std::make_shared<host::FloppyImageBacking>(c.floppy,c.directImageWrites,
                    floppyBacking_&&sameImage(c.floppy,floppyBacking_->path)?floppyBacking_->lease:nullptr,osArea());
                // HD PROM 2.3 boots an inserted floppy before the HDD. "Hard disk first"
                // keeps the drive empty until the loaded OS runs, as if the disk were
                // inserted after power-on; advance() inserts it then.
                if(!holdFloppy)installFloppy(*fresh,freshFloppy);
                media=mediaLabel(*freshFloppy);
            }
            std::shared_ptr<host::DirectImageFile> freshLease;
            if(!c.hdd.empty()){
                fresh->installHardDisk(host::loadHddImage(c.hdd));
                if(c.directImageWrites){
                    freshLease=hddLease_&&sameImage(c.hdd,hddSource_)?hddLease_:std::make_shared<host::DirectImageFile>(c.hdd,fresh->hardDisk->image);
                    fresh->hardDisk->persistImage=[freshLease](std::span<const std::uint8_t> bytes){freshLease->persist(bytes);};
                }
            }
            for(unsigned i=0;i<6;++i)fresh->scannerControls.setVoltage(input(i),c.controls[i]);
            fresh->enableNormalRuntime();
            fresh->sampleInput=sampleSource_?sampleSource_:hostInput_;
            // Old pipeline drained, then destroyed (joins its DSP) before the new one exists.
            quiesce();
            machine_.reset();releaseDsp();
            machine_=std::move(fresh);halted_=paused_=false;
            fastPanelActive_=freshFastPanel;memory1mbActive_=big;
            hddSource_=c.hdd;hddLastSaved_.clear();hddLease_=std::move(freshLease);floppyHeld_=holdFloppy&&freshFloppy;floppyBacking_=std::move(freshFloppy);floppyLastSaved_.clear();
            gOffset_=G_;installAudio();
            midiTools_.resetInputState();midiTools_.wheelBridge=c.midiWheels;midiTools_.noteOffZero=c.noteOffZero;
            contacts_={};media_=media;error_=rescued;
            startup_="Production: phase0 / RAM seed1 / controller seed e1122026";
            recallPending_=c.recallProgram>=0;recallDeadline_=G_+120*cpuRate;recallProgram_.store(c.recallProgram);
            speedTick_=G_;speedTime_=std::chrono::steady_clock::now();speedRatio_=0;
        }catch(const std::exception& e){error_=e.what();}
    }

    // ---- input ----
    void contact(unsigned i,bool pressed){
        if(!machine_||i>=30)return;
        auto& c=contacts_[i];const bool last=c.pending.empty()?c.held:c.pending.back();
        if(pressed==last)return;
        if(c.pending.empty()&&c.tick!=machine_->ticks){machine_->scannerControls.setButton(ScannerButton(i),pressed);c.held=pressed;c.tick=machine_->ticks;}
        else c.pending.push_back(pressed); // one executing quantum per edge
    }
    void applyMidi(const std::uint8_t* bytes,unsigned size){
        if(!size||size!=midiMessageLength(bytes[0]))return;
        for(unsigned i=1;i<size;++i)if(bytes[i]>=128)return;
        if((bytes[0]&0xf0)==0xc0){recallProgram_.store(int(bytes[0])<<8|bytes[1]);settings_.recallProgram=recallProgram_.load();recallPending_=false;}
        if(!machine_)return;
        const std::span raw(bytes,size);
        const auto packet=midiTools_.adapt(raw);
        if(packet.wheel)machine_->scannerControls.setVoltage(input(packet.wheel->control),packet.wheel->volts);
        if(machine_->receiveMidi(std::span(packet.bytes.data(),size)))midiTools_.observeEnqueued(std::span(packet.bytes.data(),size));
    }
    void applyEvent(const HostEvent& e){
        if(e.kind==0)applyMidi(e.bytes.data(),e.size);
        else if(e.kind==1&&e.control<6){settings_.controls[e.control]=e.volts;
            // Slider throttle (comfort option, off by default): the edit-mode firmware
            // recomputes every voice per slider message; fewer, latest-value updates keep
            // its message queue from filling. Wheels are never throttled.
            if(settings_.sliderThrottle&&e.control<4&&(G_-throttleTick_[e.control]<throttleTicks||scannerBacklog()>2)){throttlePending_[e.control]=true;return;}
            throttleTick_[e.control]=G_;throttlePending_[e.control]=false;
            if(machine_)machine_->scannerControls.setVoltage(input(e.control),e.volts);}
    }
    static constexpr std::uint64_t throttleTicks=cpuRate/25; // the scanner samples each slider ~25x/s
    // Bytes waiting in the scanner->main message ring (scanner RAM 26Eh write / 270h read
    // pointers, SLIDER_ALLVOICES_AUDIT.md). A new slider value is held while the
    // firmware is still working through earlier ones (latest value wins).
    unsigned scannerBacklog()const noexcept{
        if(!machine_)return 0;const auto& r=machine_->scannerRam;
        const unsigned w=r[0x26e&2047]|r[0x26f&2047]<<8,rd=r[0x270&2047]|r[0x271&2047]<<8;return (w-rd)&0xff;
    }
    std::array<std::uint64_t,4> throttleTick_{};std::array<bool,4> throttlePending_{};
    void flushThrottle(){ // apply the latest held slider value once its interval has passed
        const bool busy=settings_.sliderThrottle&&scannerBacklog()>2;
        for(unsigned i=0;i<4;++i)if(throttlePending_[i]&&(!settings_.sliderThrottle||(G_-throttleTick_[i]>=throttleTicks&&!busy))){
            throttlePending_[i]=false;throttleTick_[i]=G_;if(machine_)machine_->scannerControls.setVoltage(input(i),settings_.controls[i]);}
    }
    // Firmware readiness for preset recall: LCD line 1 shows "Pnn ..." once a bank is loaded.
    bool presetDisplayed()const{
        if(!machine_||!machine_->lcd.display)return false;const auto l=machine_->lcd.line(0);
        return l.size()>=3&&l[0]=='P'&&std::isdigit(static_cast<unsigned char>(l[1]))&&std::isdigit(static_cast<unsigned char>(l[2]));
    }

    // ---- time ----
    void runTicks(std::uint64_t n){
        if(producing()){
            try{
                // Keep machine and DSP pipelined. Wait only when this whole run
                // cannot fit, rather than draining the DSP queue every quantum.
                // Reserve by block count: short MIDI runs also flush partial blocks.
                const auto waitStart=std::chrono::steady_clock::now();
                if(nonlinear())while(!dspRunFits(machine_->chipAudioStats().queuedBlocks,n)){if(quitRequested())return;dspSignal_.wait(2);}
                const auto runStart=std::chrono::steady_clock::now();
                hostInput_->bind(localAnchorEpoch_,rate_.load(),anchorTickLocal_,gOffset_,settings_.inputGain*std::pow(10.,settings_.sampleInputTrimDb/20));
                machine_->run(n);
                const auto runEnd=std::chrono::steady_clock::now();
                runSeconds_+=std::chrono::duration<double>(runEnd-runStart).count();waitSeconds_+=std::chrono::duration<double>(runStart-waitStart).count();ranTicks_+=n;
                G_=gOffset_+machine_->ticks;
                for(unsigned i=0;i<30;++i){auto& c=contacts_[i];
                    // Queued edges keep >= 5 ms of emulated contact time, as with the
                    // standalone's 20000-tick quanta, although runs are now shorter.
                    if(!c.pending.empty()&&machine_->ticks>=c.tick+20000){c.held=c.pending.front();c.pending.pop_front();c.tick=machine_->ticks;machine_->scannerControls.setButton(ScannerButton(i),c.held);}}
                if(recallPending_){
                    if(presetDisplayed()){const int pc=recallProgram_.load();recallPending_=false;if(pc>=0){const std::uint8_t m[2]{std::uint8_t(pc>>8),std::uint8_t(pc&0x7f)};applyMidi(m,2);}}
                    else if(G_>recallDeadline_)recallPending_=false;
                }
            }catch(const std::exception& e){G_=gOffset_+machine_->ticks;halt(e.what());}
        }else{
            const auto before=expectedFrames(G_);G_+=n;ring_.pushSilence(expectedFrames(G_)-before);
        }
    }
    bool quitRequested(){std::lock_guard l(mutex_);return quit_;}
    // Runs at most one quantum toward the host target; returns true if behind.
    bool advance(){
        const auto target=tickOf(hostTarget_.load(std::memory_order_acquire));
        for(;;){
            const HostEvent* e=events_.front();if(!e)break;
            const auto t=e->epoch==localAnchorEpoch_?tickOf(e->pos):G_;
            if(t>G_)break;
            applyEvent(*e);events_.pop();
        }
        flushThrottle();
        if(floppyHeld_&&machine_&&machine_->main.cpu.pc>=0x2800){floppyHeld_=false;if(floppyBacking_)installFloppy(*machine_,floppyBacking_);}
        // Realtime recovery after a long stall (e.g. the host process was starved):
        // once the machine is more than 100 ms behind the playback point, frames it
        // would render are already late. Freeze emulated time over the gap (like the
        // standalone pacer's rebase) instead of playing catch-up in silence.
        // A live ADC/SAR conversion must consume every host input sample.
        // Freezing machine time while jumping its host-input mapping splices
        // audio into the feedback loop and recorded DRAM. Permit recovery only
        // outside actual sampling/monitor mode; retained input can catch up.
        const auto inputMode=machine_?machine_->channel0Mode():LogicLevel{};
        const bool sampling=producing()&&inputMode.known&&inputMode.high;
        if(!offline_.load(std::memory_order_relaxed)&&!sampling){
            const std::uint64_t latencyTicks=std::uint64_t(latency_.load())*cpuRate/rate_.load();
            if(target>G_+latencyTicks+cpuRate/10){
                quiesce();const auto newG=target-latencyTicks,before=expectedFrames(G_);
                gOffset_+=newG-G_;G_=newG;ring_.pushSilence(expectedFrames(G_)-before);restartPipe();
                rebases_.fetch_add(1,std::memory_order_relaxed);
            }
        }
        if(!producing()){hostInput_->bind(localAnchorEpoch_,rate_.load(),anchorTickLocal_,gOffset_,settings_.inputGain*std::pow(10.,settings_.sampleInputTrimDb/20));hostInput_->discard(hostTarget_.load());}
        if(G_>=target)return false;
        // SSM2045 models: whole 96-frame DSP blocks (8000 ticks) per run, so only the
        // run that ends at the host target flushes a partial block (each block copy
        // moves ~200 KB of pin snapshots through the worker queue).
        // Machine::run has a per-call cost (measured ~25 % load at 1 ms runs), so
        // runs are 5-6 ms while the lead is healthy and 1-2 ms once fewer than
        // half the margin's frames are ready (after a stall: late frames must
        // reach the ring after 1-2 ms of work, not a whole long run).
        const bool dsp=nonlinear();
        const auto ready=std::int64_t(ring_.written())-std::int64_t(readIndex_.load(std::memory_order_relaxed));
        const bool urgent=!offline_.load(std::memory_order_relaxed)&&ready<std::int64_t(marginMs_.load(std::memory_order_relaxed))*48/2;
        const std::uint64_t step=urgent?(dsp?dspBlockTicks:fineQuantum):(dsp?3*dspBlockTicks:quantum);
        auto limit=std::min(target,G_+step);
        if(const HostEvent* e=events_.front();e&&e->epoch==localAnchorEpoch_)limit=std::min(limit,std::max(G_+1,tickOf(e->pos)));
        runTicks(limit-G_);
        return G_<target;
    }
    void publish(bool force){
        const auto now=std::chrono::steady_clock::now();
        if(!force&&now-published_<std::chrono::milliseconds(40))return;
        published_=now;
        const double elapsed=std::chrono::duration<double>(now-speedTime_).count();
        if(elapsed>=1){speedRatio_=double(G_-speedTick_)/double(cpuRate)/elapsed;speedTick_=G_;speedTime_=now;
            if(ranTicks_){const double emu=double(ranTicks_)/cpuRate;machineLoad_=runSeconds_/emu;dspWait_=waitSeconds_/emu;}runSeconds_=waitSeconds_=0;ranTicks_=0;}
        EngineStatus s;auto& next=s.panel;
        next.hdd=hddState();next.floppy=floppyState();next.revision=++revision_;next.error=error_;next.startup=startup_;next.media=media_;
        next.running=producing();next.speedRatio=speedRatio_;
        if(machine_){
            const auto& m=*machine_;
            s.sampleReads=m.sampleReads;s.sampleUnknownReads=m.sampleUnknownReads;s.sampleRailClips=m.sampleAnalog.railClips;
            next.loaded=true;next.reconstruction=m.reconstructionEnabled();next.nativeAudio=m.nativeAudioEnabled();next.audioMode=m.runtimeAudioMode();next.dsp=m.chipAudioStats();
            next.ticks=m.ticks;next.lcd={m.lcd.line(0),m.lcd.line(1)};next.lcdOn=m.lcd.display;next.leds=m.scannerControls.leds;
            next.midiReceived=m.midiSerial.received;next.midiRead=m.midiSerial.reads;next.midiOverruns=m.midiSerial.overruns;next.midiDropped=m.midiWire.dropped;
            for(unsigned i=0;i<6;++i)next.controls[i]=m.scannerControls.adc.inputs[unsigned(input(i))];
            if(m.floppy.drives[0]){const auto& d=*m.floppy.drives[0];next.cylinder=d.cylinder;next.selected=d.selected();next.spinning=d.spinning();}
        }
        s.callbackMaxGapMs=callbackMaxGapUs_.load(std::memory_order_relaxed)/1000.0;s.callbacks=callbackCount_.load(std::memory_order_relaxed);
        const auto hostTick=tickOf(hostTarget_.load(std::memory_order_acquire));s.ownerLagMs=hostTick>G_?double(hostTick-G_)/4000.0:0;
        s.ownerMmcss=ownerMmcss_;s.ownerHighQos=ownerHighQos_;s.highResolutionWait=wake_.highResolution();
        s.fastPanelActive=fastPanelActive_;s.memory1mbActive=memory1mbActive_;
        s.machineLoad=machineLoad_;s.dspWait=dspWait_;s.underruns=underruns_.load();s.ringOverflows=ring_.overflows();s.framesWritten=ring_.written();s.outputPeak=outputPeak_.load();s.eventDrops=eventDrops_.load();s.dspDrops=next.dsp.dropped;
        s.hostRate=rate_.load();s.latency=latency_.load();s.anchored=localAnchorEpoch_!=0&&localAnchorEpoch_==epoch_.load();s.paused=paused_;s.halted=halted_;
        s.inputTrimDb=settings_.sampleInputTrimDb;
        s.inputFrames=hostInput_->frames.load();s.inputDrops=hostInput_->drops.load();s.inputMissing=hostInput_->missing.load();
        s.recallProgram=recallProgram_.load();s.recallPending=recallPending_;
        if(samplingTrace_.recording()){
        SamplingTrace::Row trace{};
        trace[0]=double(std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());
        trace[1]=localAnchorEpoch_;trace[2]=s.callbacks;trace[3]=hostTarget_.load();trace[4]=next.ticks;trace[5]=G_;trace[6]=gOffset_;trace[7]=unsigned(settings_.audioMode);trace[8]=trace[9]=-1;
        trace[10]=hostInput_->peak.load(std::memory_order_relaxed);trace[11]=s.inputFrames;trace[12]=s.inputDrops;trace[13]=s.inputMissing;trace[14]=s.inputTrimDb;
        if(machine_){const auto& m=*machine_;const auto mode=m.channel0Mode(),g1=m.scannerPioBPin(7),g2=m.scannerPioBPin(6);
            trace[8]=mode.known?int(mode.high):-1;trace[9]=g1.known&&g2.known?(int(g1.high)*2+int(g2.high)):-1;
            trace[15]=m.sampleAnalog.bufferVolts();const auto a=m.sampleAnalog.iint(),b=m.sampleAnalog.held(),c=m.sampleAnalog.referenceCv();
            trace[16]=a.value;trace[17]=b.value;trace[18]=c.value;trace[19]=unsigned(a.known)|(unsigned(b.known)<<1)|(unsigned(c.known)<<2);
            trace[20]=m.controlVoltage.held[ControlVoltageBus::Amplitude][0].volts;trace[21]=m.controlVoltage.held[ControlVoltageBus::Frequency][0].volts;trace[22]=m.controlVoltage.held[ControlVoltageBus::Resonance][0].volts;}
        trace[23]=s.sampleReads;trace[24]=s.sampleUnknownReads;trace[25]=s.sampleRailClips;trace[26]=rebases_.load();trace[27]=s.underruns;trace[28]=s.dspDrops;trace[29]=s.machineLoad;trace[30]=next.dsp.load;trace[31]=s.ownerLagMs;
        if(machine_){trace[32]=machine_->nativeAudioOverflows();trace[33]=machine_->nativeAudioOrderingErrors();}
        {auto& g=eii::gui::stats;trace[34]=double(g.moves.load(std::memory_order_relaxed));trace[35]=double(g.voltages.load(std::memory_order_relaxed));trace[36]=double(g.paints.load(std::memory_order_relaxed));
         trace[37]=g.paintUs.load(std::memory_order_relaxed)/1000.0;trace[38]=g.paintMaxUs.exchange(0,std::memory_order_relaxed)/1000.0;trace[39]=g.moveUs.load(std::memory_order_relaxed)/1000.0;trace[40]=g.moveMaxUs.exchange(0,std::memory_order_relaxed)/1000.0;}
        if(machine_){const auto& m=*machine_;trace[41]=m.scannerControls.adc.inputs[unsigned(eii::ScannerAnalogInput::SliderA)].volts;
         const auto l=m.lcd.line(0)+m.lcd.line(1);std::uint32_t h=2166136261u;for(unsigned char ch:l)h=(h^ch)*16777619u;trace[42]=h%1000000;
         const unsigned w=m.scannerRam[0x26e&2047]|m.scannerRam[0x26f&2047]<<8,r=m.scannerRam[0x270&2047]|m.scannerRam[0x271&2047]<<8;trace[43]=int((w-r)&0xff);}
        samplingTrace_.push(trace);
        }
        std::lock_guard l(statusMutex_);status_=std::move(s);
    }
    void loop()noexcept{
        host::nameCurrentThread(L"EII VST3 machine");
#ifdef _WIN32
        // Do not promote the machine above its DSP dependency or host callback.
#endif
        // Both plug-in owners use the same MMCSS class/relative priority.
        // The DSP must not sit below the machine thread it is feeding.
        const host::AudioThreadSchedule scheduling;
        ownerMmcss_=scheduling.multimedia();ownerHighQos_=scheduling.highQos();
        host::placeCurrentThread(host::CpuRole::Machine);
#ifdef _WIN32
        if(CorePool::enabled())machineClaim_=CorePool::place(GetCurrentThread());
#endif
        for(;;){
            std::deque<std::function<void()>> pending;
            {std::lock_guard l(mutex_);if(quit_)break;pending.swap(commands_);}
            const bool commanded=!pending.empty();
            for(auto& f:pending)try{f();}catch(const std::exception& e){error_=e.what();}
            const auto epoch=epoch_.load(std::memory_order_acquire);
            if(epoch!=localAnchorEpoch_){
                anchorTickLocal_=G_;anchorFrame_.store(expectedFrames(G_),std::memory_order_relaxed);anchorTick_.store(G_,std::memory_order_relaxed);
                localAnchorEpoch_=epoch;anchorEpoch_.store(epoch,std::memory_order_release);anchorSignal_.set();
            }
            bool behind=false;
            try{behind=advance();}catch(const std::exception& e){halt(e.what());}
            publish(commanded);
            if(!behind)wake_.wait(20);
        }
        try{rescueDirtyMedia();}catch(...){}
        machine_.reset(); // joins the DSP worker
        releaseDsp();
#ifdef _WIN32
        CorePool::release(machineClaim_);
#endif
    }
};
}
