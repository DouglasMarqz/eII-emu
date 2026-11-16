#pragma once
#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace eii::host {
// Host PCM boundary only. Recursive filters can spend the rest of their
// lifetime processing subnormal tails after a note ends. On x64 those
// otherwise inaudible values can trigger slow floating-point assists.
// Preserve the caller's FP environment, including on exceptions; the
// reference analog experiments and firmware/controller execution stay outside.
class AudioFloatScope {
public:
    AudioFloatScope() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
        saved_ = _mm_getcsr();
        if ((saved_ & flushMask) != flushMask)
            _mm_setcsr(saved_ | flushMask);
#endif
    }
    ~AudioFloatScope() {
#if defined(__x86_64__) || defined(_M_X64)
        if ((saved_ & flushMask) != flushMask)
            _mm_setcsr(saved_);
#endif
    }
    AudioFloatScope(const AudioFloatScope&) = delete;
    AudioFloatScope& operator=(const AudioFloatScope&) = delete;
private:
#if defined(__x86_64__) || defined(_M_X64)
    static constexpr unsigned flushMask = 0x8040; // FTZ + DAZ (SSE2/x64)
    unsigned saved_ = 0;
#endif
};
}
