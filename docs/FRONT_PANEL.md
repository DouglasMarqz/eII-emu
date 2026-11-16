# Firmware front panel — 0.38 MIDI-input preview source

A thin native Win64 front panel now uses the actual emulator LCD, read-only
media chain and scanner controls. No browser or simulation of OS screen text
is involved. The timing/firmware model is retained;0.36 optimizes the controller/timer implementation.
The HFE importer is unchanged0.34.

No0.36 Windows package is released. The last delivered package is0.35,
confirmed to boot/display correctly by the user but with excessive CPU usage.
The instructions below describe the pending0.36 UI source; see
`RUNTIME_PERFORMANCE.md` for the unresolved runtime architecture work.

## Pending Windows UI

1. Build the source with `tools/build-win64.ps1`; run its front-panel executable.
2. Choose **ROM folder...**, then your prepared `main.bin`. The same folder
   must contain `scanner.bin`, `ic135.bin`, `ic137.bin`; optional diagnostics
   require `diag.bin`. The tested combination is main2.1/scanner3.1 plus the
   supplied PROMs. No firmware/disks are bundled.
3. Choose **Disk image...**, selecting the original Grand Piano HFE or a
   complete EMUIIFD image. Selection is staged until **Boot / reset**.
4. Leave the explicitly labeled **Boot experiment: RAM seed1 / controller
   phase0** selected, then press **Boot / reset**. It starts running.
5. Watch the actual2x16 LCD, simulated time, DMA count and drive status.
   **Pause**, **Run** and **Step1s** operate on this same running machine.
   One simulated second means4million CPU ticks; Run targets real time and
   shows the actual speed percentage. Step1s runs an exact unpaced step.
6. Panel buttons pulse actual scanner contacts for200ms simulated time;
   sliders A..D and wheels drive the corresponding ADC pin over0..5V.
   Run the machine to consume these inputs. The serial command box queues
   register-level diagnostic input; wait for the ROM prompt as before.

The second startup option retains unknown RAM/all16 controller phases for
verification. It can intentionally stop on an unknown DRAM read; it is not
an alternative supported musical boot preset. Neither option characterizes
physical RAM startup or the real board's absolute clock phase.

ROM/disk paths, diagnostic selection and startup selection apply on reset;
**No disk** stages an empty selection, also applied on reset. Loading a bad
image reports an error and preserves the prior machine in a paused state.
The input HFE is decoded/validated in memory; no disk export or media write
is performed. HFE timing is reconstructed using the existing nominal FM
profile, not a direct replay of original transitions.

The pending Windows source remembers the ROM folder, staged disk (including
**No disk**), diagnostic/startup choices, six slider positions and selected
WASAPI endpoint between launches. These are per-user preferences under
`HKCU\Software\Tuth\EIIEmulator\FrontPanel`; no administrator rights are
needed. Choosing paths, changing options, releasing a slider, booting and
closing save preferences. Restore does not boot automatically or restore an
emulated machine snapshot. Media is still validated on Boot / reset. A missing
audio endpoint falls back to the system default; audio playback remains pending.
The 0.36e preference change was initially source-reviewed only. It is now
included in the cross-compiled 0.37 preview; native Windows persistence testing
is still pending.

Windows verification still required: choose Unicode ROM/disk paths and a
nondefault audio device, change sliders/options, close/reopen and check restored
values; verify **No disk** persists and missing media fails on boot without
discarding a running machine. Verify fresh-user defaults without a registry key.

## LCD and controls

The renderer displays actual LCD DDRAM ASCII rows and display-enable state
as scalable5x8 dot cells. Its readable font is our own approximation; it is
not a recovered hardware CGROM. Custom characters, cursor/blink and LCD busy
cycle timing remain absent. The window is a functional layout, not a
replica of the physical EII geometry. The report includes actual LED raw
values/known masks; serial nonprintable bytes are escaped.

All30 named panel buttons use the existing verified matrix map. A click
queues a physical-contact pulse; no OS function is called directly. Six
trackbars target ADC IN4..7 (A..D), IN2(left), IN3(right). External slider
positions are retained on reset; internal ADC/latch state is not forced.
Keyboard key/contact order and note timing remain the next hardware task.
The earlier panel lacked host MIDI/audio. Current0.38 has MIDI IN and a WASAPI
stream/basic VCF/VCA, with real-bank PCM stillzero; see MIDI_INPUT.md. VST3 is absent.
The Filter menu can ask **No Current Voice / Play a Key**, which needs the
upcoming physical keyboard path.

