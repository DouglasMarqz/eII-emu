// SSM2045 model 2: datasheet anchors, SPICE-reference spot values, runtime
// stability at 96/192 kHz, determinism and backward-compatible integration.
// Reference data: validation/ssm2045-model2-2026-10-29/ (docs/SSM2045_MODEL2.md).
#include "nonlinear_voice.h"
#include "vst3/plugin_state.h"
#include "filter_service_profiles.h"
#include <bit>
#include <complex>
#include <iostream>
#include <memory>
#include <vector>
using namespace eii::analog;
static void check(bool b,const char* m){if(!b)throw std::runtime_error(m);std::cout<<"PASS "<<m<<'\n';}
static ssm2::Params fig2(){ssm2::Params p;p.c1=10e-9;p.c2=3.3e-9;p.c3=10e-9;p.c4=680e-12;p.rext=5000;p.rLoad8=10910;p.rLoad1=10910;p.tailAtZero=12603.62e-9;p.a1=p.a2=.433;p.aQ=.10396;return p;}
static ssm2::Params eiiBoard(){ssm2::Params p;p.tailAtZero=12603.62e-9;p.a1=p.a2=.433;p.aQ=.10396;return p;}
struct Tone{double h1=0,thd=0;};
// Steady sine through the filter (source behind rext); amp = open-circuit node peak.
static Tone tone(ssm2::Params p,double fs,double v5,double iq,double freq,double amp,double src5=0){
    ssm2::Filter f(fs,p);f.setFc(v5,src5);f.setQCurrent(iq);
    const double vs=amp*(p.rext+p.rin)/p.rin;const size_t per=size_t(std::llround(fs/freq));const double fe=fs/double(per);
    const size_t settle=size_t(std::max(0.2,12/fe)*fs)/per*per,cycles=std::max<size_t>(20,size_t(fe*0.1));
    std::vector<double> y;for(size_t n=0;n<settle+cycles*per;++n)y.push_back(f.process(vs*std::sin(2*ssm2::pi*fe*double(n)/fs)).pin8);
    auto dft=[&](double fr){std::complex<double> s=0;for(size_t n=settle;n<y.size();++n)s+=y[n]*std::polar(1.0,-2*ssm2::pi*fr*double(n)/fs);return std::abs(s)*2/double(y.size()-settle);};
    Tone t;t.h1=dft(fe)/vs;double h=0;for(int k=2;k<=9;++k){const double a=dft(k*fe);h+=a*a;}t.thd=std::sqrt(h)/(t.h1*vs);return t;
}
static double ppk(ssm2::Params p,double fs,double v5,double iq,double seconds){
    ssm2::Filter f(fs,p);f.setFc(v5);f.setQCurrent(iq);double mx=-1e9,mn=1e9;const size_t N=size_t(seconds*fs);
    for(size_t n=0;n<N;++n){const double y=f.process(n<16?0.05:0).pin8;if(n>N*3/4){mx=std::max(mx,y);mn=std::min(mn,y);}}
    check(f.stats().resets==0,"no solver resets during oscillation");return mx-mn;
}
int main()try{
    const double fsLab=768000;
    // --- Datasheet anchors (Figure 2 components, 5k source = three 15k inputs) ---
    {   const double g0=tone(fig2(),fsLab,0,0,50,1e-4).h1,g3=tone(fig2(),fsLab,0,0,632.46,1e-4).h1;
        std::cout<<"fig2 V5=0: gain(632.46 Hz)/gain(50 Hz) = "<<20*std::log10(g3/g0)<<" dB\n";
        check(std::abs(20*std::log10(g3/g0)+3.01)<0.15,"Fig.2 cutoff 632.5 Hz at V5=0 (geometric centre of 20 Hz..20 kHz)");
        const double up=tone(fig2(),fsLab,-0.03,0,632.46*std::exp2(30/19.3),1e-4).h1/tone(fig2(),fsLab,-0.03,0,50,1e-4).h1;
        check(std::abs(20*std::log10(up)+3.01)<0.15,"-19.3 mV/octave: V5=-30 mV moves the cutoff by 2^(30/19.3)");
        check(std::abs(g0-8100.0/5000)<0.01*8100/5000,"in-band gain = -8.1k/Rext (summing-node topology)");
    }
    {   const Tone a=tone(fig2(),fsLab,0,0,158.115,0.075),b=tone(fig2(),fsLab,0,0,632.46,0.075);
        std::cout<<"THD 150 mVpp: Fc/4 "<<a.thd*100<<" %, Fc "<<b.thd*100<<" %\n";
        check(a.thd>0.00085&&a.thd<0.00115,"THD 0.1 % at 150 mVpp, 4Fin<=Fc (PDF2)");
        check(b.thd>0.0085&&b.thd<0.0115,"THD 1.0 % at 150 mVpp, Fin=Fc (PDF2)");
    }
    {   const double below=ppk(fig2(),192000,0,145e-6,0.6),above=ppk(fig2(),192000,0,160e-6,0.6);
        std::cout<<"Fig.2 Q onset: ppk(145 uA)="<<below<<" ppk(160 uA)="<<above<<'\n';
        check(below<1e-6&&above>0.2&&above<0.5,"self-oscillation onset at 150 uA (PDF2 120..185 uA typ 150)");
    }
    {   ssm2::Params p=fig2();p.noise=true;ssm2::Filter f(192000,p);f.setFc(-0.09);
        // 20 kHz band power: compare a deterministic run against the in-band signal of 150 mVpp.
        std::vector<double> y;for(size_t n=0;n<192000;++n)y.push_back(f.process(0).pin8);
        double s=0;for(size_t n=19200;n<y.size();++n)s+=y[n]*y[n];const double rms=std::sqrt(s/double(y.size()-19200));
        const double sig=0.075*(8100.0/5000)*(5900.0/900)/std::sqrt(2.0);
        std::cout<<"noise: full-band rms "<<rms<<" V, S/N(full band at 192k) "<<20*std::log10(sig/rms)<<" dB\n";
        check(rms>0&&20*std::log10(sig/rms)>78&&20*std::log10(sig/rms)<86,"noise floor consistent with 84 dB S/N re 150 mVpp (20 kHz, see validation)");
    }
    {   ssm2::Params p=eiiBoard();ssm2::Vca v(96000,p);for(int n=0;n<96000;++n)v.control(5,-0.26705);
        check(std::abs(v.tail()-463.8e-6)<1e-6,"EII VCA2 tail 463.8 uA at ACV 5 V (SPICE reference)");
        const double gm=(v.cell(1e-6)-v.cell(-1e-6))/2e-6/v.tail();
        check(std::abs(gm-8.17)<0.01,"VCA gain 4085 umho at 500 uA (8.17 S/A, PDF2)");
        for(int n=0;n<96000;++n)v.control(0,-0.26705);check(v.tail()<1e-9,"VCA2 closes at ACV 0 through the -BIAS shifted PNP");
    }
    // --- EII board: SPICE spot values (validation/.../eii_ac.out) ---
    {   const double G=1/22600.0+1/124000.0+1/1000.0,rth=1/G;
        struct Ref{double fcv,f3;};
        for(double fs:{96000.0,192000.0})for(Ref r:{Ref{1.0,9711.81},Ref{2.5,1005.82},Ref{4.0,104.07}}){
            const double v5=(r.fcv/22600-15.0/124000)/G;
            const double g0=tone(eiiBoard(),fs,v5,0,r.f3/8,1e-4,rth).h1,g3=tone(eiiBoard(),fs,v5,0,r.f3,1e-4,rth).h1;
            const double e=20*std::log10(g3/g0)+3.01;
            std::cout<<"EII fs="<<fs<<" FCV="<<r.fcv<<" gain at SPICE f3 "<<r.f3<<" Hz: error "<<e<<" dB\n";
            check(std::abs(e)<0.35,"EII cutoff matches the transistor-level SPICE reference at the runtime rate");
        }
        check(std::abs(tone(eiiBoard(),192000,(2.5/22600-15.0/124000)/G,0,40,1e-4,rth).h1-8100.0/68000)<0.002,"EII in-band gain 8.1k/68k = -18.48 dB");
    }
    // --- Runtime stability, determinism, extremes ---
    for(unsigned rate:{96000u,192000u}){
        NonlinearPlaybackVoice a(rate,false,false,true),b(rate,false,false,true);FilterCalibration c;a.calibrate(c);b.calibrate(c);
        double peak=0;bool same=true;
        for(double fcv:{0.0,2.5,5.0})for(double qcv:{0.0,3.0,5.0}){
            for(unsigned n=0;n<rate/4;++n){const eii::KnownByte code{std::uint8_t((n/37)&1?0x7f:0xff),255};
                const double ya=a.process(code,{5,true},{fcv,true},{qcv,true}),yb=b.process(code,{5,true},{fcv,true},{qcv,true});
                same&=std::bit_cast<std::uint64_t>(ya)==std::bit_cast<std::uint64_t>(yb);
                if(!std::isfinite(ya))throw std::runtime_error("nonfinite model 2");peak=std::max(peak,std::abs(ya));}
        }
        check(same,"model 2 is deterministic (noise from a fixed seed)");
        check(peak<20,"model 2 bounded at FC/Q/drive extremes");
        const auto s=a.solverStats();std::cout<<"rate "<<rate<<" samples "<<s.samples<<" iterations/sample "<<double(s.iterations)/double(s.samples)<<" fallbacks "<<s.fallbacks<<" resets "<<s.recoveries<<'\n';
        check(s.recoveries==0&&s.fallbacks*1000<s.samples,"model 2 Newton converges at full drive");
        check(a.process({0,0},{5,true},{2.5,true},{0,true})==0,"unknown DAC code mutes model 2");
    }
    // --- Integration: modes, state, schema ---
    {   using eii::vst::PluginSettings;PluginSettings s;s.audioMode=RuntimeAudioMode::Ssm2Chip192;s.ssm2Calibration[FilterParam::Ssm2DieTemp]=40;
        auto back=eii::vst::parseSettings(eii::vst::serializeSettings(s));
        check(back==s&&eii::vst::activeCalibration(back)[FilterParam::Ssm2DieTemp]==40,"model-2 mode and profile round trip in plugin state");
        check(eii::vst::audioModeFromIndex(5)==RuntimeAudioMode::Ssm2Chip&&eii::vst::audioModeFromIndex(4)==RuntimeAudioMode::EiiiCustom,"model indices appended, old indices unchanged");
        // A 0.3.19..0.3.22 state: schema-3 calibration and CEM-sized profile lists.
        FilterCalibration old;std::string v3=old.serialize();v3.replace(v3.find("=5"),2,"=3");v3.erase(v3.find("Ssm2DieTemp="));
        std::string list;for(unsigned i=0;i<cemFilterParameterCount;++i)list+=(i?",":"")+std::to_string(old.values[i]);
        const auto parsed=eii::vst::parseSettings("EIIVST3 1\nmodel=4\nlegacycalibration="+list+"\neiiicalibration="+list+"\ncalibration:\n"+v3);
        check(parsed.audioMode==RuntimeAudioMode::EiiiCustom&&parsed.calibration==FilterCalibration{}&&parsed.ssm2Calibration==FilterCalibration{},"older states load with model-2 defaults; untouched native trims follow the new level");
        {   // A deliberate native trim keeps its loudness (level_model.h): 17.25 dB -> -8.51 dB, once.
            FilterCalibration trimmed;trimmed[FilterParam::NativeOutDb]=17.25;std::string t=trimmed.serialize();t.replace(t.find("=5"),2,"=4");
            const auto old4=eii::vst::parseSettings("EIIVST3 1\nmodel=1\ncalibration:\n"+t);
            check(std::abs(old4.calibration[FilterParam::NativeOutDb]-(17.25-25.7571))<1e-3&&eii::vst::parseSettings(eii::vst::serializeSettings(old4))==old4,"level-model migration of a set trim: -25.76 dB, applied once");
            eii::host::FilterServiceProfiles gp;gp.ssm=trimmed;gp.ssm2=trimmed;std::string g3=gp.serialize();g3.replace(g3.find("GLOBAL=4"),8,"GLOBAL=3");
            const auto old3=eii::host::FilterServiceProfiles::parse(g3);
            check(std::abs(old3.ssm[FilterParam::NativeOutDb]+8.5071)<1e-3&&std::abs(old3.ssm2[FilterParam::NativeOutDb]+8.5071)<1e-3&&old3.legacy==FilterCalibration{},"older global profiles migrate set native trims of every model; untouched ones stay");}
        eii::host::FilterServiceProfiles g;g.ssm2[FilterParam::Ssm2Noise]=0;check(eii::host::FilterServiceProfiles::parse(g.serialize())==g,"four-model global profile round trip");
        check(!ssm2ParameterSupported(unsigned(FilterParam::Gm0))&&ssm2ParameterSupported(unsigned(FilterParam::C1))&&ssm2ParameterSupported(unsigned(FilterParam::Ssm2Tail))
            &&!ssm2ParameterSupported(unsigned(FilterParam::VcaRange))&&!ssmParameterSupported(unsigned(FilterParam::Ssm2Tail))&&!legacyQParameterSupported(unsigned(FilterParam::Ssm2Tail))
            &&!cemParameterSupported(unsigned(FilterParam::Ssm2Tail)),"Filter Service shows only model-2 meaningful controls");
        check(workerAudioMode(RuntimeAudioMode::Ssm2Chip)&&workerAudioMode(RuntimeAudioMode::Ssm2Chip192)&&!nonlinearAudioMode(RuntimeAudioMode::Ssm2Chip),"model 2 runs on the DSP worker");
    }
    std::cout<<"PASS SSM2045 model 2\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
