# Sample input (channel 0 sampling): hardware facts and implementation plan

Phase 1 research and design only. No emulator code is changed by this
document. Branch `perf/cpu-placement-072`, head a78773a.

Source manual: user-supplied service manual, 151 scanned pages, SHA-256
f1071d347818ecfd107df0808ceade6d9e460987a85f86a04c71707a82700031 (same file
as SAMPLE_TRANSPORT.md). OCR was used only to locate pages. Every schematic
claim below was read from rendered page images (200 dpi; crops for detail).
Renders live outside git in `F:\Claude\_assets\manual\pages\`.

Pages used (PDF page / printed page):

| PDF | Printed | Content |
| --- | --- | --- |
| 8 | 1-6 | Front panel SAMPLE/GAIN text |
| 17 | 2-7 | Right control panel: Sample Gain pot |
| 19 | 2-9 | Main CPU: handles sampling |
| 27 | 2-17 | Output channels / The Sample Channel |
| 43, 44 | 4-4, 4-5 | Sample offset trim (RT1, IC75 pin 1) |
| 72 | 7-9 | Scanner PIO IC46 port B6/B7 -> PGAIN |
| 74 | 7-11 | Main I/O decoder IC80/IC109 -> -PCSRIN |
| 76 | 7-13 | Main Z80-DMA IC82 RDY <- -INPRDY |
| 104 | 7-42 | Output: Input Analog |
| 105 | 7-43 | Output: SAR and logic |
| 106 | 7-44 | Output: Channel 0 |
| 119, 120 | net list | Digital board: IMODE/INPRDY/IWAIT/PGAIN source/destination sheets |
| 124 | net list | Output board: input net descriptions |

## 1. Hardware facts

### 1.1 Overview (PDF 27 / 2-17, PDF 19 / 2-9)

Channel 0 is "the Sample Channel": a normal output channel plus extra logic
that lets its own DAC act as a successive-approximation A/D (7-43). The
result is read back by the CPU (main CPU: "sampling ... functions"). The
analog input has an anti-aliasing filter and programmable gain (7-42).
There is no separate ADC chip: the converter is the channel-0 Am6072 +
LF311 comparator + MC14559B SAR register.

### 1.2 Analog input path (7-42, PDF 104)

1. **Jack and GAIN pot.** `+INPUT.V` is "Atten'd input signal" from FP
   (output-board net list, PDF 124). The panel GAIN pot is "a real analog
   control ... not read by the Scanner CPU" (PDF 8) and is the sample
   input amplifier gain trim (PDF 17). The jack/pot sheet itself was not
   traced; it is outside the 7-39..7-53 output set (open question Q3).
2. **Programmable gain + anti-alias filter: IC86 S3528.** The S3528's
   internal input amplifier is the gain stage. SIG (pin 12) is the summing
   node, SIGFB (pin 10) the amplifier output.
   - Input: C95 1uF tant -> R84 100k -> SIG.
   - Feedback: R49 1M || C76 10pF, SIGFB -> SIG. C174 100pF SIGFB to ground.
   - IC76 DG202 section pins 3->2, CTL pin 1 = `-PGAIN1.D` (C185 0.1uF to
     ground on the control line): adds R50 110k || C85 100pF in parallel
     with the feedback (Rf 1M -> 99.1k).
   - IC76 DG202 section pins 14->15, CTL pin 16 = `+PGAIN2.D` (C184 0.1uF):
     adds a second input branch from +INPUT.V via C86 4.7uF tant + R51 11k
     to SIG (Rin 100k -> 9.91k).
   - Nominal resistive gains are -10 (neither), about -1 (Rf switched), about -101
     (Rin switched) and -10 (both). So there are three distinct levels (0/20/40 dB)
     plus a duplicate. Which control level closes a DG202 switch has to be
     taken from the DG202 datasheet (Q5).
   - IC86 filter code: FD5..FD0 -> pins 16,17,18,1,2,3 (MSB..LSB), OSC1
     pin 13 = `+C5MHZ.C`, CE pin 4 = `-CSWF0.D`. **This is the same FD bus
     and the same -CSWF0 strobe that loads the channel-0 reconstruction
     S3528 IC85 on 7-44.** The input anti-alias cutoff therefore always equals the
     channel-0 filter code that the firmware writes to main OUT 40h. The OS
     writes `OUT (40h),3Bh` right after entering sampling mode (see 1.6).
   - BUFIN/BUFOUT output buffer with R102/R93 10k, C104 470pF.
3. **Input amplifier IC96 MC34004P.** BUFOUT -> C67 4.7uF (nonpolar) ->
   R48 10k -> pin 6 (-), feedback R47 33k || C49 33pF (gain -3.3).
   `+IINT.V` -> C66 4.7uF (nonpolar) -> R46 33k -> pin 5 (+), R45 10k to
   ground: the non-inverting gain for IINT is 4.3 x 10/43 = **1.0**. So
   IC96 out = IINT_ac - 3.3 * BUFOUT_ac (nominal).
4. **Sample-and-hold.** Two IC76 DG202 sections (6->7, 11->10) are in
   parallel. Their CTL pins 8 and 9 are tied to `+ISMPL.D`. They charge
   C40 4700pF polystyrene.
5. **Comparator IC62 LF311.** C40 drives pin 3 (inverting) and `+IDAC.V`
   (the channel-0 LF356 I/V output) drives pin 2 (non-inverting). The
   output at pin 7 has R1 4.7k to +5V and produces `+IDATA.D`. Balance pins
   5/6 go to RT1 5k, the "sample offset trim" (PDF 43/44, measured at IC75
   pin 1). C165 10uF tant decouples.
6. **Envelope (VU) detector.** The C66 node (that is, **IINT**, not the
   raw input) -> R40 100k -> IC44 TL084 precision rectifier (R10 100k,
   C28 10pF, D2/D3/D6/D7/D1 1N914, R19 100k, D4, C29 10pF) -> C27 1uF tant
   -> IC44 buffer -> `+IENV.V`. IENV goes to digital sheets 3 and 15 (net
   list PDF 119/124). That is the scanner ADC0809 IN1, "input envelope",
   already in SCANNER_CONTROLS.md.

### 1.3 SAR and logic (7-43, PDF 105)

- **IC11 MC14559B** 8-bit successive-approximation register. CLK pin 7 =
  `+C333K.D` (333 kHz by net name; the source sheet was not traced). FF
  pin 10 = GND. DATA pin 6 = IC9 XOR(`+IDATA.D`, S), where S = IC8
  AND(NOT Q7, NOT N) and NOT Q7 comes from IC9 XOR(+5V, Q7). In plain
  terms, once the MSB/sign trial is over and Q7 = 0, the comparator sense
  is inverted for the magnitude bits (sign-magnitude conversion).
- **Start.** `+ISMPL.D` -> IC7 inverter -> IC10a 74HCT74 /PRE (pin 10).
  D = GND, CLK pin 11 = SAR Q6, /CLR = +5V. N = IC8 AND(IC10a.Q,
  `+IMODE.D`) drives SC (pin 9). So each ISMPL pulse in sampling mode
  raises SC, and SC stays high until the SAR sets Q6 (the second bit trial).
- **Am6072 encode mode.** `+ENCODE.D` = IC8 AND(/EOC, /N), where /EOC =
  IC9 XOR(+5V, EOC). This goes to 7-44 IC61 pin 1 (ENCODE). The net list
  describes it as "Input DAC Encode Mode".
- **DAC bus.** IC12 74HCT244 (both halves) drives SAR Q7..Q0 onto
  `+IDAC7..0.D`. /OE (pins 1, 19) = /IMODE (IC7 1->2). So the SAR drives
  the DAC only when IMODE = 1.
- **CPU readback.** IC13 74HCT244 drives SAR Q7..Q0 onto `+PD7..0.D`.
  /OE (pins 1, 19) = `-PCSRIN.D`.
- **Ready flag.** IC10b 74HCT74: D = +5V, CLK pin 3 = EOC (via IC9 XOR
  with GND), /CLR pin 1 = IC8 AND(+IMODE, -PCSRIN). /Q pin 6 =
  `-INPRDY.D`. EOC rising sets ready. A read cycle or IMODE = 0 clears it.
- **Wait.** `-IWAIT.D` = IC5 OR(OR(EOC, /IMODE), -PCSRIN). A port read in
  sampling mode before EOC is held with WAIT until the conversion ends.

### 1.4 Channel 0 (7-44, PDF 106)

- IC43 74HCT374: D = CHDB7..0, CLK pin 11 = `-CHAK0.D`, /OE pin 1 =
  `+IMODE.D`. It drives `+IDAC7..0.D` only when IMODE = 0, so the stored
  playback code and the SAR share one bus. This was already noted in
  SAMPLE_TRANSPORT.md and machine.h.
- IC61 Am6072: SIGN pin 2 = IDAC7, D6..LSB pins 3..9 = IDAC6..0, ENCODE
  pin 1 = `+ENCODE.D`, VLC pin 10 = GND, +VREF via R173/R39 4.7k + C176
  from `+C0ACV.V`. IOD/IOE outputs feed IC53 LF356 (R18 2.49k, R9 2.49k,
  C186 220pF). Its output is `+IDAC.D/.V`, which goes to the comparator.
  Note that channel 0's DAC reference is the ACV control voltage, so the
  firmware's ACV0 value scales the SAR decision levels (Q8).
- Deglitch: R27 200 / R35 10k -> IC74B 13202 (two sections, C75 470pF,
  C84 470pF mica) -> IC75 TL084 -> `+IINT.V` ("Channel 0 Integrator").
  This is the same topology as channels 1..7. IC25 74HCT11 decodes phases
  from IC42 HC393. The third gate is `+ISMPL.D` = Q0 & !Q1 & Q2 (already
  in channel_requests.h). The IC25 phase gates have no IMODE input.
- IINT also continues into IC95 SSM2045 -> IC85 S3528 -> `+C0OUT.V`, the
  normal output path. During sampling the converted input is therefore
  audible as channel-0 output (monitor). PDF 43/44 compares IC75 pin 1
  in Sample mode against normal mode.
- IC23 74HCT74 CHRQ0 latch: unchanged from playback.

### 1.5 Main-board interface (PDF 72, 74, 76, 119, 120)

| Signal | Hardware | Emulator port/bit |
| --- | --- | --- |
| +IMODE.D | main PIO IC24 port B7 (digital sheet 9 = 7-14; net list PDF 119) | `OUT (32h)` bit 7; 1 = sampling |
| -PCSRIN.D | IC80 74LS138 Q1 (PA7..3 = 00001) OR -PIORD (IC109 74LS32) | **IN 08h..0Fh** (PA2..0 not decoded; the 8254 IC1 at 08h..0Bh has RD tied high) |
| -INPRDY.D | IC22 7407 open collector -> IC82 Z80-DMA RDY pin 25, wired-OR with -MMRDY.D (R15 4.7k pull-up) | DMA ready |
| -IWAIT.D | to main CPU sheet 5 (7-10), CPU WAIT | WAIT |
| -PGAIN1.D | scanner PIO IC46 B7 via CN8-1 | scanner PIO B bit 7 |
| +PGAIN2.D | scanner PIO IC46 B6 via CN8-3 | scanner PIO B bit 6 |
| +IENV.V | scanner ADC0809 IN1 | scanner ADC channel 1 |
| filter code | main `OUT (40h)` (-CSWF0) | shared by IC85 and IC86 |
| sample clock | 8254 channel 0 -> -C0TIM -> IC42 HC393 -> ISMPL | existing timers |

The DMA RDY wiring shows the intended data path: Z80-DMA, I/O source
port 08h (fixed) -> DRAM, paced by INPRDY, which is set once per completed
conversion. The CPU can also poll with IN 08h, because WAIT stretches the
read until EOC.

### 1.6 Firmware evidence (OS3.1 EMUIIOS31.E2O, outside git)

Static disassembly of EMUIIOS31.E2O (SHA-256 0cd1304c...1275b9), file
offsets:

- 466Dh: `IN A,(32h); OR 80h; OUT (32h),A` sets IMODE = 1. Shortly after
  that, 46A6h: `LD A,3Bh; OUT (40h),A` loads the channel-0/input filter.
- 46ECh: `IN A,(32h); AND 7Fh; OUT (32h),A` sets IMODE = 0.
- 9A9Bh..9AA9h: the code toggles main PIO B bit 6 according to (24FBh) =
  1 or 3. That bit's function is not traced (Q7).
- No direct `IN A,(08h)` was found. The service manual (PDF 43, step 6)
  says that pressing Sample "loads the Sample software from disk". So the
  arm/threshold/force/stop and DMA programming code is a disk overlay, and
  the ports must be confirmed by a runtime trace (plan step 0). Legends
  1-VU MODE ... 0-STOP SAMPLING (vector_panel_layout.h:66) map onto that
  overlay. The threshold is evaluated in software on SAR data; there is no
  hardware threshold comparator on 7-42/7-43.

## 2. What the emulator has today

| Part | State | Where |
| --- | --- | --- |
| IMODE (PIO B7) decode | modeled as a playback gate only | src/machine.h:96-101 (`channel0PlaybackEnabled`) |
| Channel-0 divider, ISMPL decode, 128-clock stop | modeled | src/channel_requests.h:18,98-113,151-188 |
| IC43 stored code | modeled; /OE (IMODE) and the shared bus are not | src/sample_dac.h:8-40 |
| Am6072 decode current | modeled; **ENCODE pin missing** | src/am6072.h:21-45 |
| Channel-0 deglitch -> IINT | exists as the generic Deglitch3202/BasicVoice path; IINT is not exported as a node | src/deglitch.h:10, src/runtime_audio.h:160-178, src/playback_analog.h |
| Channel-0 output muted unless IMODE = 0 | modeled; in sampling mode channel 0 is silent (on hardware it should monitor) | src/runtime_audio.h:165 |
| S3528 control latch, channel 0 | modeled (OUT 40h); IC86 is not instantiated | src/machine.cpp:205, src/s3528.h:8, src/s3528_runtime.h:9 |
| IN 08h..0Fh (SAR readback) | **missing**: returns 255 and counts `unmappedIo` | src/machine.cpp:151-188 |
| -IWAIT | **missing** (only the DRAM WAIT exists) | src/machine.cpp:229-243 |
| DMA RDY | only `diskSio.readyN()`; **INPRDY wired-OR missing** | src/machine.cpp:369, src/disk_dma.h:59-60 |
| DMA I/O->memory byte mode | supported subset (I/O source, byte mode) | src/disk_dma.h:50-56,68 |
| DMA/CPU DRAM writes | real controller path | src/machine.cpp:410-428 |
| MC14559B SAR, LF311, S/H C40, IC96, IC86 gain/filter | **missing** | - |
| PGAIN (scanner PB6/PB7) | ignored | src/scanner_controls.h:139, docs/SCANNER_CONTROLS.md:89 |
| IENV -> scanner ADC IN1 | ADC exists; input is a parked 0 V external pin | docs/SCANNER_CONTROLS.md:65-71, src/front_panel_session.h:415 |
| Panel GAIN knob | drawn inactive ("recording pending") | src/windows_vector_panel.h:139-141 |
| Diagnostics text | "channel0 MODE/SAR and analog conversion absent" | src/machine.cpp:574,592 |

## 3. Host side

- WASAPI output: src/windows_audio_output.h (render client, event mode,
  shared), src/windows_audio_endpoints.h:54 (enumerates only `eRender`).
- Pacing/scheduling: src/host_audio_schedule.h, src/host_pacing.h,
  src/native_audio.h.
- Worker/command queue: src/front_panel_session.h (Kind::Voltage
  coalescing at :220; that is the pattern for a timestamped input stream).
- MIDI in: src/windows_midi_input.h. GUI/settings: src/windows.cpp,
  src/windows_vector_panel.h, src/vector_panel_layout.h.
- Missing: an `eCapture` endpoint list, an IAudioCaptureClient, a
  host-clock -> emulated-time resampling FIFO, and a calibrated volts-per-
  full-scale mapping to `+INPUT.V` (before the GAIN pot / at the jack, Q3).

## 4. Implementation plan

Rules for every step: the real firmware drives everything. No injected
bytes, no direct DRAM/sample writes and no shortcut sampler. The edge/
reference engine is the oracle. Production/event routes must match it
bit-exactly on the digital side. **With no input connected (default),
existing hashes must stay unchanged**: the 200M Grand Piano memory/known/
scanner FNV values, the 24M benchmark audio hashes and the
tools/build-native.sh suite. Each step lists its check.

0. **Firmware trace, no model change.** Add counters/trace for main IN
   08h..0Fh, PIO B writes and DMA WR bytes to the probe only. Boot the
   bank, press SAMPLE (load overlay), then each Sample keypad function.
   Record the actual DMA program (port 08h? byte mode? ready-active
   level), PGAIN writes, OUT 40h codes and 8254 channel 0 periods per
   SAMPLE LENGTH/rate. Check: probes only, all hashes identical.
1. **Datasheet verification (rendered pages).** MC14559B (SC/EOC timing
   and whether SC held high restarts), Am6072 ENCODE (decision-level
   offset, sign handling), DG202 switch polarity, LF311. Record them in
   this doc before coding.
2. **Digital SAR block** `src/sample_input.h`: IC11 SAR clocked by
   C333K edges in the half-edge scheduler, IC10a/IC10b, IC8/IC9/IC5 gates,
   IC12/IC13 bus drivers, ENCODE/INPRDY/IWAIT outputs. All power-up states
   are partial-known, using the existing KnownByte/LogicLevel conventions.
   The comparator is an explicit input pin (known/unknown). Check:
   standalone exhaustive tests against an independent gate-level
   enumeration. Machine is not connected yet, so hashes are trivially
   unchanged.
3. **Machine wiring, digital only.** IN 08h..0Fh through IC13, -IWAIT ->
   main WAIT (ORed with the DRAM WAIT), -INPRDY wired-OR into the DMA
   ready, IMODE -> IC12/IC43 bus ownership, ENCODE -> channel-0 DAC state.
   Comparator unconnected = unknown. Check: when IMODE stays 0 (every
   existing run), the outputs are inert, and the 200M/24M hashes and
   reference-vs-production equivalence are unchanged. New synthetic real-Z80
   test: IN 08h with WAIT, DMA I/O->DRAM transfer paced by INPRDY.
4. **Am6072 encode + DAC bus mux.** Extend am6072.h with ENCODE per step 1.
   The channel-0 DAC input becomes the IC43 or IC12 output depending on
   IMODE. Check: decode-mode results are bit-identical (existing Am6072
   tests and hashes). New tests cover the encode decision levels against
   the datasheet table.
5. **Analog front end** (`src/sample_input_analog.h`): the IC86 gain stage
   with PGAIN switch states and the S3528 anti-alias filter (reuse
   s3528/S3528Runtime with the channel-0 code), IC96 summing with IINT
   from the channel-0 deglitch node, the DG202 S/H onto C40 at ISMPL, and
   the LF311 decision vs `+IDAC.V` -> IDATA. Envelope detector -> IENV ->
   scanner ADC IN1. Check: closed-form tests (gains 0/20/40 dB, S/H hold,
   comparator threshold). A DC sweep with the full SAR gives a monotonic
   code transfer that matches the Am6072 encode table. No-input default
   (INPUT.V = 0 V known, or disconnected = explicit option) keeps hashes
   unchanged as long as IMODE stays 0.
6. **Channel-0 monitor.** Unmute channel 0 in sampling mode through the
   real path (IC43 disabled, the DAC follows the SAR code). Check: playback-
   only runs are unchanged; in a sampling-mode run the IINT idle offset
   stays < 10 mV against playback (the PDF 43/44 criterion, with ideal RT1).
7. **End-to-end with real firmware, offline.** A headless probe feeds a
   WAV or synthetic sine into `+INPUT.V` on emulated time. The firmware
   arms, triggers on threshold and records via DMA. The probe then plays
   the new sample from the keyboard. Check: the reference engine and the
   production route give identical DRAM/known hashes for the same input
   file. The recorded tone frequency/level matches the input within the
   documented analog tolerance (tools/measure_tone.py). No-input runs keep
   all hashes.
8. **Host WASAPI capture.** Endpoint enumeration for `eCapture`, a capture
   thread -> lock-free FIFO -> resampler keyed to emulated time ->
   `+INPUT.V`. GAIN knob wired as an attenuator. Settings: device, input
   level (V full-scale). Check: with capture off, the binary behaves and
   hashes as before. A loopback/WAV-injection test of the capture FIFO
   (device-free). Windows runtime validation is done by the owner only
   (not claimed from a cross-build).
9. **GUI/VU.** Enable the GAIN knob. VU comes from the real firmware via
   IENV -> ADC -> LCD, with no host-drawn meter. Check: owner acceptance.

## 5. Open questions (owner)

- Q1. IC96 adds IINT (channel-0 DAC playback) at unity to -3.3 x input
  before the S/H. Is this intended (monitor/prediction/offset)? Do you
  have scope captures or a working unit to confirm IC75 pin 1 and the
  C40 voltage while sampling? Until then, the plan models it literally.
- Q2. The deglitch DC gain of -50 (R27 200 / R35 10k) is the same
  unresolved scale question as in PLAYBACK_ANALOG/NATIVE_CALIBRATION.
  IINT amplitude directly affects conversion (Q1). Is there any measured
  IINT/C0OUT level?
- Q3. Where are the sample input jack and the GAIN pot (sheet 7-2 /
  right panel or rear panel), and what are the pot value and taper? What
  input level (V) should host full-scale map to?
- Q4. Which disk carries the Sample overlay software for the trace in
  step 0? Is OS3.1 on `18 Grand Piano #2.hfe` enough?
