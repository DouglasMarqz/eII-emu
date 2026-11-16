#include "machine.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
static std::uint64_t countArgument(const std::string& text) {
    if(text.empty()||text.find_first_not_of("0123456789")!=std::string::npos)
        throw std::invalid_argument("Invalid nonnegative integer: "+text);
    return std::stoull(text);
}
static double realArgument(const std::string& text) {
    std::size_t used=0;const double value=std::stod(text,&used);
    if(used!=text.size()||!std::isfinite(value))throw std::invalid_argument("Invalid finite number: "+text);
    return value;
}
int main(int argc,char** argv){try{
    if(argc<9){std::cerr<<"usage: audio_probe main scanner prom135 prom137 warmup-cycles capture-cycles output.csv rate [--zero-caps-experiment] [--align-dividers-experiment] [--gm=SIEMENS] [--vca=AMPS] [--confirm-channel0-playback] [--legacy-generic-reconstruction]\n";return 2;}
    const auto warmup=countArgument(argv[5]),capture=countArgument(argv[6]),rate=countArgument(argv[8]);
    if(rate==0||rate>eii::analog::PlaybackFrameIntegrator::sourceRate)throw std::invalid_argument("Invalid output frame rate");
    eii::analog::ClockedPlaybackOptions options;options.outputRate=unsigned(rate);
    for(int a=9;a<argc;++a){const std::string flag=argv[a];
        if(flag=="--legacy-generic-reconstruction")options.reconstructionProfile=eii::analog::S3528Profile::GenericElliptic;
        else if(flag=="--zero-caps-experiment")options.zeroCapacitorsForExperiment=true;
        else if(flag=="--align-dividers-experiment")options.alignDividersForExperiment=true;
        else if(flag=="--confirm-channel0-playback")options.channel0PlaybackConfirmed=true;
        else if(flag.starts_with("--gm="))for(auto& x:options.filterGm)x={realArgument(flag.substr(5)),true};
        else if(flag.starts_with("--vca="))for(auto& x:options.vcaBranchAmps)x={realArgument(flag.substr(6)),true};
        else throw std::invalid_argument("Unknown audio_probe argument: "+flag);
    }
    eii::analog::ClockedPlayback validateOptions(options,0);
    auto machine=std::make_unique<eii::Machine>();machine->load(argv[1],argv[2],argv[3],argv[4]);machine->run(warmup);
    std::ofstream csv(argv[7]);if(!csv)throw std::runtime_error("Cannot create measurement CSV");
    csv<<std::setprecision(17);
    csv<<"# model=nominal_source_grid_experiment; S3528="<<eii::analog::s3528ProfileName(options.reconstructionProfile)<<"; output=box_integral_not_production_antialias\n";
    csv<<"# rate="<<options.outputRate<<"; origin_half_edge="<<machine->channelTimers.halfClocks<<"; zero_caps="<<options.zeroCapacitorsForExperiment<<"; aligned_dividers="<<options.alignDividersForExperiment<<"; channel0_confirmed="<<options.channel0PlaybackConfirmed<<'\n';
    csv<<"# explicit_pin_controls=";
    for(unsigned ch=0;ch<8;++ch){csv<<" ch"<<ch<<"_gm=";if(options.filterGm[ch].known)csv<<options.filterGm[ch].value;else csv<<"unknown";csv<<"_vca=";if(options.vcaBranchAmps[ch].known)csv<<options.vcaBranchAmps[ch].value;else csv<<"unknown";}csv<<'\n';
    csv<<"frame,time_from_capture_s,known_mask,ch0_volts,ch1_volts,ch2_volts,ch3_volts,ch4_volts,ch5_volts,ch6_volts,ch7_volts\n"<<std::setprecision(17);
    std::uint64_t knownFrames=0;
    machine->enableClockedPlayback(options,[&](const eii::analog::PlaybackFrame& frame){
        unsigned mask=0;for(unsigned ch=0;ch<8;++ch)if(frame.meanVolts[ch].known)mask|=1u<<ch;
        if(mask)++knownFrames;
        csv<<frame.index<<','<<double(frame.index)/options.outputRate<<','<<mask;
        for(const auto& x:frame.meanVolts){csv<<',';if(x.known)csv<<x.value;}csv<<'\n';
    });
    machine->run(capture);csv.flush();if(!csv)throw std::runtime_error("Measurement CSV write failed");
    const auto& engine=*machine->clockedPlayback();
    std::cout<<machine->report()<<"Clocked nominal experiment intervals="<<engine.intervals()<<" frames="<<engine.frames()<<" frames_with_known_channels="<<knownFrames<<" linear_supply_exceeded_mask="<<unsigned(engine.rangeExceededMask())<<'\n';
    return 0;
}catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<'\n';return 1;}}
