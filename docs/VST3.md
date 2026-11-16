# Current accepted release: VST3 0.1.2 mini golden

Owner accepted the delivered 0.1.2 PGO package on 2026-10-23 and authorized
saving to private feature/vst3. The sections below describe earlier development
and historical validation; they do not imply a fresh 0.1.2 validator run.
Current acceptance/build records: validation/vst3-012-background-editor-2026-10-23.md
and validation/vst3-012-build-manifest.json. Zoom is now in Settings > Editor size / zoom;
there is no extra toolbar. The accepted changes and remaining limits are recorded
in state.json and the opening milestone of memory.md.

---

# Emulator II VST3 instrument (Windows x64)

Separate implementation on branch `feature/vst3` (from 17b64b3). The standalone
sources, core and media code are untouched; the plug-in only adds `src/vst3/`,
`tools/build-win64-vst3.ps1`, `tools/vst3_host_test.cpp`,
`tests/vst3_engine_tests.cpp` and the vendored SDK subset `vendor/vst3sdk/`.

## SDK

Steinberg VST3 SDK v3.8.1_build_84 (MIT since 3.8), superproject commit
3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96; base fcf9da0b, pluginterfaces 4f547e8e,
public.sdk 586dc5e6 (details and kept subset: `vendor/vst3sdk/UPSTREAM.md`).
No VSTGUI. No SDK patches; the only MinGW/clang accommodation is
`-Wno-pragma-pack` for the SDK's intentional packing push/pop.
The plug-in compiles pluginterfaces plus `coreiids.cpp`, `funknown.cpp`,
`vstinitiids.cpp` and `src/vst3/vst3_iids.cpp` (IPlugView/IPlugFrame ids).

## Shape

One class `Emulator II`, category `Audio Module Class`, subcategory
`Instrument|Sampler`, one component that is also the edit controller (like
SingleComponentEffect: `getControllerClassId` = not implemented, never
`kDistributable`). Buses: event input `MIDI In` (16 ch), audio output `Main Out`
stereo. 32-bit float only. Tail = infinite (the sampler free-runs; hosts keep
calling process). No audio input bus yet.

Parameters: 0-5 = Slider A-D, Left/Right Wheel (0-5 V, automatable), 6 = Volume.
2096 hidden parameters 1000+ch*131+n are the IMidiMapping targets for CC0-127,
channel aftertouch (128), pitch bend (129) and program change (130); they are
turned back into the corresponding MIDI short messages.

## Threads and pacing

`src/vst3/plugin_engine.h` (PluginEngine) is a separate owner, modelled on
FrontPanelSession but not sharing its code path:

* One machine owner thread per instance constructs, boots and runs `Machine`
  (real firmware, production controller, same startup seeds as the GUI:
  phase0 / DRAM seed 1 / controller seed e1122026). The SSM2045 DSP worker of the
  nonlinear models stays inside `RuntimeAudioEngine` as in the standalone.
* The host audio thread never touches `Machine`. process() pushes timestamped
  events into a bounded SPSC queue (8192), publishes the block end as the machine
  target (atomic) and wakes the owner (Win32 auto-reset event), then reads frames
  from an index-addressed 48 kHz SPSC ring (131072 frames).
* Emulated time follows the host audio clock, never wall time. Host sample h of
  the current activation maps to CPU tick `G = anchorTick + h*4e6/rate` and to
  48 kHz stream frame `anchorFrame + h*48000/rate`. The owner runs the firmware
  continuously up to the latest published host sample: 5 ms runs (20000 ticks;
  3 x 96-frame DSP blocks for the SSM2045 models) while the lead is healthy,
  1 ms runs (one DSP block) once fewer than half the margin's frames are
  ready, stopping at each event's own tick. Panel contact edges keep >= 5 ms of
  emulated time between them, as with the standalone's quanta. Because output for host sample h is read at
  h - latency, the whole latency budget is lead between the read point and the
  run limit; frames ready beyond the very next block = latency - block = margin.
  Running past the published host sample would require events that the host has
  not delivered yet, so the machine never does: every event is applied exactly at
  its tick. Policy for an event whose tick is already behind the machine (only
  possible for events queued across a re-activation): applied at the next tick. CPU and every derived clock remain emergent; nothing is
  rescaled.
