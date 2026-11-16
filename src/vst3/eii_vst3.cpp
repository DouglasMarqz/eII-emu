// Emulator II VST3 instrument (Instrument|Sampler, stereo out, MIDI in).
// Written directly against pluginterfaces: one COM object is both the audio
// component and (through a nested facet) the edit controller, like
// SingleComponentEffect but without VSTGUI/base. All machine work happens on
// the PluginEngine owner thread; process() never runs a Z80 and never blocks
// in realtime mode.
#define NOMINMAX
// clang-format off
#include <windows.h>
// clang-format on
#include "vst3/plugin_editor.h"
#include "vst3/eii_vst3_ids.h"
#include "vst3/post_output_gain.h"
#include "vst3/panel_control_fallback.h"
#include "windows_panel_preferences.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/vsttypes.h"
#include <cstring>
#include <mutex>
#include <chrono>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {
HINSTANCE dllInstance=nullptr;
std::mutex gdiMutex;ULONG_PTR gdiToken=0; // module-wide, not per instance: see ensureGdiplus
std::atomic<int> liveObjects{0};

void copyString(String128 dst,const char16_t* src){int i=0;for(;src[i]&&i<127;++i)dst[i]=TChar(src[i]);dst[i]=0;}
void copyAscii(String128 dst,const char* src){int i=0;for(;src[i]&&i<127;++i)dst[i]=TChar(src[i]);dst[i]=0;}
constexpr unsigned controlCount=6,volumeId=6,postOutputId=7,realParams=8,midiBase=1000,midiPerChannel=131; // CC0..127, aftertouch, bend, program
constexpr unsigned auxOutputs=8; // individual outputs J1..J8 (sheet 7-52)
constexpr unsigned midiParams=16*midiPerChannel;

class Plugin;
// Edit-controller facet. IComponent::setState and IEditController::setState
// share one C++ signature, so the controller lives in its own vtable whose
// identity/refcount delegate to the component.
class ControllerFacet final : public IEditController, public IMidiMapping {
public:
    explicit ControllerFacet(Plugin& p):p_(p){}
    tresult PLUGIN_API queryInterface(const TUID iid,void** obj)override;
    uint32 PLUGIN_API addRef()override;
    uint32 PLUGIN_API release()override;
    tresult PLUGIN_API initialize(FUnknown* context)override;
    tresult PLUGIN_API terminate()override;
    tresult PLUGIN_API setComponentState(IBStream*)override{return kResultOk;} // same object: already applied
    tresult PLUGIN_API setState(IBStream*)override{return kResultOk;}          // no separate controller state
    tresult PLUGIN_API getState(IBStream*)override{return kResultOk;}
    int32 PLUGIN_API getParameterCount()override{return int32(realParams+midiParams);}
    tresult PLUGIN_API getParameterInfo(int32 index,ParameterInfo& info)override;
    tresult PLUGIN_API getParamStringByValue(ParamID id,ParamValue v,String128 out)override;
    tresult PLUGIN_API getParamValueByString(ParamID id,TChar* text,ParamValue& v)override;
    ParamValue PLUGIN_API normalizedParamToPlain(ParamID id,ParamValue v)override{return id<controlCount?v*5.0:v;}
    ParamValue PLUGIN_API plainParamToNormalized(ParamID id,ParamValue v)override{return id<controlCount?v/5.0:v;}
    ParamValue PLUGIN_API getParamNormalized(ParamID id)override;
    tresult PLUGIN_API setParamNormalized(ParamID id,ParamValue v)override;
    tresult PLUGIN_API setComponentHandler(IComponentHandler* handler)override;
    IPlugView* PLUGIN_API createView(FIDString name)override;
    tresult PLUGIN_API getMidiControllerAssignment(int32 bus,int16 channel,CtrlNumber cc,ParamID& id)override{
        if(bus!=0||channel<0||channel>15||cc<0||cc>=int32(midiPerChannel))return kResultFalse;
        id=midiBase+ParamID(channel)*midiPerChannel+ParamID(cc);return kResultTrue;
    }
private:
    Plugin& p_;
};