## Ownership and responsiveness

`FrontPanelSession` confines Machine ownership to one worker. The UI only
queues typed commands and copies mutex-protected value snapshots. It never
reads a mutating Machine. Work is split into20000-tick chunks so pause and
shutdown can be handled between chunks; HFE validation can take longer.
Snapshots publish around100ms wall time and on commands/step completion.
No cross-thread HWND access or custom-message pointer ownership is required.

Boot validates a fresh machine/media before replacement. Failure clears run
budgets and preserves the old paused machine. Button releases use simulated
time, including through pause/resume, and reset drops old pending contacts.
The worker is joined before its state is destroyed. Run is unrestricted;
a sustained strict WAIT does not freeze other emulated chips or the UI.

## Validation and limits

Native tests use synthetic Z80 firmware to write the LCD and poll the physical
Filter contact. They verify pulse/release through actual scanner I/O, exact
stepping, pause/resume, slider ADC pins, failed-load rollback, startup choices,
serial-byte escaping and invalid-command rejection. Full-source ASan/UBSan
runs use `detect_leaks=0` because this environment cannot enumerate LSAN process
state. No thread-sanitizer claim is made.

`front_panel_probe` exercises the exact worker with real ROMs and the original
GrandPiano HFE. It reaches **Software Rev.3.1** then **P01 GrandPiano1**;
clicking Filter through the worker produces **No Current Voice / Play a Key**.
The probe then sets sliderA2.5V and pauses. See the source-only validation log
and provenance JSON. The prior0.34 independent145-block DMA/CRC audit remains
applicable to the unchanged core; this GUI run does not repeat that oracle.

The final PE32+ x86-64 GUI is cross-compiled using LLVM-MinGW20261006 UCRT,
C++20-O2-Wall-Wextra-static, with zero diagnostics. **It has not been run on
native Windows here.** No screenshot, visual-runtime, performance, audio or
physical calibration claim follows from compiling it. CMake/ctest are absent;
CMake/native/PowerShell targets are updated, direct compiler commands executed.

Reproduce the worker validation with:

```
build/front_panel_session_tests
build/front_panel_probe ../roms '../upload/18 Grand Piano #2.hfe'
```

Windows native build/test entry point remains `tools/build-win64.ps1` with
LLVM_MINGW_ROOT set. It now links comctl32 and builds/runs the panel tests.


##0.36 performance and output selection

See docs/RUNTIME_PERFORMANCE.md for measured release comparisons and limits.
The user reports0.35 successfully boots on Windows and confirms the LCD is
correct.0.36 retains that LCD renderer. It adds measured speed/real-time pacing
and a selectable WASAPI endpoint list below the controls. The displayed sample
rate/channel count comes from the selected Windows endpoint. Audio frames are
delivered by the 0.37 event-driven WASAPI basic VCF/VCA route; calibration and resonance
remain pending. See HOST_AUDIO.md for exact scope, muting and validation.
Enumeration, playback and pacing have not been run on native Windows here.

## 0.36e runtime selection

The unreleased worker now selects Machine::enableNormalRuntime for the explicit
phase0/seed1 startup. Unknown/all16 retains reference execution. This corrects
a missing production-path configuration; see RUNTIME_ROUTE_AUDIT.md for actual
worker equivalence tests and the remaining below-real-time limitation.

## 0.37 instrumentation and host stream

See HOST_AUDIO.md. The preview excludes optional PC/IO histograms, trace calls
and the periodic full GUI report at compile time. It connects the actual held
DAC/CV data to a bounded 48k stereo WASAPI stream, using the existing basic SSM2045 filter/VCA kernels. The
CV calibration, resonance and MIDI input remain incomplete. Windows cross-
compilation is recorded; native playback and CPU validation remain pending.

## 0.38 MIDI IN

MIDI input port selection, refresh, saved choice, All notes off and receiver/FW
read counters are available below audio. Notes enter the actual SCN2651/CTC1
firmware path. Real-bank reception/envelopes are verified; PCM remainszero, so
this is a trigger/diagnostic preview rather than an audible-bank milestone.
See MIDI_INPUT.md for scope and tests. LED graphics and CPU redesign remain pending.