- Q5. Can you supply rendered datasheets (or permission to fetch them) for
  MC14559B, Am6072 (ENCODE), DG202 and LF311?
- Q6. Do you want the channel-0 monitor (step 6) audible by default in
  Sample mode, as on hardware?
- Q7. Main PIO B6 (OS 9A9Bh) toggles with a sample-related state. Do you
  know its function? It is not yet traced on 7-14.
- Q8. Channel-0 DAC reference is +C0ACV.V. Does the overlay set ACV0 to
  a fixed value during sampling? The trace in step 0 will show it, but if
  you know the expected value, it gives an early check.
- Q9. Priority: start with offline WAV injection (steps 2-7) before
  WASAPI capture (step 8)?

## 6. Implementation log

### Step 0: firmware trace (probe only)

`tools/sample_trace_probe.cpp` boots caller-supplied ROMs/disk and presses
panel keys through `ScannerControls::setButton`; it only observes I/O via
`Machine::trace`. Disk: owner's OS disk `emuiios.v31.emuiifd` (SHA-256
recorded in validation, outside git). The bank disk `18 Grand Piano #2.hfe`
lacks the overlay: pressing SAMPLE makes the strict disk path throw
"requires one complete CRC-valid EII sector at cylinder5 side1".

`sample_trace_probe ROMS emuiios.v31.emuiifd 200000000 S 30000000 7 8000000 9 8000000 ...`
(main 2.1, scanner 3.1, both PROMs; production controller, seed 0xe1122026):