class Plugin final : public IComponent, public IAudioProcessor, public IProcessContextRequirements, public eii::vst::IEiiDiagnostics, public eii::vst::EditorBackend {
public:
    Plugin():controller_(*this){++liveObjects;for(unsigned i=0;i<realParams;++i)params_[i].store(defaultValue(i));}
    ~Plugin()override{setHandler(nullptr);engine_.reset();--liveObjects;}
    static double defaultValue(unsigned id){return id==4?0.5:id==volumeId?1.0:0.0;}
    // ---- FUnknown ----
    tresult PLUGIN_API queryInterface(const TUID iid,void** obj)override{
        auto is=[&](const TUID other){return FUnknownPrivate::iidEqual(iid,other);};
        if(is(FUnknown::iid)||is(IPluginBase::iid)||is(IComponent::iid)){*obj=static_cast<IComponent*>(this);}
        else if(is(IAudioProcessor::iid))*obj=static_cast<IAudioProcessor*>(this);
        else if(is(IProcessContextRequirements::iid))*obj=static_cast<IProcessContextRequirements*>(this);
        else if(is(IEditController::iid))*obj=static_cast<IEditController*>(&controller_);
        else if(is(IMidiMapping::iid))*obj=static_cast<IMidiMapping*>(&controller_);
        else if(is(eii::vst::IEiiDiagnostics::iid))*obj=static_cast<eii::vst::IEiiDiagnostics*>(this);
        else {*obj=nullptr;return kNoInterface;}
        addRef();return kResultOk;
    }
    uint32 PLUGIN_API addRef()override{return ++refs_;}
    uint32 PLUGIN_API release()override{const auto n=--refs_;if(!n)delete this;return n;}
    // ---- IPluginBase (shared by both facets; idempotent) ----
    tresult PLUGIN_API initialize(FUnknown*)override{
        if(initialized_++)return kResultOk;
        try{freshDefaults();engine_=std::make_unique<eii::vst::PluginEngine>();engine_->setupProcessing(48000,1024);engine_->setOutputGain(float(params_[volumeId].load()));engine_->configure(settings());engine_->setMarginMs(settings().marginMs);}
        catch(...){--initialized_;return kResultFalse;}
        return kResultOk;
    }
    tresult PLUGIN_API terminate()override{
        if(initialized_==0)return kResultOk;
        if(--initialized_==0){if(engine_)eii::vst::savePluginDefaults(settings());setHandler(nullptr);engine_.reset();} // joins machine + DSP threads
        return kResultOk;
    }
    // ---- IComponent ----
    tresult PLUGIN_API getControllerClassId(TUID)override{return kNotImplemented;} // single component
    tresult PLUGIN_API setIoMode(IoMode)override{return kNotImplemented;}
    int32 PLUGIN_API getBusCount(MediaType type,BusDirection dir)override{
        // Optional mono/stereo host sampling input. Outputs: main (MIX OUT J9,
        // stereo) plus the 8 individual channel outputs J1..J8 (sheet 7-52) as
        // mono aux buses, inactive by default.
        if(type==kAudio&&dir==kOutput)return 1+auxOutputs;if(type==kAudio&&dir==kInput)return 1;if(type==kEvent&&dir==kInput)return 1;return 0;
    }
    tresult PLUGIN_API getBusInfo(MediaType type,BusDirection dir,int32 index,BusInfo& bus)override{
        if(index<0||index>=getBusCount(type,dir))return kInvalidArgument;
        bus.mediaType=type;bus.direction=dir;bus.busType=kMain;bus.flags=BusInfo::kDefaultActive;
        if(type==kAudio&&dir==kOutput&&index>0){ // J1..J8: CnOUT.V of firmware channel 8-index through R156..R163
            bus.busType=kAux;bus.flags=0;bus.channelCount=1;char name[32];std::snprintf(name,sizeof name,"J%d Out (ch %d)",int(index),int(8-index));copyAscii(bus.name,name);return kResultOk;}
        if(type==kAudio){bus.channelCount=dir==kInput?inputChannels_.load():2;copyAscii(bus.name,dir==kInput?"Sampling In":"Main Out (Mix J9)");}else{bus.channelCount=16;copyAscii(bus.name,"MIDI In");}
        return kResultOk;
    }
    tresult PLUGIN_API getRoutingInfo(RoutingInfo&,RoutingInfo&)override{return kNotImplemented;}
    tresult PLUGIN_API activateBus(MediaType type,BusDirection dir,int32 index,TBool state)override{
        if(index<0||index>=getBusCount(type,dir))return kInvalidArgument;if(type==kAudio&&dir==kInput)inputActive_.store(state!=0);
        if(type==kAudio&&dir==kOutput&&index>0){const unsigned bit=1u<<unsigned(index-1);const unsigned m=state?(auxActive_.fetch_or(bit)|bit):(auxActive_.fetch_and(~bit)&~bit);if(engine_)engine_->channelOutputs(m!=0);}
        return kResultTrue;}
    tresult PLUGIN_API setActive(TBool state)override{
        if(!engine_)return kNotInitialized;
        if(state){
            engine_->activate();
            // A new instance (no project state) boots its standalone defaults on first activation;
            // plug-in scans that never activate do not start the machine.
            if(!stateLoaded_&&!bootRequested_){const auto s=settings();if(s.autoBoot&&!s.romFolder.empty())engine_->boot(s,false);bootRequested_=true;}
        }
        return kResultOk;
    }
    tresult PLUGIN_API setState(IBStream* stream)override{
        if(!stream||!engine_)return kInvalidArgument;
        std::string text;char buffer[4096];
        for(;;){int32 got=0;if(stream->read(buffer,sizeof buffer,&got)!=kResultOk||got<=0)break;text.append(buffer,std::size_t(got));if(text.size()>(1u<<20))return kResultFalse;}
        eii::vst::PluginSettings s;
        try{s=eii::vst::parseSettings(text);}catch(...){return kResultFalse;}
        {std::lock_guard l(settingsMutex_);settings_=s;}
        stateLoaded_=bootRequested_=true;
        applyMargin(s.marginMs);
        for(unsigned i=0;i<controlCount;++i)params_[i].store(s.controls[i]/5.0);
        params_[postOutputId].store(s.postOutputDb/eii::vst::postOutputMaxDb);params_[volumeId].store(s.volume);engine_->setOutputGain(float(s.volume));
        if(s.autoBoot&&!s.romFolder.empty())engine_->boot(s,false,true);else engine_->configure(s);
        return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream* stream)override{
        if(!stream)return kInvalidArgument;
        const auto text=eii::vst::serializeSettings(settings());
        int32 written=0;return stream->write(const_cast<char*>(text.data()),int32(text.size()),&written)==kResultOk&&written==int32(text.size())?kResultOk:kResultFalse;
    }
    // ---- IAudioProcessor ----
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* ins,int32 numIns,SpeakerArrangement* outs,int32 numOuts)override{
        if(numOuts<1||numOuts>int32(1+auxOutputs)||!outs||outs[0]!=SpeakerArr::kStereo||numIns<0||numIns>1)return kResultFalse;
        for(int32 i=1;i<numOuts;++i)if(outs[i]!=SpeakerArr::kMono)return kResultFalse;
        if(numIns==1&&(!ins||(ins[0]!=SpeakerArr::kMono&&ins[0]!=SpeakerArr::kStereo)))return kResultFalse;
        inputChannels_.store(numIns==1&&ins[0]==SpeakerArr::kMono?1:2);inputActive_.store(numIns==1);return kResultTrue;
    }
    tresult PLUGIN_API getBusArrangement(BusDirection dir,int32 index,SpeakerArrangement& arr)override{
        if(index<0||(dir!=kInput&&dir!=kOutput)||index>=getBusCount(kAudio,dir))return kInvalidArgument;
        arr=(dir==kInput&&inputChannels_.load()==1)||(dir==kOutput&&index>0)?SpeakerArr::kMono:SpeakerArr::kStereo;return kResultOk;
    }
    tresult PLUGIN_API canProcessSampleSize(int32 size)override{return size==kSample32?kResultTrue:kResultFalse;}
    uint32 PLUGIN_API getLatencySamples()override{return engine_?engine_->latencySamples():0;}
    tresult PLUGIN_API setupProcessing(ProcessSetup& setup)override{
        if(!engine_)return kNotInitialized;if(setup.symbolicSampleSize!=kSample32)return kResultFalse;
        engine_->setupProcessing(setup.sampleRate,setup.maxSamplesPerBlock);return kResultOk;
    }
    tresult PLUGIN_API setProcessing(TBool)override{return kResultOk;}
    // The sampler is a free-running machine: keep being processed.
    uint32 PLUGIN_API getTailSamples()override{return kInfiniteTail;}
    uint32 PLUGIN_API getProcessContextRequirements()override{return 0;}
    tresult PLUGIN_API process(ProcessData& data)override;
    // ---- diagnostics (private interface for the headless host test) ----
    int32 PLUGIN_API statusText(char* buffer,int32 size)override{
        if(!engine_||!buffer||size<=0)return 0;
        const auto s=engine_->status();std::ostringstream o;
        o<<"lcd0="<<s.panel.lcd[0]<<"\nlcd1="<<s.panel.lcd[1]<<"\nloaded="<<s.panel.loaded<<"\nrunning="<<s.panel.running<<"\nticks="<<s.panel.ticks
         <<"\ninputframes="<<s.inputFrames<<"\ninputdrops="<<s.inputDrops<<"\ninputmissing="<<s.inputMissing<<"\nunderruns="<<s.underruns<<"\neventdrops="<<s.eventDrops<<"\ndspdrops="<<s.dspDrops<<"\nlatency="<<s.latency<<"\nanchored="<<s.anchored
         <<"\nrecall="<<s.recallProgram<<"\nrecallpending="<<s.recallPending<<"\nmidireceived="<<s.panel.midiReceived<<"\nmidiread="<<s.panel.midiRead<<"\nminlead="<<engine_->minLead()<<"\nrebases="<<engine_->rebases()<<"\nmachineload="<<s.machineLoad<<"\ndspwait="<<s.dspWait<<"\ndspload="<<s.panel.dsp.load<<"\ntracerecording="<<engine_->samplingTraceRecording()<<"\ncallbackmaxgapms="<<s.callbackMaxGapMs<<"\ncallbacks="<<s.callbacks<<"\nownerlagms="<<s.ownerLagMs<<"\nownermmcss="<<s.ownerMmcss<<"\nownerhighqos="<<s.ownerHighQos<<"\nhighreswait="<<s.highResolutionWait<<"\nframes="<<s.framesWritten<<"\npeak="<<s.outputPeak<<"\nerror="<<s.panel.error
         <<"\nappliedmodel="<<eii::vst::audioModeIndex(s.panel.audioMode)<<"\nappliednative="<<s.panel.nativeAudio<<"\napplieds3528="<<s.panel.reconstruction<<"\n";
        const auto t=o.str();const auto n=std::min<std::size_t>(t.size(),std::size_t(size-1));std::memcpy(buffer,t.data(),n);buffer[n]=0;return int32(n);
    }
    // ---- EditorBackend ----
    eii::vst::PluginEngine& engine()override{return *engine_;}
    bool hasEngine()override{return bool(engine_);}
    eii::vst::PluginSettings settings()override{
        std::lock_guard l(settingsMutex_);auto s=settings_;
        for(unsigned i=0;i<controlCount;++i)s.controls[i]=std::clamp(params_[i].load()*5.0,0.0,5.0);
        s.postOutputDb=eii::vst::postOutputMaxDb*params_[postOutputId].load();s.volume=params_[volumeId].load();if(engine_)s.recallProgram=engine_->recallProgram();return s;
    }
    void updateSettings(const std::function<void(eii::vst::PluginSettings&)>& change)override{
        eii::vst::PluginSettings s;{std::lock_guard l(settingsMutex_);change(settings_);s=settings_;}
        if(engine_)engine_->configure(settings());
        applyMargin(s.marginMs);
        setDirty();
        eii::vst::savePluginDefaults(settings()); // what was ticked is what the next instance gets
    }
    bool saveDefaults()override{return eii::vst::savePluginDefaults(settings());}
    void editParameter(unsigned id,double v)override{
        v=std::clamp(v,0.0,1.0);params_[id].store(v);if(id==volumeId&&engine_)engine_->setOutputGain(float(v));
        if(id<controlCount)uiEditNs_[id].store(steadyNs(),std::memory_order_relaxed);
        if(auto* h=handler()){if(editing_!=int(id)){endParameterEdit();h->beginEdit(id);editing_=int(id);}h->performEdit(id,v);}
    }
    void endParameterEdit()override{if(editing_>=0){if(auto* h=handler())h->endEdit(ParamID(editing_));editing_=-1;}}
    double parameter(unsigned id)override{return id<realParams?params_[id].load():0;}
    void rememberEditorSize(int w,int h)override{
        {std::lock_guard l(settingsMutex_);if(settings_.editorWidth==w&&settings_.editorHeight==h)return;settings_.editorWidth=w;settings_.editorHeight=h;}
        setDirty(); // host must refresh cached project state (including editor zoom)
    }
    void retain()override{refs_.fetch_add(1);}
    void drop()override{static_cast<IComponent*>(this)->release();}
    // used by the controller facet
    std::array<std::atomic<double>,realParams> params_;
    void setHandler(IComponentHandler* h){std::lock_guard l(handlerMutex_);if(h)h->addRef();if(handler_)handler_->release();handler_=h;}
    IComponentHandler* handler(){std::lock_guard l(handlerMutex_);return handler_;}
    ControllerFacet controller_;
