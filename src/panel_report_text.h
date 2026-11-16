#pragma once
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace eii::host {
// Display policy only. Does not consume or modify the emulated UART history.
// Normal snapshots carry constant text; live serial is explicit and bounded.
class PanelReportText {
public:
    static constexpr std::size_t serialTailBytes=512;
    static constexpr std::string_view offMessage=
        "Live serial view is off. State Capture produces a full manual report.";
    const std::string& text()const noexcept{return text_;}
    std::uint64_t revision()const noexcept{return revision_;}
    void reset(){mode_=Mode::Empty;serialSize_=std::numeric_limits<std::size_t>::max();text_.clear();++revision_;}
    bool serial(std::string_view bytes,bool enabled) {
        if(!enabled){
            if(mode_==Mode::Off)return false;
            mode_=Mode::Off;text_=offMessage;++revision_;return true;
        }
        // Machine::serialTx is append-only during one boot, capped at64KiB.
        // Reset this view on boot and on explicit view changes.
        if(mode_==Mode::Serial && serialSize_==bytes.size())return false;
        mode_=Mode::Serial;serialSize_=bytes.size();
        const auto offset=bytes.size()>serialTailBytes?bytes.size()-serialTailBytes:0;
        text_="LIVE SERIAL TX: last "+std::to_string(bytes.size()-offset)+" of "+std::to_string(bytes.size())+
              " captured bytes (register-level view)\n"+escape(bytes.substr(offset));
        ++revision_;return true;
    }
    void capture(std::string_view report){mode_=Mode::Capture;text_=escape(report);++revision_;}
    void diagnostic(std::string_view report){mode_=Mode::Diagnostic;text_=escape(report);++revision_;}
private:
    enum class Mode{Empty,Off,Serial,Capture,Diagnostic};
    Mode mode_=Mode::Empty;std::size_t serialSize_=std::numeric_limits<std::size_t>::max();
    std::uint64_t revision_=0;std::string text_;
    static std::string escape(std::string_view input) {
        std::string out;out.reserve(input.size());constexpr char hex[]="0123456789abcdef";
        for(unsigned char byte:input){
            if(byte=='\n'||byte=='\r'||byte=='\t'||(byte>=32&&byte<127))out+=char(byte);
            else{out+="\\x";out+=hex[byte>>4];out+=hex[byte&15];}
        }
        return out;
    }
};
}
