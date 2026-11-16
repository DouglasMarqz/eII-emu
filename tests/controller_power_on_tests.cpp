#include "machine.h"
#include <cassert>
#include <iostream>
static std::unique_ptr<eii::Machine> machine() {
    auto m=std::make_unique<eii::Machine>();
    m->channelTimers.controllerTiming=eii::ControllerTiming(0);
    m->channelTimers.enableControllerTiming();
    eii::PromLatch p;p.upper.fill(0xc0);p.lower.fill(0x4a);
    m->channelTimers.controllerTiming.loadProms(p);m->enableCpuDram();
    return m;
}
template<class F> static void rejects(F fn) {bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main() {
    auto a=machine(),b=machine();
    assert(a->channelTimers.controllerTiming.fullyKnownRegisters()==0);
    a->initializeControllerPowerOn(0x12345);b->initializeControllerPowerOn(0x12345);b->initializeControllerPowerOn(0x12345);
    assert(a->channelTimers.controllerTiming.fullyKnownRegisters()==64);
    for(unsigned i=0;i<64;++i)assert(a->channelTimers.controllerTiming.registerValue(i)==b->channelTimers.controllerTiming.registerValue(i));
    const auto before=a->report();rejects([&]{a->initializeControllerPowerOn(0);});assert(a->report()==before);
    b->channelTimers.enableEventTiming();b->enableRuntimeDevices();b->channelTimers.controllerTiming.enableConcreteRuntime();
    a->run(10000);b->run(10000);assert(a->report()==b->report());
    rejects([&]{a->initializeControllerPowerOn(1);});
    auto c=machine();c->channelTimers.advanceHalfClocks(1);const auto sourceBefore=c->report();
    rejects([&]{c->initializeControllerPowerOn(1);});assert(c->report()==sourceBefore);
    auto d=machine();d->registerStaging.write(0x48,3);const auto ioBefore=d->report();
    rejects([&]{d->initializeControllerPowerOn(1);});assert(d->report()==ioBefore);
    eii::ControllerTiming all;rejects([&]{eii::CpuRegisterStaging s;all.initializePowerOn(1,s);});
    std::cout<<"PASS:unknown default, deterministic pre-clock controller realization, repeated initialization, event/reference execution and transactional rejection guards\n";
}