private:
    void applyMargin(int ms){
        if(!engine_)return;const auto before=engine_->latencySamples();engine_->setMarginMs(ms);
        if(engine_->latencySamples()!=before)if(auto* h=handler())h->restartComponent(kLatencyChanged);
    }
    void setDirty(){if(auto* h=handler()){IComponentHandler2* h2=nullptr;if(h->queryInterface(IComponentHandler2::iid,reinterpret_cast<void**>(&h2))==kResultOk&&h2){h2->setDirty(true);h2->release();}}}
    eii::vst::PostOutputGain postOutput_; // process thread only
    std::array<eii::vst::PostOutputGain,auxOutputs/2> auxPost_; // individual outputs, process thread only
    std::atomic<unsigned> auxActive_{0};
    eii::vst::PanelControlFallback panelControls_; // process thread only
    // Echo suppression for editor-owned panel controls. Some hosts queue the
    // performEdit stream and play it back over later blocks; applying those
    // echoes replayed every past slider position after release. While the
    // editor moved a control within 3 s (or the host is still draining
    // echoes, <0.5 s apart), host points for it are dropped and the latest
    // editor value is handed over once per block (latest wins).
    static std::int64_t steadyNs()noexcept{return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
    std::array<std::atomic<std::int64_t>,controlCount> uiEditNs_{};
    std::array<std::int64_t,controlCount> echoNs_{}; // process thread only
    std::uint64_t echoesDropped_=0; // process thread only
    std::atomic<int> inputChannels_{2};std::atomic<bool> inputActive_{true};
    std::atomic<uint32> refs_{1};int initialized_=0;int editing_=-1;std::atomic<bool> stateLoaded_{false},bootRequested_{false};
    // Precedence (docs/OUTPUT_STAGE.md, settings): a project state (setState)
    // replaces every setting; these global defaults (standalone registry
    // preferences + global Filter Service profile) apply only to a new instance
    // that has no project state. Nothing global overrides a loaded project.
    // Fresh instances start from the standalone's per-user preferences (read only):
    // ROM folder, floppy/HDD, chip model, options, calibration, panel controls.
    // Direct image writes always start off (protected working copy).
    void freshDefaults(){
        // The plug-in's own saved defaults first (last settings used in any instance).
        // Direct image writes still start off: a protected working copy is the safe default.
        {eii::vst::PluginSettings d;if(eii::vst::loadPluginDefaults(d)){
            d.directImageWrites=false;
            for(unsigned i=0;i<controlCount;++i)params_[i].store(d.controls[i]/5.0);
            params_[volumeId].store(d.volume);params_[postOutputId].store(d.postOutputDb/eii::vst::postOutputMaxDb);
            std::lock_guard l(settingsMutex_);settings_=d;return;}}
        // First run only: seed from the standalone's per-user preferences.
        eii::host::WindowsPanelPreferences p;p.load();eii::vst::PluginSettings s;
        s.romFolder=p.romFolder;s.floppy=p.disk;s.hdd=p.hddImage;s.audioMode=eii::vst::audioModeFromIndex(p.chipModel);s.audioMode=eii::analog::RuntimeAudioMode::EiiiCustom;
        s.nativeAudio=p.nativeAudio;s.reconstruction=p.reconstruction;s.midiWheels=p.midiWheels;s.noteOffZero=p.noteOffZero;
        if(p.calibrationError.empty())s.calibration=p.calibration;
        // Only new-instance defaults. A later host setState replaces these values.
        // Invalid/unreadable global data leaves the existing startup defaults intact.
        try{if(const auto global=eii::host::loadGlobalFilterService()){s.calibration=global->ssm;s.legacyCalibration=global->legacy;if(global->hasEiii)s.eiiiCalibration=global->eiii;if(global->hasSsm2)s.ssm2Calibration=global->ssm2;}}catch(const std::exception&){}

        for(unsigned i=0;i<controlCount;++i){s.controls[i]=std::clamp(double(p.sliders[i])/200.0,0.0,5.0);params_[i].store(s.controls[i]/5.0);}
        s.volume=std::clamp(double(p.outputVolume)/1000.0,0.0,1.0);params_[volumeId].store(s.volume);s.inputGain=std::clamp(double(p.inputGain)/1000.0,0.0,1.0);
        std::lock_guard l(settingsMutex_);settings_=s;
    }
    std::unique_ptr<eii::vst::PluginEngine> engine_;
    std::mutex settingsMutex_;eii::vst::PluginSettings settings_;
    std::mutex handlerMutex_;IComponentHandler* handler_=nullptr;
};