| Tick | Event |
| --- | --- |
| 200.0M | boot `P01 NULL PRESET` |
| 205.71M | `OUT 32h,BDh` pc 6B73: PIO B7 = 1, **IMODE = 1** |
| 205.75M | `OUT 40h,3Bh` pc 6BAA: channel-0 / input S3528 code 3Bh |
| 232.8M | LCD `VOICE V01  +0???` (Sample module) |
| key 7 | LCD `Sample Armed`; pre-trigger loop pc 6E1F/6E36 |
| key 9 | record loop pc 6E68 |

Firmware interface found (main-RAM disassembly of the loaded overlay):

- 8254 IC3 counter 0 (`OUT 18h`) = 0168h = 360 -> 10 MHz/360 = 27.78 kHz.
- Before arming: `OUT 70h,F8h; OUT 78h,FFh; OUT 70h,F0h` sets channel-0
  ACV to code FFh (about 4.98 V nominal, Iref about 530 uA).
- **No DMA.** The CPU reads the SAR with `INI` from port 08h (C = 08h).
  Pre-trigger: `LD HL,8800h; LD B,FFh; INI ...` into a 256-byte ring.
  Record: `INI` loop with bank switching through `OUT 6Bh` (page register).
  Pacing is entirely -IWAIT: each `INI` waits while the SAR converts.
