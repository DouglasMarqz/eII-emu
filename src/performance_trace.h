#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

namespace eii::host {
// UI-owned, opt-in bounded telemetry. Never a CPU/bus instruction trace.
// Formatting/file export happens outside the emulation and audio owners.
class PerformanceTrace {
public:
    static constexpr std::size_t maxRows=4096,maxBytes=4*1024*1024,maxDetail=8192;
    void start(std::uint64_t nowMs){rows_.clear();bytes_=overwritten_=0;origin_=nowMs;active_=true;event(nowMs,"record_start","");}
    void stop(std::uint64_t nowMs){if(active_)event(nowMs,"record_stop","");active_=false;}
    bool active()const noexcept{return active_;}
    std::size_t size()const noexcept{return rows_.size();}
    std::size_t bytes()const noexcept{return bytes_;}
    std::uint64_t overwritten()const noexcept{return overwritten_;}
    void event(std::uint64_t nowMs,std::string_view name,std::string_view detail){
        if(!active_)return;
        auto out=begin(nowMs,"event");out<<",\"name\":"<<quote(name)<<",\"detail\":"<<quote(detail.substr(0,maxDetail))
            <<",\"detail_truncated\":"<<(detail.size()>maxDetail?"true":"false");append(out.str()+"}\n");
    }
    template<class Snapshot>void sample(std::uint64_t nowMs,const Snapshot& s){
        if(!active_||!s.trace.enabled)return;
        auto out=begin(nowMs,"sample");const auto& t=s.trace;const auto& d=s.dsp;
        auto value=[&](const char* key,auto n){out<<",\""<<key<<"\":";
            if constexpr(std::is_floating_point_v<decltype(n)>){if(!std::isfinite(n)){out<<"null";return;}}
            out<<n;};
        value("tick",s.ticks);value("loaded",int(s.loaded));value("running",int(s.running));
        value("machine_generation",t.machineGeneration);value("audio_generation",t.audioGeneration);value("model",unsigned(s.audioMode));
        value("native_dac",int(s.nativeAudio));value("native_overflow",s.nativeOverflow);value("native_order_errors",s.nativeOrderErrors);value("s3528",int(s.reconstruction));value("speed",s.speedRatio);value("owner_busy",t.ownerBusy);value("quantum_ms",t.quantumMs);value("mean_quantum_ms",t.meanQuantumMs);value("max_quantum_ms",t.maxQuantumMs);
        value("lateness_ms",t.latenessMs);value("pacing_rebases",s.pacingRebases);value("discarded_debt_ms",s.pacingDebtMs);
        value("audio_frames",t.audio.queued);value("audio_limit",t.audio.limit);value("audio_dropped",t.audio.dropped);value("audio_missing",t.audio.missing);
        value("audio_dropped_per_s",s.hostDroppedPerSecond);value("audio_missing_per_s",s.hostMissingPerSecond);
        value("dsp_queue",d.queuedFrames);value("dsp_rendered",d.rendered);value("dsp_dropped",d.dropped);value("dsp_failures",d.failures);
        value("dsp_load_wall",d.load);value("dsp_block_ms",d.blockMs);value("dsp_wait_ms",d.waitMs);value("dsp_max_wait_ms",d.maxWaitMs);
        value("solver_samples",d.solverSamples);value("solver_iterations",d.iterations);value("solver_fallbacks",d.fallbacks);value("solver_recovered",d.recoveries);value("solver_held",d.held);
        value("dsp_dc",d.outputDc);value("dsp_clipped",d.clippedFrames);value("calibration_revision",d.calibrationRevision);
        value("midi_received",s.midiReceived);value("midi_read",s.midiRead);value("midi_overruns",s.midiOverruns);value("midi_dropped",s.midiDropped);
        value("main_pc",t.mainPc);value("scanner_pc",t.scannerPc);value("main_halt",int(t.mainHalt));value("main_wait",int(t.mainWait));
        value("dma_transfers",s.dma);value("dram_reads",s.dramReads);value("cylinder",s.cylinder);value("drive_selected",int(s.selected));
        value("production_invalid",int(t.invalid));out<<",\"lcd\":["<<quote(s.lcd[0])<<','<<quote(s.lcd[1])<<"]";
        out<<",\"cv\":[";for(unsigned i=0;i<t.cv.size();++i){if(i)out<<',';if(std::isfinite(t.cv[i]))out<<t.cv[i];else out<<"null";}out<<"]";
        out<<",\"s3528_codes\":[";for(unsigned ch=0;ch<8;++ch){if(ch)out<<',';out<<unsigned(t.reconstructionCode[ch].value&63);}out<<"],\"s3528_masks\":[";for(unsigned ch=0;ch<8;++ch){if(ch)out<<',';out<<unsigned(t.reconstructionCode[ch].known&63);}out<<"]";
        out<<",\"cv_known_mask\":"<<t.cvKnown<<",\"error\":"<<quote(s.error.substr(0,1024));append(out.str()+"}\n");
    }
    std::string exportJsonl()const {
        std::ostringstream out;out.imbue(std::locale::classic());
        out<<"{\"type\":\"header\",\"schema\":1,\"application\":\"EII 0.70\",\"sample_ms\":250,\"threads_ms\":1000,\"max_rows\":"<<maxRows
            <<",\"max_bytes\":"<<maxBytes<<",\"overwritten_rows\":"<<overwritten_
            <<",\"models\":\"1=Legacy;2=SSM96k;3=SSM192k\",\"cv_order\":\"ACV0..7,FCV0..7,QCV0..7; known mask bit=index\",\"notes\":\"Opt-in host telemetry; not cycle replay. Threads busy=one-core fraction; -1=no prior live thread. DSP counters reset across audio_generation.\"}\n";
        for(const auto& row:rows_)out<<row;
        return out.str();
    }
    static std::string quote(std::string_view text){
        std::string out="\"";constexpr char hex[]="0123456789abcdef";
        for(unsigned char b:text){if(b=='"'||b=='\\'){out+='\\';out+=char(b);}else if(b<32||b>=127){out+="\\u00";out+=hex[b>>4];out+=hex[b&15];}else out+=char(b);}
        return out+'"';
    }
private:
    bool active_=false;std::uint64_t origin_=0,overwritten_=0;std::size_t bytes_=0;std::deque<std::string> rows_;
    std::ostringstream begin(std::uint64_t nowMs,std::string_view type)const {
        std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(10)<<"{\"type\":"<<quote(type)<<",\"ms\":"<<(nowMs>=origin_?nowMs-origin_:0);return out;
    }
    void append(std::string row){
        if(row.size()>maxBytes){++overwritten_;return;}
        while(!rows_.empty()&&(rows_.size()>=maxRows||bytes_+row.size()>maxBytes)){bytes_-=rows_.front().size();rows_.pop_front();++overwritten_;}
        bytes_+=row.size();rows_.push_back(std::move(row));
    }
};
}