struct Pending{int32 offset;std::uint8_t kind,size;std::array<std::uint8_t,3> bytes;unsigned control;double volts;};

tresult PLUGIN_API Plugin::process(ProcessData& data){
    if(!engine_)return kNotInitialized;
    // Gather host events (MIDI + automation) for this block, ordered by sample offset.
    std::array<Pending,1024> pending;unsigned used=0;
    std::array<bool,controlCount> hostControlPoints{};
    auto add=[&](const Pending& p){if(used<pending.size())pending[used++]=p;};
    if(auto* changes=data.inputParameterChanges){
        const int32 count=changes->getParameterCount();
        for(int32 i=0;i<count;++i){
            auto* queue=changes->getParameterData(i);if(!queue)continue;
            const ParamID id=queue->getParameterId();const int32 points=queue->getPointCount();
            for(int32 k=0;k<points;++k){
                int32 offset=0;ParamValue v=0;if(queue->getPoint(k,offset,v)!=kResultOk)continue;v=std::clamp(v,0.0,1.0);
                if(id<controlCount){
                    const auto ui=uiEditNs_[id].load(std::memory_order_relaxed);
                    if(ui){const auto now=steadyNs();if(now-ui<3000000000LL||now-echoNs_[id]<500000000LL){echoNs_[id]=now;++echoesDropped_;continue;}}
                    params_[id].store(v);hostControlPoints[id]=true;add({offset,1,0,{},id,v*5.0});}
                else if(id==volumeId){params_[id].store(v);engine_->setOutputGain(float(v));}
                else if(id==postOutputId)params_[id].store(v);
                else if(id>=midiBase&&id<midiBase+midiParams){
                    const unsigned ch=(id-midiBase)/midiPerChannel,cc=(id-midiBase)%midiPerChannel;
                    if(cc<128)add({offset,0,3,{std::uint8_t(0xb0|ch),std::uint8_t(cc),std::uint8_t(std::lround(v*127))},0,0});
                    else if(cc==kAfterTouch)add({offset,0,2,{std::uint8_t(0xd0|ch),std::uint8_t(std::lround(v*127)),0},0,0});
                    else if(cc==kPitchBend){const auto b=unsigned(std::lround(v*16383));add({offset,0,3,{std::uint8_t(0xe0|ch),std::uint8_t(b&0x7f),std::uint8_t(b>>7)},0,0});}
                    else add({offset,0,2,{std::uint8_t(0xc0|ch),std::uint8_t(std::lround(v*127)),0},0,0}); // kCtrlProgramChange
                }
            }
        }
    }
    if(auto* events=data.inputEvents){
        const int32 count=events->getEventCount();
        for(int32 i=0;i<count;++i){
            Event e{};if(events->getEvent(i,e)!=kResultOk)continue;
            auto vel=[](float v,bool on){int x=int(std::lround(std::clamp(v,0.f,1.f)*127));return std::uint8_t(on?std::max(1,x):x);};
            switch(e.type){
            case Event::kNoteOnEvent:{const auto ch=std::uint8_t(e.noteOn.channel&15);const auto key=std::uint8_t(std::clamp<int>(e.noteOn.pitch,0,127));
                add({e.sampleOffset,0,3,{std::uint8_t(0x90|ch),key,vel(e.noteOn.velocity,true)},0,0});break;}
            case Event::kNoteOffEvent:{const auto ch=std::uint8_t(e.noteOff.channel&15);const auto key=std::uint8_t(std::clamp<int>(e.noteOff.pitch,0,127));
                add({e.sampleOffset,0,3,{std::uint8_t(0x80|ch),key,vel(e.noteOff.velocity,false)},0,0});break;}
            case Event::kPolyPressureEvent:{const auto ch=std::uint8_t(e.polyPressure.channel&15);
                add({e.sampleOffset,0,3,{std::uint8_t(0xa0|ch),std::uint8_t(std::clamp<int>(e.polyPressure.pitch,0,127)),vel(e.polyPressure.pressure,false)},0,0});break;}
            default:break; // SysEx/note expression: not part of the short-message firmware path
            }
        }
    }
    // No immediate UI-owner ADC write: a delayed host echo must not rewind a
    // newer physical slider value. No-echo hosts use this same timestamp queue.
    const unsigned beforeFallback=used;
    panelControls_.append(hostControlPoints,[&](unsigned i){return params_[i].load(std::memory_order_relaxed);},
        [&](unsigned i,double volts){add({0,1,0,{},i,volts});});
    // A fallback is the block's initial panel position. Explicit host events
    // at offset0 (notably MIDI wheel bridging) must be applied AFTER it.
    if(used!=beforeFallback)std::rotate(pending.begin(),pending.begin()+beforeFallback,pending.begin()+used);
    // Insertion sort (stable, allocation-free; host lists are nearly sorted).
    for(unsigned i=1;i<used;++i){auto x=pending[i];unsigned j=i;while(j>0&&pending[j-1].offset>x.offset){pending[j]=pending[j-1];--j;}pending[j]=x;}
    const int32 n=std::max<int32>(0,data.numSamples);
    for(unsigned i=0;i<used;++i){
        const int32 off=std::clamp<int32>(pending[i].offset,0,std::max<int32>(0,n-1));
        if(pending[i].kind==0)engine_->queueMidi(off,pending[i].bytes.data(),pending[i].size);
        else if(engine_->queueVoltage(off,pending[i].control,pending[i].volts))panelControls_.queued(pending[i].control,pending[i].volts);
    }
    if(n==0||data.numOutputs<1||!data.outputs||data.outputs[0].numChannels<1)return kResultOk;
    if(data.symbolicSampleSize!=kSample32)return kResultFalse;
    const float *inputLeft=nullptr,*inputRight=nullptr;bool stereo=false,sl=true,sr=true;
    if(inputActive_.load(std::memory_order_relaxed)&&data.numInputs>0&&data.inputs){
        const auto& in=data.inputs[0];
        if(in.channelBuffers32&&in.numChannels>0){inputLeft=in.channelBuffers32[0];sl=(in.silenceFlags&1)!=0;
            stereo=inputChannels_.load(std::memory_order_relaxed)==2&&in.numChannels>1;
            if(stereo){inputRight=in.channelBuffers32[1];sr=(in.silenceFlags&2)!=0;}}
    }
    // Copy input before rendering: hosts may alias input/output buffers.
    engine_->pushInput(inputLeft,inputRight,n,stereo,sl,sr);
    auto& out=data.outputs[0];float* l=out.channelBuffers32[0];float* r=out.numChannels>1?out.channelBuffers32[1]:nullptr;
    std::array<float*,auxOutputs> jacks{};bool anyJack=false;
    for(unsigned k=0;k<auxOutputs;++k)if(int32(k+1)<data.numOutputs){auto& b=data.outputs[k+1];if(b.channelBuffers32&&b.numChannels>0&&b.channelBuffers32[0]){jacks[k]=b.channelBuffers32[0];anyJack=true;b.silenceFlags=0;}}
    engine_->render(l,r,n,data.processMode==kOffline,anyJack?jacks.data():nullptr);
    // Last plugin stage: AFTER native synthesis, mix, output ring, SRC and volume.
    const double postDb=eii::vst::postOutputMaxDb*params_[postOutputId].load(std::memory_order_relaxed);
    postOutput_.process(l,r,n,postDb,engine_->sampleRate());
    for(unsigned k=0;k<auxOutputs;k+=2)if(jacks[k]||jacks[k+1])auxPost_[k/2].process(jacks[k]?jacks[k]:jacks[k+1],jacks[k]?jacks[k+1]:nullptr,n,postDb,engine_->sampleRate());
    out.silenceFlags=0;
    return kResultOk;
}

