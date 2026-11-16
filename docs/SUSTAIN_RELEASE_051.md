# Sustain release checkpoint 0.51

The host All Notes Off button previously sent CC123 on all 16 MIDI channels. With the real firmware, CC123 does not release notes retained by CC64 sustain. The button now sends CC64=0 followed by CC123=0 for each channel. All bytes travel through the existing timed UART and firmware; no forced voice reset or DSP mute is introduced. Original envelope release tails remain audible.

Manual state capture now records the channel masks for successfully enqueued CC64 messages. These are host observations, not firmware pedal-state readback. A successful reboot clears the observations. This adds no per-CPU-clock instrumentation.

## Reproduction and validation

Native Release, actual firmware, supplied bank, concrete phase0/DRAM seed1/controller seed0xe1122026, production engine. Select P04, sustain on, note60 and64 velocity127, hold5s. Fork the same machine state for an old/fixed comparison, then send both note-offs and the release command; run8s.

| Path | Maximum amplitude CV after8s | UART bytes received/read | Overruns |
|---|---:|---:|---:|
| Old CC123 only | 4.82422V | 54/54 | 0 |
| CC64=0 then CC123 | 0.46875V | 102/102 | 0 |

Both paths retained valid production state and finite PCM. See validation/tests-051-sustain-release.txt. The native-only tools/sustain_release_probe.cpp reproduces this comparison; caller supplies ROM folder and bank image. Build with C++20, src/chips.cpp and src/machine.cpp, -O3 -flto -DNDEBUG -DEII_DIAGNOSTICS=0 -pthread -Isrc -Ivendor/chips.

MIDI helper tests passed both optimized and ASan/UBSan (leak detection disabled). Worker/session tests passed, including host CC64 observations and all-channel release enqueueing.

## Limits

This confirms a defect in the host release button and reproduces a similar sustained-voice condition. The user's exact historical input sequence was not captured, so its cause is not proven. Enable Capture MIDI before reproducing; save Capture state and Save trace at failure. P04 is a slow PAD; its normal attack/release is not itself a defect.

Earlier native preset-selection alarm corrected: this bank contains only P01 through P04; P05 NOT FOUND was a legitimate firmware response, not a frozen UI.

Analog DSP, SSM2045 resonance, S3528 reconstruction and host mix headroom are unchanged. Separately observed clipping when held voices overlap a new preset remains open. A successful cross-build is not Windows runtime or listening validation.
