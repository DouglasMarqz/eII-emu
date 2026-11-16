#pragma once
#include <array>
#include <cstdint>
#include "channel_requests.h"

namespace eii {
// Output board 7-41: IC26 HCT151 and IC27 HCT138. Electrical levels;
// the ACK decoder's output-to-channel wiring is rotated by one position.
class ChannelServiceBus {
public:
    std::uint8_t selection=0,selectionKnown=0;
    LogicLevel acknowledgeEnable{},ureq{};
    std::array<LogicLevel,8> acknowledge{};
    ChannelServiceBus() {decodeAcknowledge();}
    void drive(std::uint8_t value,std::uint8_t known,LogicLevel enable) noexcept {
        selection=value&7;selectionKnown=known&7;acknowledgeEnable=enable;
        decodeAcknowledge();
    }
    void requests(const std::array<LogicLevel,8>& inputs) noexcept {
        if(selectionKnown==7) { // single possible input: the general merge reduces to this
            const auto in=inputs[selection];ureq={in.known,!in.known||in.high};return;
        }
        bool zero=false,one=false;
        for(unsigned selected=0;selected<8;++selected) if(possible(selected)) {
            const auto input=inputs[selected];
            zero|=!input.known || !input.high;
            one|=!input.known || input.high;
        }
        ureq={!(zero&&one),one};
    }
private:
    bool possible(unsigned value) const noexcept {
        return ((value^selection)&selectionKnown)==0;
    }
    void decodeAcknowledge() noexcept {
        for(unsigned channel=0;channel<8;++channel) {
            // Y0 -> -CHAK7, Y1 -> -CHAK0, ... Y7 -> -CHAK6.
            const unsigned code=(channel+1)&7;
            if((acknowledgeEnable.known && !acknowledgeEnable.high) || !possible(code))
                acknowledge[channel]={true,true};
            else if(acknowledgeEnable.known && acknowledgeEnable.high && selectionKnown==7)
                acknowledge[channel]={true,false};
            else acknowledge[channel]={false,false};
        }
    }
};
}