// ---- controller facet ----
tresult PLUGIN_API ControllerFacet::queryInterface(const TUID iid,void** obj){return static_cast<IComponent&>(p_).queryInterface(iid,obj);}
uint32 PLUGIN_API ControllerFacet::addRef(){return static_cast<IComponent&>(p_).addRef();}
uint32 PLUGIN_API ControllerFacet::release(){return static_cast<IComponent&>(p_).release();}
tresult PLUGIN_API ControllerFacet::initialize(FUnknown* c){return static_cast<IComponent&>(p_).initialize(c);}
tresult PLUGIN_API ControllerFacet::terminate(){return static_cast<IComponent&>(p_).terminate();}
tresult PLUGIN_API ControllerFacet::getParameterInfo(int32 index,ParameterInfo& info){
    if(index<0||index>=getParameterCount())return kInvalidArgument;
    info={};info.unitId=0; // root unit
    if(index<int32(realParams)){
        static const char16_t* names[]={u"Slider A",u"Slider B",u"Slider C",u"Slider D",u"Left Wheel",u"Right Wheel",u"Volume",u"Post output gain"};
        static const char16_t* shortNames[]={u"A",u"B",u"C",u"D",u"LWhl",u"RWhl",u"Vol",u"PostOut"};
        info.id=ParamID(index);copyString(info.title,names[index]);copyString(info.shortTitle,shortNames[index]);
        copyString(info.units,index==int32(postOutputId)?u"dB":index<int32(controlCount)?u"V":u"%");info.defaultNormalizedValue=Plugin::defaultValue(unsigned(index));info.flags=ParameterInfo::kCanAutomate;
        return kResultOk;
    }
    const unsigned k=unsigned(index)-realParams,ch=k/midiPerChannel,cc=k%midiPerChannel;
    info.id=midiBase+k;info.flags=ParameterInfo::kIsHidden;info.defaultNormalizedValue=cc==kPitchBend?0.5:0;
    char text[64];
    if(cc<128)std::snprintf(text,sizeof text,"MIDI ch%u CC%u",ch+1,cc);else std::snprintf(text,sizeof text,"MIDI ch%u %s",ch+1,cc==kAfterTouch?"Aftertouch":cc==kPitchBend?"Pitch Bend":"Program");
    copyAscii(info.title,text);copyAscii(info.shortTitle,text);return kResultOk;
}
tresult PLUGIN_API ControllerFacet::getParamStringByValue(ParamID id,ParamValue v,String128 out){
    char text[64];
    if(id<controlCount)std::snprintf(text,sizeof text,"%.2f",v*5.0);else if(id==volumeId)std::snprintf(text,sizeof text,"%d",int(std::lround(v*100)));else if(id==postOutputId)std::snprintf(text,sizeof text,"%.2f",v*eii::vst::postOutputMaxDb);else std::snprintf(text,sizeof text,"%.3f",v);
    copyAscii(out,text);return kResultOk;
}
tresult PLUGIN_API ControllerFacet::getParamValueByString(ParamID id,TChar* text,ParamValue& v){
    char ascii[64];int i=0;for(;text[i]&&i<63;++i)ascii[i]=char(text[i]<128?text[i]:' ');ascii[i]=0;
    char* end=nullptr;const double x=std::strtod(ascii,&end);if(end==ascii)return kResultFalse;
    v=std::clamp(id<controlCount?x/5.0:id==volumeId?x/100.0:id==postOutputId?x/eii::vst::postOutputMaxDb:x,0.0,1.0);return kResultOk;
}
ParamValue PLUGIN_API ControllerFacet::getParamNormalized(ParamID id){return id<realParams?p_.params_[id].load():id>=midiBase&&(id-midiBase)%midiPerChannel==kPitchBend?0.5:0;}
tresult PLUGIN_API ControllerFacet::setParamNormalized(ParamID id,ParamValue v){
    if(id<realParams){p_.params_[id].store(std::clamp(v,0.0,1.0));return kResultOk;}
    return id>=midiBase&&id<midiBase+midiParams?kResultOk:kInvalidArgument;
}
tresult PLUGIN_API ControllerFacet::setComponentHandler(IComponentHandler* h){p_.setHandler(h);return kResultOk;}
IPlugView* PLUGIN_API ControllerFacet::createView(FIDString name){
    if(!name||std::strcmp(name,ViewType::kEditor)!=0)return nullptr;
    try{return new eii::vst::Editor(p_);}catch(...){return nullptr;}
}

