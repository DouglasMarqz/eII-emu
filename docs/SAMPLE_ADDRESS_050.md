# 0.50: shared ALU addend wiring and reverse sample traversal

The user reported rough, noise-like sound on preset02 of the supplied bank,
with production invalid0, MIDI bytes all consumed, no UART overrun and pacing
near real time. Those counters do not establish correct sample data.

## Root cause

Service-manual sheet7-22 (PDF85), IC123..127: the vertical conductor joins
**all eighteen A19..10 and A7..0 inputs to UCD04.1A**. Only IC125 A1/A0
(the overall A9/A8 pins) join UCD04.1B. The earlier implementation mistakenly
treated the remaining sixteen A inputs as grounded. Both the reference and
production engines contained that error; agreement between them could not
prove the wiring correct.

Correct arithmetic is:

`B + (A4a ? 0xFFCFF : 0) + (A4b ? 0x300 : 0) + CI`

The low20 bits drive the sum; carry OUT still drives RFD23. Once the two
UCD04 latches share a sample, the total addend is0 or0xFFFFF, plus CI:

| UCD04 A/B | CI | Low20 action | Carry |
| --- | --- | --- | --- |
| 0/0 | 0 | hold | 0 |
| 0/0 | 1 | increment | 1 only on overflow |
| 1/1 | 0 | decrement | 1 except when B=0 |
| 1/1 | 1 | hold | 1 |

For B=0x12345, UCD04 A/B=1/1, CI=0, the old path produced0x13245 instead
of0x12344 with carry1. Repeated sample traversal therefore jumped forward
rather than walking backward. No firmware patch, sample substitution, guard
suppression, gain reduction or analog smoothing was used to correct it.

## Controlled original-bank reproduction

`tools/sound_path_probe.cpp` boots the supplied original firmware/media using
the GUI's explicit concrete startup, selects01 or02 with actual panel contacts,
checks the LCD selection, sends MIDI note60/velocity100, and observes one second
at48kHz. The single-note experiment has no host audio-device queue. It records
held DAC/CV values, each provisional voice output, the unclamped sum and actual
sample-CAS address counts. The same initial boot and panel/MIDI timing are used
before and after the wiring change. Source lab hooks compile only with
`EII_SOUND_PATH_LAB`; ordinary Release and diagnostic builds contain no hook.

| One-second observation | Preset01 before/after | Preset02 before | Preset02 after |
| --- | --- | --- | --- |
| Frames | 48000 / 48000 | 48000 | 48000 |
| Actual sample CAS reads, all channels | 222221 / 222221 | 222221 | 222221 |
| Reads below0x2800, physical startup DRAM | 0 / 0 | 535 | 0 |
| Sum peak before host clamp | .781777 / .781777 | 2.76004 | .129687 |
| Frames exceeding host unity | 0 / 0 | 546 | 0 |
| RMS before host clamp | .0806979 / .0806979 | .229712 | .0298609 |
| DAC0 adjacent-sample correlation | .850782 / .850782 | .444762 | .917594 |

Preset01's entire DAC/CV/voice/sum CSV is byte-identical before/after.
Correlation is only a descriptive diagnostic, not a fidelity score. Lower RMS
is not a gain adjustment: all analog code and host gain remain unchanged.
The pre-fix isolated negative control fails the physical decrement assertion
(`13245 expected812344`, where bit23 denotes carry).

## Validation and reproduction

The ALU oracle now walks the five physical four-bit adders individually,
including IC125's split A inputs. Tests compare both engines to that oracle,
exercise forward/reverse/hold/carry and20-bit wrap, and retain46656 exhaustive
partial-state/control cases. The parallel carry test now includes the complete
0..0x100000 addend range:404976 serial/parallel cases. This also covers the
all-ones-plus-CI hold case. Existing unknown-state and latch-edge behavior is
retained; shared latch equality is established only by a joint sample.

The full native script exited0 with47 passing test executables and no warnings.
The ALU test also passed ASan/UBSan (leak detection disabled); the final pin/carry
assertions passed a separate optimized run. Win64 Release cross-compilation
exited0 without warnings. These are not Windows device/listening tests.

Run the ordinary suite with `bash tools/build-native.sh`. The lab is POSIX-only:

```
g++ -std=c++20 -O3 -flto=auto -DNDEBUG -DEII_DIAGNOSTICS=0 \
  -DEII_SOUND_PATH_LAB -pthread -Isrc -Ivendor/chips \
  src/chips.cpp src/machine.cpp tools/sound_path_probe.cpp -o build/sound_path_probe
build/sound_path_probe ROM_FOLDER ORIGINAL_BANK_IMAGE [--stress]
```

Create `build/050` first; captures are diagnostic local outputs, not bundled
sample assets. Optional stress sends96 note-on/off pairs,25ms between successive
messages, then observes two seconds of release. Both presets completed96 pairs with invalid0, UART576received/576read/0overrun
and zero low-DRAM sample reads. It is not the user's exact WinMM sequence. Retained results are in `validation/tests-050-*` and
`validation/sound-address-050.json`.

## Remaining limits

This corrects a demonstrated digital addressing defect. It does not certify
all presets, analog gain, absolute pitch, deglitch timing, hardware calibration
or Windows device behavior. The basic SSM2045 remains zero-Q; S3528 remains
bypassed in the production audio path. Their integration remains separate work.
The exact earlier zero-overrun stuck-note sequence has not been replayed.
