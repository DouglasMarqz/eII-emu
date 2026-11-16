// Sample input step 5: 7-42 analog path + 7-43 SAR + channel-0 IINT loop with
// a real Z80 INI loop. Synthetic program; no ROMs. Scanner PIO B pins (PGAIN)
// are set directly as the board-level external state of this test.
#include "machine.h"
#include "vst3/host_sample_input.h"
#include "deglitch.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
static int failures=0;
static void require(bool c,const char* what){if(!c){std::cerr<<"FAIL: "<<what<<"\n";++failures;}}
using eii::analog::SampleInputAnalog;using eii::analog::SampleInputSignal;
static constexpr eii::LogicLevel H{true,true},L{true,false};
static double amplitudeAt1k(bool pgain1N,bool pgain2,double vin){
    SampleInputAnalog a;a.reset({4.98,true});SampleInputSignal s;s.rate=192000;s.voltsFullScale=1;
    const unsigned n=192000/2;s.samples.resize(n);
    for(unsigned i=0;i<n;++i)s.samples[i]=float(vin*std::sin(2*3.14159265358979323846*1000*i/192000.0));
    SampleInputAnalog::Pins p{pgain1N?H:L,pgain2?H:L,L,L,L,{0x3b,255},{4.98,true}};
    double peak=0;
    for(std::uint64_t t=0;t<4000000/2-100;++t){a.tick(p,&s,t,{0,true});if(t>1500000)peak=std::max(peak,std::fabs(a.bufferVolts()));}
    return peak;
}
int main(int argc,char**){
    {   // IC86 gain states (DG202 logic1 = on): nominal |gain| 1 / 10 / 10 / 101 at 1 kHz.
        const double g0=amplitudeAt1k(true,false,0.01)/0.01,g1=amplitudeAt1k(false,false,0.01)/0.01;
        const double g2=amplitudeAt1k(true,true,0.01)/0.01,g3=amplitudeAt1k(false,true,0.001)/0.001;
        std::printf("1kHz gain: -PGAIN1=1,PGAIN2=0 %.3f  0,0 %.3f  1,1 %.3f  0,1 %.3f\n",g0,g1,g2,g3);
        require(std::fabs(g0-0.991)<0.05&&std::fabs(g1-10)<0.5&&std::fabs(g2-10.0)<0.5&&std::fabs(g3-100.9)<5,"0/20/20/40 dB nominal gain states (S3528 code 3Bh, fc 12.5 kHz)");
    }
    {   // S/H and comparator: IDATA = +IDAC.V > V(C40).
        SampleInputAnalog a;a.reset({4.98,true});
        SampleInputAnalog::Pins p{H,L,H,L,L,{0,255},{4.98,true}};
        auto c=a.tick(p,nullptr,0,{0.5,true});require(c.known&&c.high,"IDAC above held 0V");
        c=a.tick(p,nullptr,1,{-0.5,true});require(c.known&&!c.high,"IDAC below held 0V");
        p.ismpl=L;c=a.tick(p,nullptr,2,{0.0,true});require(a.held().known&&a.held().value==0,"hold while ISMPL low");
    }
    {   // Closed loop with a real Z80 INI loop: 1 kHz, 0.5 V into +INPUT.V, 0 dB.
        auto m=std::make_unique<eii::Machine>();
        const std::uint8_t code[]={
            0x31,0x00,0x28,
            0x3e,0x34,0xd3,0x1f, 0x3e,0x68,0xd3,0x1c, 0x3e,0x01,0xd3,0x1c, // channel-0 timer 360
            0x3e,0xf8,0xd3,0x70, 0x3e,0xff,0xd3,0x78, 0x3e,0xf0,0xd3,0x70, // ACV0 = code FFh (as OS3.1 6DE8)
            0x3e,0x3b,0xd3,0x40,                                           // -CSWF0: S3528 code 3Bh
            0x3e,0xcf,0xd3,0x33, 0x3e,0x7f,0xd3,0x33, 0x3e,0x80,0xd3,0x32, // IMODE=1
            0x01,0x00,0x40, 0x0b,0x78,0xb1,0x20,0xfb,                      // settle ~16k loops
            0x21,0x00,0x21, 0x06,0x00, 0x0e,0x08,                         // 256 bytes at 2100h
            0xed,0xa2, 0xe3,0xe3, 0x20,0xfa,
            0x06,0x00, 0xed,0xa2, 0xe3,0xe3, 0x20,0xfa,                   // 256 more
            // Leave sampling, wait ~0.4s, then re-enter and record again.
            0x3e,0x00,0xd3,0x32,
            0x01,0xff,0xff,0x0b,0x78,0xb1,0x20,0xfb,
            0x3e,0x80,0xd3,0x32,
            0x01,0x00,0x40,0x0b,0x78,0xb1,0x20,0xfb,
            0x21,0x00,0x23,0x06,0x00,0x0e,0x08,
            0xed,0xa2,0xe3,0xe3,0x20,0xfa,
            0x06,0x00,0xed,0xa2,0xe3,0xe3,0x20,0xfa,
            0x76};
        std::copy(std::begin(code),std::end(code),m->main.rom.begin());
        auto& b=m->scanner.pio[1];b.mode=3;b.mask=0x3f;b.output=0x80;   // -PGAIN1=1, +PGAIN2=0
        auto sig=std::make_shared<eii::analog::SampleInputSignal>();sig->rate=48000;sig->voltsFullScale=1.0;
        sig->samples.resize(96000);for(unsigned i=0;i<sig->samples.size();++i)sig->samples[i]=float(0.5*std::sin(2*3.14159265358979323846*1000*i/48000.0));
        if(argc>1){auto input=std::make_shared<eii::vst::HostSampleInput>();input->bind(1,48000,0,0,1);input->push(1,0,sig->samples.data(),nullptr,int(sig->samples.size()),false);m->sampleInput=input;}
        else m->sampleInput=sig;
        m->run(4000000);
        {const auto& a=m->sampleAnalog;const auto& cv=m->controlVoltage.held[eii::ControlVoltageBus::Amplitude][0];const auto fc=m->reconstructionControl[0].activeCode();
         std::printf("reads %llu unknown %llu unkCmp %llu steps %llu held %d/%.3f iint %d/%.3f acv %d/%.3f code %02x/%02x conv %llu pg %d/%d\n",(unsigned long long)m->sampleReads,(unsigned long long)m->sampleUnknownReads,
          (unsigned long long)a.unknownComparisons,(unsigned long long)a.chainSteps,a.held().known,a.held().value,a.iint().known,a.iint().value,cv.known,cv.volts,fc.value,fc.known,(unsigned long long)m->sampleSar.conversions,m->scannerPioBPin(7).known,m->scannerPioBPin(6).known);}
        require(m->sampleReads==1024&&m->sampleUnknownReads==0,"1024 known SAR reads in two recordings");
        // Offline DPCM reconstruction with the settled 7-44 deglitch charge transfer.
        double iint=0;std::vector<double> y;
        for(unsigned i=0;i<512;++i){const std::uint8_t c=m->memory[0x2100+i];
            const double idac=eii::analog::Am6072::eiiSteadyVolts({c,255},{4.98046875,true}).value;
            iint-=(200*iint+10000*idac)/10200;y.push_back(iint);}
        double mx=-1e9,mn=1e9;unsigned crossings=0;
        for(unsigned i=256;i<512;++i){mx=std::max(mx,y[i]);mn=std::min(mn,y[i]);if(i>256&&(y[i-1]<0)!=(y[i]<0))++crossings;}
        std::printf("codes:");for(unsigned i=256;i<272;++i)std::printf(" %02x",m->memory[0x2100+i]);
        std::printf("\nreconstructed p-p %.3f V, zero crossings in 256 samples %u (expect ~%.1f), clips %llu\n",mx-mn,crossings,2*256/27.7778,(unsigned long long)m->sampleAnalog.railClips);
        // Loop theory: IINT -> (10000/10200) x 3.3 x Vin (C40 = IINT - 3.3 BUF), 0 dB.
        require(std::fabs((mx-mn)-2*0.5*0.990*3.3*10000/10200)<0.25,"DPCM reconstruction amplitude matches the 7-42/7-44 loop gain");
        require(crossings>=17&&crossings<=19,"1 kHz at 27.78 kHz sample rate");
        double secondMin=100,secondMax=-100,last=0;unsigned secondCrossings=0;
        double secondIint=0;
        for(unsigned i=0;i<512;++i){const double idac=eii::analog::Am6072::eiiSteadyVolts({m->memory[0x2300+i],255},{4.98046875,true}).value;secondIint-=(200*secondIint+10000*idac)/10200;if(i<256)continue;secondMin=std::min(secondMin,secondIint);secondMax=std::max(secondMax,secondIint);if(i>256&&(secondIint<0)!=(last<0))++secondCrossings;last=secondIint;}
        std::printf("second recording p-p %.3f V, crossings %u, chain steps %llu\n",secondMax-secondMin,secondCrossings,(unsigned long long)m->sampleAnalog.chainSteps);
        require(secondCrossings>=17&&secondCrossings<=19&&std::abs((secondMax-secondMin)-(mx-mn))<.25,"second recording after idle preserves tone and amplitude");
        if(argc>1){auto input=std::dynamic_pointer_cast<eii::vst::HostSampleInput>(m->sampleInput);std::printf("host missing reads %llu\n",(unsigned long long)input->missing.load());require(input->missing==0,"sampling resume does not replay discarded idle input");}
        // Exercise the exact fast-path boundary: leave IMODE when SAR is
        // already quiescent. Pin change is synthetic; re-entry is a real OUT.
        for(unsigned n=0;n<1000;++n){m->sampleSar.readStrobe(H);if(m->sampleSar.quiescent(L))break;m->run(1);}
        require(m->sampleSar.quiescent(L),"SAR reaches idle boundary for sampling exit");
        m->main.pio[1].output&=0x7f;m->run(100000);
        const auto steps=m->sampleAnalog.chainSteps;
        const std::uint8_t restart[]={0x3e,0x80,0xd3,0x32,0x76};
        std::copy(std::begin(restart),std::end(restart),m->main.rom.begin()+0x1000);
        m->main.pins=z80_prefetch(&m->main.cpu,0x1000);m->run(20000);
        const auto added=m->sampleAnalog.chainSteps-steps;
        std::printf("quiescent re-entry chain steps %llu (<=961 expected)\n",(unsigned long long)added);
        require(added<=961,"quiescent exit invalidates analog timeline before early return");
        if(argc>1){auto input=std::dynamic_pointer_cast<eii::vst::HostSampleInput>(m->sampleInput);std::printf("post-resume missing reads %llu\n",(unsigned long long)input->missing.load());require(input->missing==0,"no discarded input requested after quiescent resume");}

    }
    {   // Settled 6-tick-grid deglitch vs the Deglitch3202 oracle stepped every
        // source clock (0.1 us). 360-clock period, phi1 clocks 100..115, phi2
        // 120..135, DAC changing at SAR rising edges (every 30 clocks).
        SampleInputAnalog a;a.reset({4.98,true});eii::analog::Deglitch3202 o;o.resetKnownZero();
        std::uint32_t rng=12345;double idac=0,maxErr=0,rms=0;unsigned n=0;
        for(std::uint64_t sc=0;sc<360*400;++sc){
            if(sc%30==0){rng=rng*1664525u+1013904223u;idac=(double(rng>>8)/16777216.0-0.5)*0.8-0.02*o.output().value;}
            const unsigned ph=unsigned(sc%360);
            const eii::LogicLevel p1{true,ph>=100&&ph<116},p2{true,ph>=120&&ph<136};
            if(sc%15==0){SampleInputAnalog::Pins p{H,L,L,p1,p2,{0x3b,255},{4.98,true}};a.tick(p,nullptr,sc*2/5,{idac,true});}
            o.advance(0.1e-6,{idac,true},p1,p2);
            if(ph==359&&sc>3600){const double d=std::fabs(a.iint().value-o.output().value);maxErr=std::max(maxErr,d);rms+=o.output().value*o.output().value;++n;}
        }
        rms=std::sqrt(rms/n);std::printf("deglitch grid vs Deglitch3202 oracle: max |diff| %.2e V (signal rms %.3f V)\n",maxErr,rms);
        require(a.iint().known&&maxErr<1e-3*std::max(rms,0.1),"settled grid deglitch matches the per-clock oracle");
    }
    // LiveSampleInput (jitter buffer) has its own test: tests/sample_input_live_tests.cpp
    std::cout<<(failures?"FAIL":"PASS")<<": sample input analog path (gain states, S/H, LM311)\n";
    return failures?1:0;
}