// ---- factory ----
class Factory final : public IPluginFactory2 {
public:
    tresult PLUGIN_API queryInterface(const TUID iid,void** obj)override{
        if(FUnknownPrivate::iidEqual(iid,FUnknown::iid)||FUnknownPrivate::iidEqual(iid,IPluginFactory::iid)||FUnknownPrivate::iidEqual(iid,IPluginFactory2::iid)){addRef();*obj=this;return kResultOk;}
        *obj=nullptr;return kNoInterface;
    }
    uint32 PLUGIN_API addRef()override{return 1;} // static lifetime
    uint32 PLUGIN_API release()override{return 1;}
    tresult PLUGIN_API getFactoryInfo(PFactoryInfo* info)override{
        if(!info)return kInvalidArgument;*info={};
        std::strncpy(info->vendor,"Tamas (private)",PFactoryInfo::kNameSize-1);std::strncpy(info->url,"",PFactoryInfo::kURLSize-1);std::strncpy(info->email,"",PFactoryInfo::kEmailSize-1);
        info->flags=PFactoryInfo::kUnicode;return kResultOk;
    }
    int32 PLUGIN_API countClasses()override{return 1;}
    tresult PLUGIN_API getClassInfo(int32 index,PClassInfo* info)override{
        if(index!=0||!info)return kInvalidArgument;*info={};
        std::memcpy(info->cid,eii::vst::processorUid,sizeof(TUID));info->cardinality=PClassInfo::kManyInstances;
        std::strncpy(info->category,kVstAudioEffectClass,PClassInfo::kCategorySize-1);std::strncpy(info->name,"Emulator II Custom",PClassInfo::kNameSize-1);return kResultOk;
    }
    tresult PLUGIN_API getClassInfo2(int32 index,PClassInfo2* info)override{
        if(index!=0||!info)return kInvalidArgument;*info={};
        std::memcpy(info->cid,eii::vst::processorUid,sizeof(TUID));info->cardinality=PClassInfo::kManyInstances;
        std::strncpy(info->category,kVstAudioEffectClass,PClassInfo::kCategorySize-1);std::strncpy(info->name,"Emulator II Custom",PClassInfo::kNameSize-1);
        info->classFlags=0; // single component: never kDistributable
        std::strncpy(info->subCategories,PlugType::kInstrumentSampler,PClassInfo2::kSubCategoriesSize-1);
        std::strncpy(info->vendor,"Tamas (private)",PClassInfo2::kVendorSize-1);std::strncpy(info->version,EII_VST3_VERSION,PClassInfo2::kVersionSize-1);
        std::strncpy(info->sdkVersion,kVstVersionString,PClassInfo2::kVersionSize-1);return kResultOk;
    }
    tresult PLUGIN_API createInstance(FIDString cid,FIDString iid,void** obj)override{
        *obj=nullptr;
        if(!FUnknownPrivate::iidEqual(cid,eii::vst::processorUid))return kNoInterface;
        auto* p=new(std::nothrow) Plugin;if(!p)return kOutOfMemory;
        const auto r=static_cast<IComponent*>(p)->queryInterface(iid,obj);static_cast<IComponent*>(p)->release();return r;
    }
};
Factory factory;
} // namespace

