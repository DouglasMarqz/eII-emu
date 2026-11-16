#include "midi_host_tools.h"
#include <iostream>
static void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int main()try {
    using namespace eii::host;
    MidiHostTools tools;
    const std::array<std::uint8_t,3> off{0x82,60,64},center{0xe3,0,64},mod{0xb7,1,127};
    require(tools.adapt(off).bytes==off && !tools.adapt(center).wheel,"raw defaults preserve MIDI/physical wheel independence");
    tools.noteOffZero=true;auto packet=tools.adapt(off);
    require(packet.bytes==std::array<std::uint8_t,3>{0x92,60,0},"optional canonical note-off preserves channel and pitch");
    tools.wheelBridge=true;
    require(tools.adapt(center).wheel->volts==2.5 && tools.adapt(center).wheel->control==4,"14bit pitch center to physical left wheel");
    require(tools.adapt(std::array<std::uint8_t,3>{0xe0,0,0}).wheel->volts==0,"pitch minimum");
    require(tools.adapt(std::array<std::uint8_t,3>{0xe0,127,127}).wheel->volts<5,"pitch maximum bounded below rail");
    require(tools.adapt(mod).wheel->control==5 && tools.adapt(mod).wheel->volts==5,"CC1 to physical right wheel");
    require(!tools.adapt(std::array<std::uint8_t,3>{0xb0,64,127}).wheel,"sustain is not modulation");
    tools.observeEnqueued(std::array<std::uint8_t,3>{0xb2,64,127});
    tools.observeEnqueued(std::array<std::uint8_t,3>{0xbf,64,64});
    require(tools.sustainSeenMask==0x8004 && tools.sustainDownMask==0x8004,"host tracks per-channel enqueued sustain threshold");
    tools.observeEnqueued(std::array<std::uint8_t,3>{0xb2,64,63});
    tools.observeEnqueued(std::array<std::uint8_t,3>{0xbf,1,0});
    require(tools.sustainDownMask==0x8000,"sustain-up preserves other channels; CC1 does not clear pedal");
    const auto release=MidiHostTools::releaseAllMessages();
    require(release.size()==32,"release sends two messages on each of16 channels");
    for(unsigned ch=0;ch<16;++ch) {
        require(release[2*ch]==std::array<std::uint8_t,3>{std::uint8_t(0xb0|ch),64,0} &&
                release[2*ch+1]==std::array<std::uint8_t,3>{std::uint8_t(0xb0|ch),123,0},"pedal release precedes all-notes-off on every channel");
        tools.observeEnqueued(release[2*ch]);tools.observeEnqueued(release[2*ch+1]);
    }
    require(tools.sustainSeenMask==65535 && tools.sustainDownMask==0,"release clears all host-observed pedal-down states");
    tools.resetInputState();require(!tools.sustainSeenMask&&!tools.sustainDownMask,"boot clears observations without inventing firmware state");
    tools.observe(off,packet,1,2,3,true);require(tools.total==0,"capture disabled by default");
    tools.record(true);
    for(unsigned n=0;n<4097;++n)tools.observe(off,packet,n,n+5,n*20000,n%2);
    const auto trace=tools.exportTsv();
    require(tools.total==4097 && tools.overwritten==2049,"bounded trace overwrites oldest events");
    require(trace.find("823c40\t923c00")!=std::string::npos,"trace preserves source and wire data");
    require(std::count(trace.begin(),trace.end(),'\n')==2051,"bounded trace exports2048 rows plus header");
    tools.record(false);tools.observe(off,packet,0,0,0,true);require(tools.total==4097,"stopped capture preserves data");
    bool rejected=false;try{tools.adapt(std::array<std::uint8_t,3>{0xe0,255,0});}catch(...){rejected=true;}
    require(rejected,"invalid data bytes cannot drive analog wheel outside range");
    std::cout << "PASS: raw defaults; optional note-off normalization; pitch/CC1 physical wheel adapter; bounded disabled-by-default MIDI capture and malformed-data guard\n";
}catch(const std::exception& e){std::cerr << e.what() << '\n';return 1;}
