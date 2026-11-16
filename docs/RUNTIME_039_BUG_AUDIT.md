# 0.39: note-release CPU bug and playback-probe correction

User-confirmed Windows0.38: VI61 MIDI input works;96 UART bytes read by the
original firmware with0 overruns/drops. User reports speed falling from about93%
to80–82%, and cannot choose the first sound-card output. Native Windows device
access is unavailable in the build environment. This is not a claim that the
reported Windows regression has been fully reproduced or fixed.

## Confirmed CPU defect

The provisional host-rate voice had no subnormal handling. After a note ends,
the amplitude-reference RC, filter and feedback states can decay into subnormal
double values and remain there. They still produce zero float PCM, but x64
floating-point assists make that arithmetic much slower. This is sustained
unnecessary work after release, not a MIDI packet queue or a device polling loop.

`host::AudioFloatScope` enables FTZ/DAZ only during `RuntimeAudioEngine::sample`
and restores the caller's MXCSR on return or exception. There is no arbitrary
audible-level threshold, fabricated signal, altered firmware, WAIT bypass or
change to the reference analog kernels. The implementation targets x64 SSE2;
other architectures currently retain their existing floating-point policy.

Reproduction without ROMs/disks:

```
g++ -std=c++20 -O3 -DNDEBUG -Isrc tools/audio_tail_benchmark.cpp -o /tmp/audio-tail
/tmp/audio-tail
```

Build the same tool against0.38 and0.39. It exercises eight real host voice
kernels, first held, then released, then a long silent tail. Measurements in
`validation/runtime-039-tail-comparison.json` are native Linux release results,
not a Windows CPU percentage or whole-emulator speedup. Normal active-signal
handling has a small scope-management overhead; the slow silent tail is removed.

The two silent-tail timings averaged11.6624s before and0.632252s after
(18.45x faster for this DSP-only case). One separate whole-machine pair, after
the same original-bank boot, averaged3.356482s vs3.104418s for each8M-clock
postboot stage:7.51% less wall time, with matching final memory/known/scanner
hashes and matching voice-transaction/frame counts. This one unpinned container
pair is an observation, not a guaranteed7.51% Windows gain or proof that FTZ
alone caused every whole-machine timing difference. Native0.37/0.38 idle
means were3.27303s/3.344485s; the reported10–12% Windows loss was not reproduced
by that idle comparison. Raw runs and scope are in runtime-039-stage-comparison.json.

## Invalid0.38 playback test setup

`tools/midi_play_probe.cpp` replaced `controllerTiming` after Machine construction
but omitted the matching reset release. The GUI did release it correctly. That
probe could boot the bank and receive MIDI while its sample-channel scan stayed
reset. Its zero PCM result was therefore an invalid diagnosis of the GUI path.
The old failure logs are retained as history, with this correction superseding
their sound-path conclusion.

Machine/ChannelTimers now accept the startup phase in their constructors. The
GUI and MIDI playback probe use the same initialization sequence. The probe
also requires actual voice transactions, so this setup failure cannot quietly
pass as a playback observation. A digital regression checks phase advancement.

## Remaining CPU work and output selection

The source-clock controller still runs at10MHz with20MHz half edges. The
denormal fix does not replace this expensive runtime architecture. Continue
the timed upper-arbiter/DRAM/WAIT/ninth-slot work in VOICE_TRANSACTIONS.md;
do not label this checkpoint a completed low-CPU emulator.

A corrected0.38 postboot `-pg` profile with diagnostics compiled out still
executes100million upper-controller rises for40million CPU ticks. Sampled
`z80_tick` self time is about4%; instrumentation, inlining and helper-symbol
aliasing limit attribution. The warm controller loop, rather than an unbounded
MIDI message drain, remains the main architectural target. See
validation/runtime-039-postboot-profile.txt; these are not Windows CPU figures.

The Windows endpoint selector includes the system default followed by every
active WASAPI render endpoint. Index0 is a host endpoint-list entry, unrelated
to emulated voice0. There is currently no ASIO or multichannel stereo-pair
selector. The reported first-output failure is still unconfirmed: an absent
endpoint, a stream-initialization error, and a missing1/2 stereo-pair choice
require different fixes. No speculative device-routing change is included.