namespace eii::vst {
HINSTANCE moduleInstance(){return dllInstance;}
// VectorPanelWindow pairs GdiplusStartup/Shutdown per window and caches a GDI+
// bitmap in a function-local static. One extra module-lifetime reference keeps
// GDI+ alive while the DLL is loaded, so reopening an editor stays valid.
void ensureGdiplus(){
    std::lock_guard l(gdiMutex);if(gdiToken)return;
    Gdiplus::GdiplusStartupInput in;if(Gdiplus::GdiplusStartup(&gdiToken,&in,nullptr)!=Gdiplus::Ok)gdiToken=0;
}
}

extern "C" {
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){dllInstance=instance;DisableThreadLibraryCalls(instance);}return TRUE;}
__declspec(dllexport) bool PLUGIN_API InitDll(){
    // Soft CPU-set placement (host_cpu_placement.h) assumes one machine per
    // process; in a DAW every instance would claim the same two cores. The DSP
    // worker honours EII_CPU_PLACEMENT, so disable it unless the user set it.
    const char* placement=std::getenv("EII_CPU_PLACEMENT");
    if(placement&&std::strcmp(placement,"dynamic")==0){eii::vst::CorePool::enabledFlag()=true;_putenv_s("EII_CPU_PLACEMENT","0");}
    else if(!placement)_putenv_s("EII_CPU_PLACEMENT","0");
    return true;
}
__declspec(dllexport) bool PLUGIN_API ExitDll(){
    UnregisterClassW(L"EIIVectorFrontPanel",GetModuleHandleW(nullptr));UnregisterClassW(L"EIIFilterService",GetModuleHandleW(nullptr));
    UnregisterClassW(L"EIIVst3EditorHost",dllInstance);
    std::lock_guard l(gdiMutex);if(gdiToken&&liveObjects.load()==0){Gdiplus::GdiplusShutdown(gdiToken);gdiToken=0;}
    return true;
}
__declspec(dllexport) IPluginFactory* PLUGIN_API GetPluginFactory(){factory.addRef();return &factory;}
}
