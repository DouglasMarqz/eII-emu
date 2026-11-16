#pragma once
// Class id and the private diagnostics interface shared by the plug-in and the
// headless host test. The diagnostics interface is read-only status text; DAWs
// never query it.
#include "pluginterfaces/base/funknown.h"
#define EII_VST3_VERSION "0.3.22-custom-fast-panel"
namespace eii::vst {
inline const Steinberg::TUID processorUid=INLINE_UID(0x5E2A7C31,0x9B4D4E6F,0xA1C3E2D9,0x0E11CE03);
struct IEiiDiagnostics : Steinberg::FUnknown {
    // Fills buffer with "key=value" lines (LCD, ticks, counters); returns length.
    virtual Steinberg::int32 PLUGIN_API statusText(char* buffer,Steinberg::int32 size)=0;
    inline static const Steinberg::TUID iid=INLINE_UID(0x5E2A7C31,0x9B4D4E6F,0xA1C3E2D9,0x0E11D1A6);
};
}