* Output for host sample h is the stream at h - latency. Realtime: a frame not
  yet produced is output as silence and counted (`late frames`); when it arrives
  it is discarded, so the stream never slips. Offline / freewheel
  (`processMode == kOffline`): process() waits for the owner (20 s guard), so
  bounces are complete and identical in timing.
* Reported latency (getLatencySamples) = max block + safety margin (default
  30 ms; Settings: 10/20/30/40 ms, stored in the project, host re-queries via
  kLatencyChanged) + interpolator delay: 48 kHz/256 -> 1696 samples, 48 kHz/512
  -> 1952; 44.1 kHz/441 -> 1795.
* The SSM2045 worker drops blocks when its 8-block queue overflows; the owner
  starts each quantum only when that queue is empty, so offline renders of the
  nonlinear models are drop-free too.
* Pipeline changes (boot, audio model, native DAC, pause) first drain the DSP
  worker and pad the stream so the frame index stays aligned with CPU ticks.
* `host_cpu_placement.h` soft CPU-set pinning assumes one machine per process.
  `InitDll` sets `EII_CPU_PLACEMENT=0` unless the user set it, so instances do
  not all claim the same two cores (measured, see validation). With
  EII_CPU_PLACEMENT=1 the machine thread is placed like the standalone too. The
  machine thread joins MMCSS "Pro Audio" (high) on top of ABOVE_NORMAL
  (EII_VST3_MMCSS=audio: the standalone worker's "Audio" class, =0: off;
  without MMCSS the owner ran ~40 % slower under load). EII_CPU_PLACEMENT=dynamic
  gives each instance's machine and DSP thread its own least-claimed physical
  core; it measured no better than the scheduler, so it stays off.