- Trigger/abort come from the scanner over PIO A: the main loop polls
  `IN A,(30h)` for FCh (trigger) and FEh (stop). The threshold is
  therefore judged by the scanner on IENV (ADC0809 IN1). FORCE (key 9)
  goes straight to the record loop.
- Without a SAR model the reads return FFh (unmapped) and are never
  stretched (loop period 76-87 CPU clocks instead of one sample period).

Regression: the probe is not linked into the emulator; hashes unchanged by
construction.

### Step 1: datasheet verification (rendered pages)

Datasheets are kept outside git in `F:\Claude\_assets\datasheets\`.

- **MC14559B** (onsemi MC14549B/D, pages 4-5): all inputs act on the
  positive clock edge. A conversion is initiated by a positive SC
  transition seen at a clock edge; it also recirculates after EOC = 1
  while SC = 1. The start clock sets Q7 and clears Q6..Q0. Each
  following clock conditionally resets the bit under trial (D = 0 resets,
  D = 1 keeps) and sets the next lower bit. EOC goes high on the
  **negative** clock edge after Q0's conditional reset (FF = VSS: 8 bits).
  The text says "With EOC = 0 ... no stable state exists under continual
  clocked operation". So an unknown power-up register reaches EOC = 1
  within 9 clocks while SC = 0.
- **C333K** (7-30, PDF 93): IC91 74LS92 CLK0 = 4 MHz, Q0 = +C2MHZ.D fed
  to CLK1, Q3 = +C333K.D. That is CPU clock / 12. The LS92 reset pins are
  grounded, so the power-up phase is unknown (the model chooses rising
  edges at `ticks % 12 == 0`, falling at 6).
- **-PWAIT** (7-10, PDF 73): IC84 AND(IC61 /Q, -IWAIT.D) drives Z80 WAIT.
- **Am6072** (AMD 1979 Linear and Interface Data Book pp. 3-55..3-56,
  PDF pp. 154-155): E/D = 1 selects encode. Current flows into IOE(+) or
  IOE(-) per SB (SB = 1 positive). An internal half step of the current
  chord is added in encode (full scale +32 uA at Iref 528 uA; chord 0
  step 0 = +/-0.25 uA). On 7-44 IOE+/IOE- are wired to the same LF356
  nodes as IOD+/IOD-, which is the data-book "ninth bit" decoder hookup.
  So encode magnitude = decode magnitude + (1 << chord) units of Iref/2112.
- **DG202** (Temic DG201A/202 p. 1): logic 1 = switch ON. Pins
  IN1/D1/S1 = 1/2/3, IN2/D2/S2 = 16/15/14, IN3 = 9/10/11, IN4 = 8/7/6, matching
  7-42. Hence `-PGAIN1.D` high closes the R50 110k feedback (low gain);
  `+PGAIN2.D` high closes the R51 11k input branch (+20 dB).
- **LM311** (TI SLCS007K p. 3): pin 2 IN+, pin 3 IN-, pin 7 collector.
  IDATA = 1 when V(+IDAC.V) > V(C40).

Consequence (worked through the gates): during the start clock and the Q7
trial, N = 1, ENCODE = 0, code 80h decodes to zero current. So Q7 = 1 is
kept exactly when V(C40) < 0. After Q6 rises, N = 0, ENCODE = 1, and the
IC9 XOR inverts IDATA whenever Q7 = 0. The SAR therefore produces the
sign-magnitude mu-255 code of V(C40) in channel-0 DAC volts
(`Am6072::eiiSteadyVolts` convention), using encode decision levels.

### Step 2: digital SAR block (standalone)

`src/sample_input.h` `SampleSar`: IC11 MC14559B, IC10a/IC10b, IC8/IC9/IC5
gates, ENCODE, -INPRDY, -IWAIT, readback register. Partial-known levels
(`LogicLevel`/`KnownByte`); no power-on random bits are drawn (that would
shift every other startup state). Clocked at C333K = CPU/12 with the chosen
phase. `tests/sample_input_tests.cpp` (no ROMs): power-up convergence to
idle EOC = 1 in nine clocks, ISMPL with IMODE = 0 only presets IC10a,
1044-target grid against an independent enumeration of the sign-magnitude
decision rule (synthetic linear DAC with half-step encode), exact EOC tick
(start + 8 x 12 + 6), single conversion per ISMPL (no recirculation),
-IWAIT/-INPRDY/read-clear handshake, unknown comparator -> 8 unknown bits,
unknown IMODE -> unknown state recovering after IMODE = 0.
Limitation: an unknown IC10a while IMODE = 1 keeps the register unknown
until IMODE returns to 0; Machine keeps IC10a known (step 3).
Not linked into Machine yet: hashes unchanged by construction.

### Steps 3-4: Machine wiring, Am6072 encode, DAC bus ownership

Committed together because the bus accessor needs the encode decoder.

- `Machine::channel0Mode()`: PIO B7 pin level (input/masked bit = unknown).
- `IN 08h..0Fh` (7-11 IC80 Q1 OR -PIORD) returns IC13: the SAR register,
  unknown bits exposed as 1 at the Z80 boundary and counted
  (`sampleUnknownReads`); live while a held read continues.
- -IWAIT -> -PWAIT (7-10 IC84): a main-CPU read of 08h..0Fh with IMODE = 1
  before EOC holds the CPU like the existing DRAM WAIT adapter
  (`sampleWaitClocks`). Unknown -IWAIT is not held but counted.
- -INPRDY is wired-OR into the Z80-DMA ready input (7-13 IC22 7407).
  Unknown -INPRDY while the DMA is enabled is counted (`sampleReadyUnknown`);
  OS3.1 never uses it.
- `SampleSar` runs per CPU tick only while not quiescent. With IMODE known
  low and the register idle it is exactly static and skipped. IC10a's ISMPL
  presets during skipped time are replayed from a new observation-only
  counter on channel 0 (`ChannelRequestTiming::samplingPulses`).
- `Am6072::encodeMagnitudeUnits/eiiSteadyVoltsMode`: decode + 2^chord units
  in encode (data-book table values checked for all 256 codes); decode path
  unchanged.
- `Machine::channel0DacBus()` (IC43 when IMODE = 0, IC12/SAR when IMODE = 1,
  merged if unknown) and `channel0IdacVolts()`.
- The comparator is still an explicit external level (`sampleComparator`,
  used only by tests with `sampleComparatorExternal`).
- Datasheet-bounded power-up resolution: an unknown MC14559B with SC known
  restarts or idles after nine clocks (chosen schedule within the bound).

Tests: `tests/sample_machine_tests.cpp` (real Z80: PIO B mode 3, channel-0
8254 = 360, IMODE = 1, `INI` loop of 16 reads with the OS3.1 66-T loop
shape). Bytes FFh/7Fh for constant comparator high/low (sign then XOR
magnitude). 16 known reads, -IWAIT holds (1196 clocks), one conversion
per ISMPL, reads paced at about one sample period (2108 ticks for 15
intervals), IMODE = 0 read not held and unknown -> FFh.
`tests/sample_input_tests.cpp` adds Am6072 encode checks.
Gate: Windows clang bench, parent a78773a vs this tree, identical
mem/known/scan/Legacy audio (mode 1) and native frames (mode 2).
Suite: `tools/build-native.sh` under WSL Ubuntu g++ 15, parent and this tree
run without stopping: the only failing command in both is the pre-existing
`performance_trace_session_tests` ("Legacy remains reference path");
every parent PASS line is unchanged and the new tests pass.

### Step 5: analog input path, comparator and IINT loop

`src/sample_input_analog.h` `SampleInputAnalog` (nominal 7-42/7-44 values,
ideal op-amps), advanced per CPU tick only while IMODE = 1:

- +INPUT.V from `SampleInputSignal` (host samples, +/-1.0 -> +/-1.0 V
  nominal line level after the GAIN pot; pot factor 0..1; 0 V = jack open).
- IC86 input amplifier: C95/R84 and (+PGAIN2) C86/R51 input branches, R49
  1M || C76 10pF feedback plus (-PGAIN1 high) R50 110k || C85 100pF.
  PGAIN pins = scanner PIO B7/B6 (7-9). Measured: 0.990 / 9.99 / 9.99 /
  100.8 at 1 kHz for the four switch states (nominal 0/20/20/40 dB).
- IC86 S3528 low-pass = existing `S3528Runtime` (AMI1993 fit, 192 kHz) with
  the shared -CSWF0 code (OS3.1: 3Bh, divisor 10, fc 12.5 kHz).
  BUFIN/BUFOUT pole 33.9 kHz, C67/R48 coupling.
- IC96: -3.3 x BUF + 1.0 x IINT (C66 coupling 0.202 s), rails +/-13.5 V.
- DG202 S/H onto C40 while +ISMPL is high; LM311 IDATA = +IDAC.V > V(C40).
- +IDAC.V = `Am6072::eiiSteadyVoltsMode(bus, ACV0 RC 2.35 ms, ENCODE)`.
- IINT = existing `Deglitch3202` on +IDAC.V with channel-0 phi1/phi2.
- Quiet start (documented assumption): the analog model starts with all
  capacitors discharged at the first tick after IMODE = 1 with a fully
  known DAC bus. Before that, comparisons are unknown.
- Not modeled: IENV/VU detector (IC44 diode orientation not resolvable at
  scan resolution, so ARM/threshold cannot trigger; FORCE works), RT1
  offset, S3528 clock images, DG202 injection, op-amp slew.

Result (worked out and verified): the loop is a **DPCM** coder. C40 =
IINT - 3.3 Vin, the SAR codes that difference, and the deglitch charge
transfer integrates it back, so IINT -> (10000/10200) x 3.3 x Vin. The
stored bytes are mu-255 companded differences; playback through the same
integrator reconstructs the waveform. This also explains the long-standing
"DC gain -50" of the 7-44/7-45 deglitch (a leaky integrator, Q2).

Test `tests/sample_analog_tests.cpp`: gain states, S/H hold, comparator.
Closed loop with a real Z80 `INI` loop (OS3.1-like setup: timer 360,
ACV0 FFh, S3528 3Bh) and 1 kHz, 0.5 V input: 512 known bytes. Offline
reconstruction through the settled deglitch recursion gives 3.19 V p-p
(theory 3.20) and 18 zero crossings per 256 samples (1 kHz).
Gate: bench identical to a78773a (modes 1 and 2); WSL suite: parent PASS
set unchanged plus the new tests; only the pre-existing perf-trace failure.

### Step 6: channel-0 monitor as wired

7-44 has no IMODE gate on the channel-0 output path (DAC -> deglitch IINT ->
SSM2045 -> S3528 -> C0OUT); PDF43/44 measures IC75 pin 1 in Sample mode.
`Machine::channel0AudioEnabled()` = IMODE known (0 or 1) replaces the old
"IMODE = 0 only" mute for the legacy/chip runtime engines; with IMODE = 1
`playbackAnalogInputs()` gives channel 0 the bus code latched at channel-0
phi1 (what the 3202 deglitch samples), not SAR trial codes. No host-side
extra signal. Not done: the native (event) audio path still receives only
IC43 codes, so the monitor is silent there (GUI default route); the native
Mode event is unchanged.
Check: bench identical (modes 1 and 2, IMODE never 1). With the real
firmware (step 7 probe) the monitor plays the 440 Hz input at 440.0 Hz
(peak 0.0998) during recording, identical in both engines.

### Step 7: offline WAV -> real firmware -> SAR -> DRAM -> playback

`tools/sample_wav_probe.cpp` (caller-supplied ROMs, OS disk, WAV): boots
OS3.1 from `emuiios.v31.emuiifd`, presses SAMPLE, attaches the WAV to
+INPUT.V, presses 9 (FORCE SAMPLING), records for 2 s, presses 0 (STOP),
leaves Sample mode, then plays MIDI note 48. Panel keys and the input pin
voltage are the only stimuli; no memory/CPU writes. It runs the reference
and production controller engines separately and compares DRAM, DRAM-known
and audio hashes.

Command (Windows llvm-mingw build of this tree, input 440 Hz sine,
amplitude 0.5, 48 kHz, 1.0 V full scale = 0.5 V peak at +INPUT.V):
`sample_wav_probe ROMS emuiios.v31.emuiifd sine440.wav run7 both 1.0 2 48`

| Stage | Both engines |
| --- | --- |
| SAMPLE | LCD `VOICE V01  +0???`, SAR converting continuously (174228 conversions by 230.8M) |
| FORCE + 2 s | 60621 `INI` reads, 1282529 -IWAIT clocks, 0 unknown reads |
| STOP | LCD **`Sample is good`** |
| DRAM | 61460 bytes changed in 02020h..18942h (CPU DRAM path) |
| analysis | DPCM-decoded recorded tone 441.2 Hz (27.78 kHz rate) |
| monitor (step 6) | 440.0 Hz, peak 0.0998 |
| playback note 48 | 440.0 Hz, peak 0.0964 (note 60 plays 883 Hz: root is 48) |
| hashes | reference == production: mem 13224198788934156055, known 11198069657462029093, audio 3252512138953584631 |

Full log: `validation/sample-input-wav-probe.txt`.
Limits: FORCE only (ARM needs IENV, not modeled); GAIN SET not exercised
(OS default PGAIN state used); nominal +INPUT.V level; the analog values
are nominal, not calibrated against a unit; Windows runtime GUI not
exercised (steps 8-9).

## 7. Open questions after implementation

- Q1/Q2 are now understood as the DPCM loop: IINT is the integrated
  playback of the stored differences. The open part is calibration:
  the actual IINT/C0OUT level on a unit (scope), and RT1.
- Q3 (jack/pot sheet, pot value/taper, real line level) still open. The
  model uses 1.0 V peak at +INPUT.V for host full scale.
- IENV/VU detector topology (7-42 IC44, D1/D4/D7 orientation) needs a
  higher-resolution scan or a measurement before ARM/threshold and VU
  mode can work.
- Q7 (main PIO B6) and Q8 (ACV0 is set to FFh by the overlay; answered by
  the trace) remain as recorded.

### Step 8: live sample input in the Windows GUI

- `src/windows_audio_capture.h`: WASAPI shared-mode capture on its own
  named thread ("EII WASAPI capture", `AudioThreadSchedule` multimedia
  class). It is event driven. Device mix format (float32 or PCM16/24/32,
  any channel count) is converted to mono (channel mean) and linearly
  resampled to 48 kHz.
- `src/sample_input_live.h` `LiveSampleInput`: lock-free SPSC ring
  (32768 samples, 0.68 s).
  - The machine thread consumes it in emulated time (4 MHz ticks ->
    48 kHz) through the `SampleInputSource` interface that the offline WAV
    signal also implements.
  - Underrun: 0 V for that sample, counted, no debt.
  - Overrun: above 100 ms buffered, the oldest samples are dropped down to
    40 ms, counted. A full ring drops the new block on the producer side,
    also counted.
  - While IMODE != 1 the machine drains the ring about every 1 ms
    (`idle`), so a recording starts at "now". No blocking on either side.
  - Host +/-1.0 = 1.0 V peak at +INPUT.V (nominal assumption) x GAIN pot.
- Settings window: "SAMPLE IN:" list (None = default, system default
  input, devices), "Refresh inputs", and live status: rate, buffered,
  underruns, overruns, GAIN.
  - Persisted in the registry as `SampleInputEnabled`,
    `SampleInputEndpoint` and `SampleInputGain`, next to the output device
    and volume.
  - The performance trace gets a `sample_input` event (capture
    diagnostics plus ring counters) once a second.
  - The Filter Service status shows the same line.
- Vector panel GAIN knob is live: drag vertically, 0..1 linear nominal
  taper of the 7-42 attenuation before +INPUT.V. It is persisted like the
  volume. "(recording pending)" is removed.
- Native (event) audio monitor:
  - With IMODE = 1, channel-0 `Pins` events carry the SAR bus (IC12)
    instead of IC43.
  - The `Mode` event uses `channel0AudioEnabled()` (IMODE known). This is
    identical to the old value whenever IMODE = 0.
- CPU cost with no capture device: one null-pointer test per tick in
  `sampleInputTick` (the capture thread is not started).

Checks: `tests/sample_analog_tests.cpp` covers the ring: the 48 kHz
mapping, GAIN scaling, underrun, high-water drop, producer drop and idle
drain. The bench is identical to a78773a (modes 1/2, native frames). Native
monitor with the real overlay (probe `native`): 440.0 Hz during recording,
note 48 plays 440.0 Hz, reference == production.

### Step 9: IENV / VU / threshold and GAIN SET

The 7-42 IC44 region was read from the native 318 ppi CCITT image
(`pdfimages`, inverted crop; the 200 dpi renders were too coarse). With
1N914 pin 1 = cathode (bar):

- IC44a: R40 100k in, R10 100k from node A, C28 10pF. D2 anode = output,
  cathode = node A. D3 anode = summing node, cathode = output. D6 anode =
  output, cathode = +5V. This is a precision peak detector of the
  **negative** half of the C66-coupled IINT onto **C27 1uF** (node A).
- C27 discharges through R10 into the virtual ground: tau 0.1 s.
- IC44d: R19 100k from IENV, D4 anode at its summing node and cathode at
  its output, C29. D1 anode = IC44d output, cathode = C27. D7 clamps to
  +5V. This holds IENV >= 0.
- IC44b buffers C27 -> +IENV.V.
- 7-8 (PDF 71, native image): +IENV.V -> R26 1k -> ADC0809 IN1, with a
  1N914 clamp.

Model (`SampleInputAnalog::envelope`): env = max(-IINT_ac clipped at
5 V, env x exp(-dt/0.1 s)), floored at 0. It feeds ADC IN1 every 16 us
while IMODE = 1, and returns to 0 V when sampling mode ends.

Firmware confirmation (`sample_wav_probe`, real OS3.1 overlay):

- ARM (key 7) with `burst440.wav` (1 s silence, then 440 Hz 0.5 FS):
  - The pre-trigger loop is left at **1.005 s after ARM**; the tone starts
    at 1.000 s, and there is no early trigger in silence.
  - LCD `Sample is good`; note 48 plays 440.0 Hz.
  - Reference == production hashes.
- GAIN SET (key 3) shows `INPUT GAIN +00dB / USE SLIDER A`. Slider A at
  5 V gives `INPUT GAIN +40dB` with -PGAIN1 = 0 and +PGAIN2 = 1. That is
  exactly the x101 state derived from DG202 logic 1 = ON, which
  independently confirms the switch polarity. With 0.1 V input at +40 dB
  (about 10 V) the firmware reports `Sample Overload`.

Assumption: the half-wave (negative-half) detection and ideal diode drops
follow from the read topology. The release tau is nominal R10 x C27. The
LCD second line shows '?' for the overlay's custom VU glyphs (LCD
CG-RAM characters are not rendered by `Lcd::line`).

### Hot-path cost (bench regression found and fixed)

The first PGO ABBA (mode 3 native+S3528, 8 held notes, Windows PGO benches
of a78773a and this tree) was 1.247x against 1.276x: about -2.3%.
Bisected causes:

- The -IWAIT block inlined into `Machine::step` enlarged it and changed
  PGO inlining. It is now a cold, non-inlined `sampleWaitStep`, guarded
  by a flag.
- A per-channel ISMPL counter grew `ChannelRequestTiming` (about -0.5%
  non-PGO). It was removed; IC10a history now uses the existing
  `requestClocks`/`ambiguousRequestClocks` of channel 0. A request edge
  (count 4) is always followed 16 source clocks later by ISMPL (count 5)
  when the 8254 period is >= 128 clocks (OS3.1: 360). The documented
  approximation is a preset at most 1.6 us early at an IMODE transition.
- `sampleQuiet_`: while IMODE = 0 and the SAR is idle nothing is
  evaluated per tick. Only a main PIO write clears it. The live-input
  drain runs every 4096 ticks only if a source is attached.

After the fix, ABBA a78773a / this tree (PGO, same machine):
1.2878 / 1.2836, 1.2637 / 1.2626, 1.2766 / 1.2726, 1.2771 / 1.2639.
Means are 1.2763 and 1.2707 (-0.4%, inside the about +/-1% run-to-run
spread). For reference, docs/RUNTIME_072.md gives 1.30 / 1.28 for
a78773a.

### Build, suites and owner GUI procedure (0.73-sample)

- `tools/build-win64.ps1` (LLVM-MinGW 20261006), run on this tree and on
  a78773a with failures logged instead of stopping:
  - Both trees fail the same three pre-existing checks: vector panel
    "physical keypad routing", performance trace session, chip audio.
  - Every PASS line is identical between the two trees.
- WSL g++ 15 `build-native.sh`: the parent PASS set is unchanged, plus the
  new sample tests. The only failure is the pre-existing perf-trace-session
  test.
- `tools/build-win64-pgo.ps1 -RomFolder Z:\EII\roms -Bank "Z:\EII\disk\18 Grand Piano #2.hfe"`:
  the PGO GUI was copied to `Z:\EII\EII-0.73-sample.exe` (new file;
  SHA-256 e0a4b31f...233428).
- Bench speed: see `validation/sample-input-wav-probe.txt` (PGO benches
  of a78773a and this tree, ABBA, mode 3 native+S3528, 8 held notes).

Owner procedure (sound card input):

1. Boot with the OS disk `emuiios.v31.emuiifd` as the disk image. The
   Sample overlay is on the OS disk, not on the Grand Piano bank disk.
2. Settings window: in **SAMPLE IN:** pick the sound card input (or
   "System default input"). The status line shows the capture rate and
   the underrun/overrun counters. "None" (the default) stops capture.
3. Press **SAMPLE** on the panel. The LCD shows `VOICE V01 +0???`, and
   channel 0 now monitors the input through the emulated DAC.
4. Set the level:
   - Coarse: **3** (GAIN SET), then slider **A** for +00/+20/+40 dB.
   - Fine: drag the panel **GAIN** knob (it scales +INPUT.V).
   - Host full scale is assumed to be 1.0 V peak.
5. Press **7** to arm (the threshold triggers on the input envelope) or
   **9** to force; press **0** to stop. The LCD shows `Sample is good` or
   `Sample Overload`.
6. Press **SAMPLE** again to leave the module, then play. The 440 Hz test
   sample plays at pitch on MIDI note 48.

Remaining assumptions (owner-checkable):

- Host full scale = 1.0 V peak at +INPUT.V. The GAIN pot is a linear 0..1
  attenuation (value and taper not on the read sheets).
- Nominal component values and ideal op-amps/diodes. RT1 offset = 0.
- The analog model starts "quiet" when IMODE goes to 1.
- IENV detects the negative IINT half (read topology), with release tau
  R10 x C27.
- IC10a preset history from channel-0 request edges (at most 1.6 us early
  at an IMODE transition).
- Slider A positions: 0 V and 2.5 V gave +00 dB, 5 V gave +40 dB. The +20
  dB threshold was not mapped.
- Windows GUI capture was compiled and the thread/ring logic is
  unit-tested, but no capture was exercised on real hardware here.

## 8. Owner feedback on 0.73 (capture, suite failures, memory)

### A. Capture clock domain: jitter buffer

The 0.73 capture ring reported "buffered 0 | underruns 484 | overruns 1378".
It had no prefill, counted every missing sample as an underrun, and
counted every high-water drop (100 ms) as an overrun. The machine thread
consumes in about 5 ms quanta at 0.95-1.05x, so emulated-time consumption
and the capture clock drift and burst against each other.

`LiveSampleInput` (src/sample_input_live.h) is now a jitter buffer:

- Size: 1.37 s capacity; target fill 60 ms.
- Prefill: the consumer outputs 0 V until 60 ms are buffered, then reads
  60 ms behind the newest sample.
- Underrun = event: re-prefill rather than starving sample by sample.
- Trim: proportional, bounded to +/-0.5 %, on a 1 s smoothed fill error.
  It holds the target fill against device-clock drift.
- Hard limit (target + 300 ms): skip the oldest data (overrun event).
- Read position: advances with emulated time (CPU ticks -> 48 kHz x
  (1 + trim)); value = 4-point cubic (Catmull-Rom) interpolation. The
  modeled 7-42 path sees continuous time, and the real SAR samples it at
  its own clock. The offline WAV source uses the same cubic.
- Not sampling (`idle()`), paused (no consumer heartbeat for 10 capture
  blocks, about 100 ms) or rebooting (ticks restart): the producer keeps
  the fill near target. It drops the oldest quietly (`discarded`, not an
  overrun), and the consumer restarts with a quiet prefill.
- Counters:
  - `underruns` = events where the consumer, while running, needed an
    uncaptured sample;
  - `overruns` = data lost while sampling (ring full, or hard limit);
  - `discarded` = quiet drops while inactive;
  - `prefills` = starts/restarts;
  - `trim` = current rate trim.
  All are shown in the settings status line, the Filter Service and the
  performance trace (`sample_input` event).
- Capture: mono = (L+R)/2 mix, left or right (settings list, persisted as
  `SampleInputChannel`). 4-point cubic resampling to 48 kHz when the
  device rate differs.

`tests/sample_input_live_tests.cpp` (deterministic, no ROMs):

- Setup: 10 ms packets on a +150 ppm device clock with +/-3 ms delivery
  jitter; 5 ms machine quanta at 0.95-1.05x, paced to wall time; 45 ms
  stalls every 6 s.
- Result over 60 s after the prefill: **0 underruns, 0 overruns**, fill
  42.8-108.8 ms.
- The 440 Hz content is read at the SAR grid as 440.019 Hz: the trim
  absorbs the +150 ppm clock.
- Idle/pause/reboot cases discard quietly with no overrun. A starved
  consumer counts an underrun and re-prefills.

Real firmware (probe, FORCE, 440 Hz WAV): stored-data rate **27778
samples/s** (= 10 MHz / 360, purely from the timer and -IWAIT), note 48
plays 440.0 Hz, reference == production.

### B. Suite failures: not DRAM, sample memory or controller

- `performance_trace_session_tests` ("Legacy remains reference path"):
  - passes at 27d81df, fails from 08a9e1f at every later baseline
    (08a9e1f, 2e02112, a78773a, HEAD);
  - 08a9e1f deliberately made the requested S3528 apply in Legacy
    (docs/NATIVE_CALIBRATION_071.md: "now apply to Legacy and nonlinear
    modes"), and the snapshot reports the applied state;
  - the expectation was outdated and is updated.
- `chip_audio_tests` (Windows only):
  - exit code 0xC00000FD STATUS_STACK_OVERFLOW before `main` prints
    anything, at every baseline back to 1568696 (0.70);
  - cause: the test builds a `ChipAudioWorker` (1,796,048 bytes of inline
    rings) on the stack, and the Windows main-thread stack is 1 MB
    (Linux 8 MB, where it passes);
  - production heap-allocates it (`RuntimeAudioEngine::chipWorker_`), so
    the test now does too.
- `performance_trace_tests` itself passes everywhere. The Windows script
  reported the trace loop as failed because of the session test.
- None of the three exercises Machine, DRAM, sample memory or the
  controller. The DRAM/known hashes of the gate and the probe engines are
  unchanged.

### C. "Need More Room": free memory before sampling

Service manual PDF 43 (sample offset trim, step 5) erases all memory with
PRESET DEFINITION, 1, 6, YES, YES. On the keypad 9 = YES and 7 = NO
(sheet 7-4). The firmware prompts, verified with
`sample_trace_probe ... gp.hfe ... Q 1 6 9 9`:

| Key | LCD |
| --- | --- |
| PRESET DEFINITION | `PresetDef[11-32]` |
| 1, 6 | `Erase ALL Memory / Press Y/N` |
| 9 (YES) | `Erase ALL Memory / You SURE? Y/N` |
| 9 (YES) | back to `PresetDef[11-32]` |

With the Grand Piano bank loaded and the OS disk inserted
(`SWAPDISK`):

- SAMPLE offers `VOICE V89`, and FORCE ends with `Sample is good` within
  3 s: the remaining memory is nearly full.
- After the erase, SAMPLE offers `VOICE V01`, and the forced take is
  still recording after 3 s.

Owner sequence: insert the OS disk, then PRESET DEFINITION, 1, 6, YES(9),
YES(9), then SAMPLE.

### 0.73b build

`tools/build-win64.ps1` now passes completely (no failures). WSL
`build-native.sh` passes completely. The bench gate is identical to
a78773a (modes 1/2, native frames). The PGO GUI is
`Z:\EII\EII-0.73b-sample.exe` (new file, SHA-256 ec5de012...07e8ce).

## 9. Owner feedback on 0.73b (chunked recording, noisy monitor) -> 0.73c

Owner, `EII-0.73b-sample.exe` on a real sound card: the recording "comes out
as chunks placed side by side" (right tone), and the monitor while
sampling is "distorted, noisy and glitchy, though recognisable".

### Root cause 1: the machine ran at about half real time while sampling

`sbench` (scratch; GUI route SSM192k + native + S3528, CPU placement on,
OS3.1 overlay, real panel keys), 0.73b and 0.73c built and run back to
back in the same session:

| build | idle P01 | Sample mode | Sample + 440 Hz | FORCE recording |
| --- | --- | --- | --- | --- |
| 0.73b (e3ff3c3), O3 LTO | 1.06-1.17x | 0.48-0.50x | 0.49-0.50x | 0.47x |
| 0.73c, O3 LTO | 1.22-1.25x | 1.11-1.13x | 1.06-1.09x | 1.02-1.03x |
| 0.73c, PGO (GUI profile) | 1.53-1.59x | 1.30-1.36x | 1.16-1.36x | 1.04-1.24x |

At 0.5x the 0.73b jitter buffer (hard limit target + 300 ms, trim only
+/-0.5 %) filled within about 0.7 s and then skipped its oldest data
(overrun) again and again: the stored stream was captured chunks with gaps.
The monitor glitches had the same cause: a machine at 0.5x cannot feed the
audio output in real time.

Where the time went (profiling copy with switches, same session):

- `Am6072::eiiSteadyVoltsMode` (encode-mode +IDAC.V) and `decodeCurrent`
  merged unknown bits by looping over all 256 codes, even for a fully
  known code; the sampling path called it every CPU tick.
- `ChannelTimers::flushEventTiming()` synchronised all nine counters and
  eight dividers every tick to read channel 0's phases.
- The analog front end evaluated its RCs with `exp`/`expm1` every tick.

### Fixes (the edge/reference DRAM engine and the hash gate unchanged)

- 6-tick evaluation grid. The 7-43 SAR, S/H, LM311 and deglitch are
  evaluated at t % 6 = 0, which contains both C333K edges (t % 12 = 0, 6).
  +ISMPL, phi1 and phi2 last 16 source clocks (6.4 ticks), so each pulse is
  seen. The only shorter input, a -PCSRIN read strobe (IC10b /CLR), is
  applied off-grid by `SampleSar::readStrobe`. Off-grid ticks cost one
  modulo test.
- Cached coefficients: RC alphas for the 6-tick step, the gain-stage
  coefficient per -PGAIN1 state, and the 192 kHz chain step times.
- `Am6072`: a fully known code skips the 256-code merge (the loop's only
  member; bit-identical, also for playback).
- Channel-0 phase sync only when its divider can have changed
  (`clocksUntilPhaseEvent`) or after any timer event. Probe DRAM, known
  and audio hashes are identical with and without this skip.
- Deglitch, settled per phase: C75 tracks the R27/R35 divider while phi1
  is closed, and phi2 dumps it into IINT. Against `Deglitch3202` stepped
  every 0.1 us (DAC changing at SAR edges) the maximum difference is
  1.5e-4 V on a 4 V rms signal (`sample_analog_tests`).

### Continuity independent of host timing (input-paced machine)

`LiveSampleInput` (2^21-sample ring, 43.7 s):

- While the converter samples, nothing is trimmed, skipped or
  re-prefilled. The read position advances exactly 48000 samples per
  emulated second.
- `ready()` holds the machine quantum when the captured input is not yet
  available (`paceHolds`). The GUI worker then waits 1 ms instead of
  running ahead of the capture.
- A slow host only grows the buffer (multi-second headroom).
- An underrun can only happen if the capture itself stops. It is one
  counted event that outputs 0 V and keeps the timeline: a time-aligned
  gap, never a splice. After 200 ms without capture packets the input no
  longer holds the machine.
- Quiet trimming to 60 ms happens only while not sampling (`discarded`).
- The status line and the `sample_input` trace event show: buffer ms,
  underruns, overruns, prefills, pace holds, discarded.

`tests/sample_input_live_tests.cpp`:

- Setup: 62 s; 10 ms packets with jitter; machine 0.95-1.05x with 0.8x
  periods and 45 ms stalls.
- Result: 0 underruns, overruns and discards; 1 prefill; maximum fill
  5.6 s; every consumed sample equals the source (max error 1.1e-5).

`sample_wav_probe ... live:<speed>` replays a realistic capture timeline
through the real OS3.1 overlay (FORCE, 440 Hz):

- capture: 10 ms packets delivered 10-14 ms late, every 97th one 45 ms
  late;
- machine: <speed> x real time, halved for 300 ms in every 2 s, with a
  45 ms stall every 621 quanta;
- the same pacing rule as the GUI.

| speed | pace holds | prefills | underruns / overruns / discarded | max buffer | DRAM hash |
| --- | --- | --- | --- | --- | --- |
| 0.85x | 49 | 1 | 0 / 0 / 0 | 1.5 s | 272045085696015956 |
| 0.5x | 42 | 1 | 0 / 0 / 0 | 6.2 s | 272045085696015956 |
| 0.6x (Legacy) | 45 | 1 | 0 / 0 / 0 | 4.4 s | 272045085696015956 |

The stored sample is bit-identical at every host speed. The recorded
region is 88839 bytes (3.198 s). DPCM-decoded, it is 440.00 Hz in
288 x 10 ms blocks with no phase jump > 0.25 rad (max 0.006 rad). A
splice of 10 ms of a 440 Hz input would be about 2.5 rad. Reference ==
production for each run.

### Root cause 2 (monitor quality): DPCM is clean; the Legacy renderer adds images

THD+N of a clean 440 Hz (1.0 V peak at +INPUT.V, +00 dB, FORCE), least
squares sine fit:

| signal | native route | Legacy route |
| --- | --- | --- |
| monitor while recording | -46.8 dB | -27.1 dB |
| DRAM, DPCM-decoded at 27778 Hz | -47.0 dB | -47.0 dB |
| playback, note 48 | -47.2 dB | -27.1 dB |

- Inherent: the companded DPCM loop gives about -47 dB, almost all of it
  broadband quantisation noise (H2..H5 at -55 to -99 dBc).
- Emulator-made, Legacy only:
  - The Legacy BasicVoice renderer puts the 27.78 kHz DAC steps on its
    output grid.
  - This adds images at 440 +/- k x 2444.4 Hz (2444.4 = 7 x 27777.8 -
    192000) and at 2 x 27777.8 - 48000 = 7555.6 Hz +/- 440. Observed:
    2005, 2885, 4449, 5329, 6893 and 7116 Hz, at -36 to -43 dBc.
  - Normal Legacy playback has the same images. The Legacy audio hash is
    gate-locked, so this is documented, not changed.
  - The GUI default (native) route does not have the images.
- No clipping at +00 dB (`railClips` 0). The host's +/-1.0 maps to
  +/-1.0 V, so +20/+40 dB GAIN SET overdrives the C40 summing amplifier
  (+/-13.5 V rails), as on hardware with a 1 V line signal. Use the GAIN
  knob.
- Monitor continuity: 0 phase jumps in both routes.

### 0.73c build

- Bench gate: memory, dramKnown, scannerRam, Legacy audio and native
  frames identical to the parent (modes 1/2).
- `tools/build-win64.ps1` passes.
- WSL could not start in this session (Hyper-V/virtualisation unavailable,
  HCS_E_HYPERV_NOT_INSTALLED), so `build-native.sh` was not rerun. The
  sample tests (`sample_input`, `sample_input_live`, `sample_analog`,
  `sample_machine`) pass with llvm-mingw.
- PGO GUI: `Z:\EII\EII-0.73c-sample.exe`, a new file, SHA-256
  9e87a5b8...57f961f3.
- Windows runtime with a real sound card has not been validated by us;
  the owner tests it.
