#include "ctc_event_runtime.h"
#include <iostream>
#include <stdexcept>
static void require(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
int main() try {
    z80ctc_t c{};z80ctc_init(&c);
    auto& high=c.chn[0];auto& busy=c.chn[1];auto& low=c.chn[2];
    high.int_vector=0xe0;busy.int_vector=0xe2;low.int_vector=0xe4;
    busy.int_state=Z80CTC_INT_SERVICED|Z80CTC_INT_NEEDED;
    low.int_state=Z80CTC_INT_NEEDED;
    auto pins=eii_ctc_irq(&c,Z80CTC_IEIO);
    require(!(pins&Z80CTC_INT),"in-service channel must not interrupt itself");
    require(!(pins&Z80CTC_IEIO),"in-service channel blocks downstream channels");
    require(busy.int_state==(Z80CTC_INT_SERVICED|Z80CTC_INT_NEEDED),"pending event retained until RETI");
    const auto ack=std::uint64_t(Z80CTC_IEIO|Z80CTC_M1|Z80CTC_IORQ);
    pins=ack;Z80CTC_SET_DATA(pins,0xff);pins=eii_ctc_irq(&c,pins);
    require(Z80CTC_GET_DATA(pins)==0xff,"in-service channel cannot supply a second ACK vector");
    high.int_state=Z80CTC_INT_NEEDED;
    pins=eii_ctc_irq(&c,Z80CTC_IEIO);
    require(pins&Z80CTC_INT,"higher-priority channel may preempt");
    pins=eii_ctc_irq(&c,ack);
    require(Z80CTC_GET_DATA(pins)==0xe0 && (high.int_state&Z80CTC_INT_SERVICED),"higher-priority vector acknowledged");
    pins=eii_ctc_irq(&c,Z80CTC_IEIO|Z80CTC_RETI);
    require(!high.int_state && (busy.int_state&Z80CTC_INT_SERVICED) && !(pins&Z80CTC_INT),"first RETI releases only nested higher-priority service");
    pins=eii_ctc_irq(&c,Z80CTC_IEIO|Z80CTC_RETI);
    require((pins&Z80CTC_INT) && !(busy.int_state&Z80CTC_INT_SERVICED),"second RETI releases retained event");
    pins=eii_ctc_irq(&c,ack);
    require(Z80CTC_GET_DATA(pins)==0xe2 && busy.int_state==Z80CTC_INT_SERVICED,"retained event serviced once");
    busy.int_state|=Z80CTC_INT_NEEDED;
    pins=eii_ctc_irq(&c,ack);
    require(busy.int_state==(Z80CTC_INT_SERVICED|Z80CTC_INT_NEEDED) && !(pins&Z80CTC_INT),"repeated ACK clocks do not reenter in-service channel");
    std::cout<<"PASS: CTC self-interrupt inhibition, retained pending event, higher-priority nesting, ACK and RETI ordering\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