* Stall recovery: if the owner falls more than 100 ms behind the playback point
  (realtime only), emulated time is frozen over the gap (silence, like the
  standalone pacer's rebase) instead of catching up in silence; counted as
  "stall recoveries" in Settings > Status.

## Host sample rates

The machine always renders its emergent 48 kHz stream. At 48 kHz the stream is
copied bit-exactly. Other rates use a fixed-ratio windowed-sinc interpolator
(`src/vst3/resampler.h`): 64 taps, Kaiser beta 8, 1024 interpolated phases,
unity DC gain per phase, cutoff 0.94 x min(24 kHz, host Nyquist). Measured in
`vst3_engine_tests`: 1 kHz error < -90 dB, 18 kHz passband within 0.1 dB,
23 kHz attenuated > 60 dB at 44.1 kHz.

## MIDI

Note on/off, poly pressure (events) and CC/aftertouch/bend/program (mapped
parameters) are converted to MIDI short messages with their sample offsets and
delivered at the matching CPU tick through the standalone path:
`MidiHostTools::adapt` (optional wheel bridge / Note-Off-as-90/0) ->
`Machine::receiveMidi` -> 31250-baud wire -> SCN2651 -> CTC -> firmware.
Note-on velocity 0 is sent as 1 (a VST3 note-on is never a note-off). SysEx and
note expression are not forwarded.

## Editor

`src/vst3/plugin_editor.h`: the unchanged GUI v1.0 `VectorPanelWindow` is
created, restyled to WS_CHILD and reparented into a container child of the
host HWND. IPlugView resize contract: canResize, checkSizeConstraint keeps the
panel aspect ratio (following the dragged edge), onSize rescales the vector
drawing. Default 1500x582 at 96 dpi, multiplied by the host content scale
(IPlugViewContentScaleSupport) or, without one, the parent window's per-monitor
DPI; minimum width 784. The last size is stored in the project state. Panel keys/faders/knobs act like the standalone (faders and Volume also
report host automation edits). Host buttons: SETTINGS / DEVICES = menu (ROM
folder, floppy insert/save copy/eject, HDD select/create/none/save copy, direct
image writes, audio model Legacy/SSM96k/SSM192k, native DAC, S3528, MIDI wheel
bridge, Note Off as 90/0, auto-boot, realtime safety margin, status), FILTER SERVICE = the standalone
calibration window, BOOT / RESET, RUN / PAUSE (pause freezes emulated time),
ALL NOTES OFF. RECORD TRACE is standalone-only. Dialogs run on the UI thread
only. The LCD font is a copy of the standalone's procedural font
(`panel_glyphs.h`) so `windows.cpp` stays byte-identical. GDI+ is kept alive
for the module lifetime because the panel caches a GDI+ bitmap statically.

## Media protections

Same rules as `docs/media-write-policy.md`, executed on the owner thread between
quanta with the same guards: protected working copy by default; direct writes
only when enabled, through the exclusive `DirectImageFile` lease; dirty media
need save / discard / cancel before reset or eject (Yes/No/Cancel dialog);
copies always go to NEW files. Plug-in specific: when the host destroys an
instance or loads a project over dirty protected working copies, they are
written to new files in `%LOCALAPPDATA%\EmulatorII-VST3\recovery\`
(`<name>-unsaved-<timestamp>.<ext>`), never over the source. Direct-mode
images are already flushed per write.

## New instances

A new instance (no project state) starts from the standalone's per-user
preferences, read only from the registry: ROM folder, floppy, HDD, chip model,
native DAC, S3528, MIDI options, filter calibration, sliders, volume. Direct
image writes always start off. It boots on its first activation, so plug-in
scans that never activate do not start a machine. With no ROM folder, choose it
under SETTINGS / DEVICES and press BOOT / RESET.

## State

getState writes a small UTF-8 text blob (`EIIVST3 1`, see
`src/vst3/plugin_state.h`): ROM folder, floppy and HDD paths, direct-writes
flag, audio model, native DAC, S3528, MIDI options, auto-boot, safety margin,
editor size, the six control voltages, volume, input gain, last host program change and the
filter calibration. No ROM, disk, sample or machine RAM bytes (test state: 1286
bytes). setState with auto-boot reboots the same configuration, so the bank
loads from the stored floppy. Preset recall: the last program change the host
sent is replayed through the MIDI path once the firmware shows `Pnn` on the
LCD after boot (or immediately, if the project itself sends one). Panel-only
preset selection is not captured. Full machine RAM snapshots are NOT restored:
`Machine` has no complete serializer (CPU, devices, DRAM known-masks, media,
timers), so it is neither cheap nor safely verifiable.

## Build

    $env:LLVM_MINGW_ROOT='F:\tools\llvm-mingw-20261006-ucrt-x86_64'
    tools\build-win64-vst3.ps1 [-RomFolder Z:\EII\roms -Floppy 'Z:\EII\disk\18 Grand Piano #2.hfe']
    tools\build-win64-vst3.ps1 -ProfileData <eii.profraw|eii.profdata> ...   # PGO -> build\win64-vst3-pgo

Output `build\win64-vst3\EmulatorII.vst3\Contents\x86_64-win\EmulatorII.vst3`
(O3/LTO, static libc++, only system DLLs; exports GetPluginFactory, InitDll,
ExitDll). The script also builds and runs `vst3_engine_tests`, builds the
unmodified Steinberg validator and runs it on the bundle, and with ROM/floppy
runs `vst3_host_test`. The PGO variant reuses the standalone's owner-captured
compiler profile (core sources unchanged since 49c5a5f; plug-in code unprofiled).

## Validation

See `validation/vst3-2026-10-23.md`.

## Limits

* Windows x64 only; no macOS/Linux builds, no moduleinfo.json.
* No audio input bus yet (sampling from the host). Hook:
  `PluginEngine::sampleSource()` feeds `Machine::sampleInput`; a future input bus
  would push host input into a jitter buffer paced like the machine.
* Realtime needs the machine to run faster than real time on the host CPU; the
  SSM2045 192k model may not, exactly as in the standalone. Late frames are
  silent and counted (Settings > Status).
* Floppy/HDD writes, sample RAM and firmware state live only in the running
  instance; save copies or use direct writes before closing a project.
* The DSP worker's MMCSS/CPU-set behaviour is unchanged core code.
