// SSM2045 model-2 laboratory: datasheet-anchor experiments on the C++ model.
// Usage: ssm2_lab <experiment> key=value...   (see docs/SSM2045_MODEL2.md)
// Experiments: thd, ac, onset, osc, noise, expo, vcathd, vcaft, cpu
#include "ssm2045_model2.h"
#include <chrono>
#include <complex>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using namespace eii::analog::ssm2;
static std::map<std::string,double> kv;
static double arg(const char* k,double d){auto i=kv.find(k);return i==kv.end()?d:i->second;}
static Params params(){
    Params p;
    if(arg("fig2",0)!=0){p.c1=10e-9;p.c2=3.3e-9;p.c3=10e-9;p.c4=680e-12;p.rext=5000;p.rLoad8=10910;p.rLoad1=10910;}
    p.a1=arg("a1",p.a1);p.a2=arg("a2",p.a2);p.aQ=arg("aQ",p.aQ);p.tailAtZero=arg("tail",p.tailAtZero);
    p.dieTempC=arg("T",p.dieTempC);p.rext=arg("rext",p.rext);p.noise=arg("noise",0)!=0;p.offsets=arg("offsets",0)!=0;
    p.sinkCurrent=arg("isink",p.sinkCurrent);p.aVca=arg("aVca",p.aVca);
    return p;
}
// Goertzel-style DFT of x at bin frequency f over samples [a,b).
static std::complex<double> dft(const std::vector<double>& x,size_t a,size_t b,double f,double fs){
    std::complex<double> s=0;for(size_t n=a;n<b;++n)s+=x[n]*std::polar(1.0,-2*pi*f*double(n)/fs);return s*(2.0/double(b-a));
}
int main(int argc,char** argv){
    if(argc<2){std::puts("usage: ssm2_lab experiment key=value...");return 2;}
    for(int i=2;i<argc;++i){const char* e=std::strchr(argv[i],'=');if(e)kv[std::string(argv[i],size_t(e-argv[i]))]=std::atof(e+1);}
    const std::string ex=argv[1];const double fs=arg("fs",1536000);
    Params p=params();Filter f(fs,p);
    const double v5=arg("v5",0),iq=arg("iq",0),src5=arg("src5",0);
    f.setFc(v5,src5);f.setQCurrent(iq);
    const double ocGain=p.rin/(p.rext+p.rin); // open-circuit node drive per source volt
    if(ex=="thd"||ex=="ac"){
        const double freq=arg("freq",1000),amp=arg("amp",0.075); // amp: open-circuit node peak [V]
        const double vs=amp/ocGain;const size_t per=size_t(std::llround(fs/freq));const double fexact=fs/double(per);
        const size_t settle=size_t(arg("settle",0.3)*fs)/per*per,cycles=size_t(std::max(20.0,arg("cycles",fexact*0.2)));
        std::vector<double> y;y.reserve(settle+cycles*per);
        for(size_t n=0;n<settle+cycles*per;++n){auto o=f.process(vs*std::sin(2*pi*fexact*double(n)/fs));y.push_back(arg("pin",8)==1?o.pin1:o.pin8);}
        const auto h1=dft(y,settle,y.size(),fexact,fs);double hs=0;std::printf("%s freq=%.6g amp=%.6g v5=%.6g iq=%.6g h1=%.9g phase=%.6f",ex.c_str(),fexact,amp,v5,iq,std::abs(h1),std::arg(h1));
        for(int k=2;k<=9;++k){const double a=std::abs(dft(y,settle,y.size(),k*fexact,fs));hs+=a*a;std::printf(" h%d=%.3e",k,a/std::abs(h1));}
        double dc=0;for(size_t n=settle;n<y.size();++n)dc+=y[n];dc/=double(y.size()-settle);
        std::printf(" thd=%.6g dc=%.6g gainOC=%.6g gainSrc=%.6g\n",std::sqrt(hs)/std::abs(h1),dc,std::abs(h1)/amp,std::abs(h1)/vs);
    }else if(ex=="osc"){ // steady oscillation amplitude/frequency at given iq, plus in-band gain
        const double tEnd=arg("t",1.0);std::vector<double> y;const size_t N=size_t(tEnd*fs);y.reserve(N);
        double kick=1e-3;for(size_t n=0;n<N;++n){auto o=f.process(n<16?kick/ocGain:0);y.push_back(o.pin8);}
        const size_t a=N*3/4;double mx=-1e9,mn=1e9;size_t zc=0,first=0,last=0;
        for(size_t n=a+1;n<N;++n){mx=std::max(mx,y[n]);mn=std::min(mn,y[n]);if(y[n-1]<0&&y[n]>=0){if(!zc)first=n;last=n;++zc;}}
        const double fr=zc>1?double(zc-1)*fs/double(last-first):0;
        std::printf("osc iq=%.6g v5=%.6g ppk=%.6g max=%.6g min=%.6g freq=%.6g fallbacks=%llu\n",iq,v5,mx-mn,mx,mn,fr,(unsigned long long)f.stats().fallbacks);
    }else if(ex=="noise"){ // write pin-8 output with zero input as float64 raw to file
        const size_t N=size_t(arg("t",2.0)*fs);std::FILE* o=std::fopen("ssm2_noise.f64","wb");
        for(size_t n=0;n<N;++n){const double v=f.process(0).pin8;std::fwrite(&v,8,1,o);}std::fclose(o);
        std::printf("noise written %zu samples fs=%g\n",N,fs);
    }else if(ex=="drive"){ // large sine source (behind rext): pin-8 max/min/rms and C4 extremes
        const double fr=arg("freq",400),amp=arg("srcpeak",13.5);const size_t N=size_t(arg("t",0.06)*fs),start=size_t(arg("t0",0.04)*fs);
        double mx=-1e9,mn=1e9,s2=0,c4x=-1e9,c4n=1e9;
        for(size_t n=0;n<N;++n){auto o=f.process(amp*std::sin(2*pi*fr*double(n)/fs));if(n>=start){mx=std::max(mx,o.pin8);mn=std::min(mn,o.pin8);s2+=o.pin8*o.pin8;}}
        (void)c4x;(void)c4n;
        std::printf("drive max=%.6g min=%.6g rms=%.6g fallbacks=%llu it/s=%.3f\n",mx,mn,std::sqrt(s2/double(N-start)),(unsigned long long)f.stats().fallbacks,double(f.stats().iterations)/double(f.stats().samples));
    }else if(ex=="cpu"){
        const size_t N=size_t(arg("n",192000));double acc=0;const auto t0=std::chrono::steady_clock::now();
        for(size_t n=0;n<N;++n){acc+=f.process(0.5*std::sin(n*0.01)/ocGain*0.05).pin8;}
        const double dt=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
        std::printf("cpu samples=%zu seconds=%.6f nsPerSample=%.2f iterations/sample=%.3f acc=%g\n",N,dt,dt*1e9/double(N),double(f.stats().iterations)/double(N),acc);
    }else if(ex=="vcadc"){ // steady VCA2 tail current vs ACV through the EII R91/C112/R100 network
        for(double acv:{0.0,0.2,0.25,0.3,0.4,0.5,0.75,1.0,2.0,3.0,4.0,5.0}){Vca v(fs,p);for(size_t n=0;n<size_t(0.2*fs);++n)v.control(acv,arg("bias",-0.26705));
            std::printf("vcadc acv=%.3g i15=%.6g\n",acv,v.tail());}
    }else if(ex=="vcathd"){ // cell THD at given peak input and ACV, output = tail*tanh() current
        Vca v(fs,p);const double acv=arg("acv",5),amp=arg("amp",0.02),fr=arg("freq",1000);for(size_t n=0;n<size_t(0.2*fs);++n)v.control(acv,arg("bias",-0.26705));
        const size_t per=size_t(std::llround(fs/fr));std::vector<double> y;for(size_t n=0;n<per*50;++n)y.push_back(v.cell(amp*std::sin(2*pi*fr*double(n)/fs))*4700);
        const auto h1=dft(y,0,y.size(),fs/double(per),fs);double hs=0;for(int k=2;k<=10;++k){const double a=std::abs(dft(y,0,y.size(),k*fs/double(per),fs));hs+=a*a;}
        std::printf("vcathd acv=%.3g amp=%.4g i15=%.6g h1=%.6g thd=%.6g\n",acv,amp,v.tail(),std::abs(h1),std::sqrt(hs)/std::abs(h1));
    }else{std::puts("unknown experiment");return 2;}
    return 0;
}
