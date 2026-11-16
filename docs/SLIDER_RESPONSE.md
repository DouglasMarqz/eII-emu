# Slider response (A/B/C/D): root-cause investigation, 2026-10-30 (revised)

Owner report: slider A lags badly in edits (VCA ADSR, VCF), while B/C/D are
fast. With the 0.3.22 fast-panel patch, A is better but still slower. Rapid A
moves seem to delay the LCD and the sound and to shift notes. The fast panel
also seems to break the arpeggiator tempo.

Method:
* Real ROMs (main 2.1, scanner 3.1, IC135/IC137), read-only bank
  "18 Grand Piano #2", production controller, emulated time, no GUI and no DSP.
* `tools/slider_latency_probe` and `tools/vst3_host_test ... sliders`.
* Scanner ROM disassembly (`tools/rom_disasm`).
* Rendered service sheets 7-6 (PDF69) and 7-8 (PDF71).
* Logs: `validation/slider-latency-2026-10-30/`.

## 1. Hardware path (rendered sheets) = model

* **ADC0809 IC27** (7-8):
  - IN0 = A/D IN (J6), IN1 = +IENV, IN2 = LWHEEL, IN3 = RWHEEL.
  - **IN4 = +SLDRA (CN3-50)**, IN5 = SLDRB (CN3-49), IN6 = SLDRC (CN3-48),
    IN7 = SLDRD (CN3-47).
  - Address = SD2..0. ALE and START come from -SCSWC through IC48. The clock
    is -C500K (500 kHz). EOC drives +SCI.D.
  - The model's mapping (SliderA..D → IN4..7) is identical.
* **Scanner CTC IC45** (7-6): CK = +SCLK (4 MHz), CT1 = -PRXC, CT2 = +C2MHZ,
  **CT3 = +SCI (ADC EOC)**. The model drives exactly these pins
  (`machine.cpp`: CLKTRG2 = ticks&1, CLKTRG3 = EOC, CLKTRG1 = SMPTE clock).

## 2. Scanner firmware (ROM 3.1, 478cf8e0)

Vectors at I = 87h:
* CTC0 0826h: 0.5 ms tick (control 87h, TC 7Dh = 4 MHz/16/125).
* CTC1 13EDh: SMPTE.
* CTC2 12C8h.
* **CTC3 0BF3h: ADC service.**
* PIO 0F77h / 08BCh.

* **Pacing:** CTC3 control BFh, TC 20h: timer, prescaler 256, started by the
  EOC rising edge. So the next conversion is serviced 8192 clocks = 2.048 ms
  after each conversion ends, about 2.2 ms per conversion.
* **Schedule:** 16 slots. Channels 0, 2 and 3 have four slots each; IN1 and
  sliders 4..7 have one each. So **each slider is sampled once per ~35-40 ms**.
* **Filter, channels ≥4 only (0C23h-0C4Ah):** two cascaded fixed-point
  half-averages per sample, then rounding.
* **Hysteresis (0C57h-0C7Ch):** a difference of 0 or ±1 is ignored. ±2 or ±3
  is reported only after three consecutive samples. Larger differences are
  reported at once.
* **Report (0CA0h, 0662h-0686h):** a changed channel sets its flag bit (table
  0ECDh: ch4..7 = 10h/20h/40h/80h). The main loop sends `F0h+ch, value>>1` to
  the main CPU through the PIO A ring. Flow control: main can hold the scanner
  through 8297h.
* **All four sliders run through identical code and identical
  per-channel state.** There is no channel-4 special case, and the state areas
  82A5h+5·ch and 8258h/8260h+ch do not overlap other variables.

## 3. Measurements (emulated time)

ADC conversions in 2 s: IN0 198, IN2 198, IN3 199, **IN4..IN7 50 each** (25/s,
40 ms period). That is within about 5% of the ROM/CTC arithmetic of ~38 ms.

VCA/LFO page "Atk Dec Sus Rel": one slider steps 0→3 V and back, with the
current voice V04.

| Slider (parameter) | Stock: first LCD / settled | Fast patch: settled |
|---|---|---|
| A (Atk) | 22 / 388 ms, then 30 / 272 ms | 42 / 31 ms |
| B (Dec) | 42 / 410 ms, then 17 / 260 ms | 32 / 22 ms |
| C (Sus) | 36 / 402 ms, then 7 / 253 ms | 25 / 14 ms |
| D (Rel) | 25 / 399 ms, then 43 / 289 ms | 13 / 43 ms |

Filter page "Freq Q Env" (A = Freq, B = Q, C = Env): stock A 389/273 ms,
B 375/138 ms, C 402/167 ms.

* **Scanner report → LCD:** 1.5-3 ms (main CPU + PIO link + scanner LCD
  write). The upward steps of the ADSR page show 17 ms, from the firmware's own
  value computation.
* **A sequence of 9 LCD values** for a step (31, 60, 81, 96, 105, 110, 113,
  115, 117) equals the two-stage average exactly.
* **Rapid A sweeps** (Tune page, 2.5 Hz triangle for 2 s, then hold):
  - Scanner→main TX backlog ≤ 1 byte.
  - LCD final value 146 ms after the hold (stock), 43 ms (fast).
  - No lag accumulates.
* **Main CPU:** DRAM wait 1.1% of cycles, with or without notes. No
  starvation.
* **Note timing** (24 MIDI notes, 8/s), MIDI → attack:
  - Static sliders: 15.9 ms ± 2.7 ms.
  - All four sliders swept at 5 Hz: 18.4 ± 2.8 ms stock, 16.2 ± 3.1 ms fast.
  - No added jitter (sd unchanged).
  - The earlier arp measurement (validation/arp-panel-2026-10-27) shows
    125.9/125.7 ms, steady/moving.
* **Real plug-in, dense host automation** on A or B (0/1/16/64 points per
  512-frame block, 6 s each): wall time per emulated second 0.757 / 0.759 /
  0.761 / 0.766 (A) and 0.756 / 0.749 / 0.760 / 0.786 (B). No DSP or event
  drops. Splitting the runs at each point costs < 1.2%.

## 4. Owner observations of 2026-10-30 (retry) and what they predict

* The mod wheel does not disturb notes; the sliders do.
* Slider moves delay external MIDI notes and the sound, not only the arp.
* The fast panel breaks the arp tempo during slider moves, mostly A and B.

What differs between sliders and wheels, layer by layer:

| Layer | Wheels (IN2/IN3) | Sliders (IN4..7) |
|---|---|---|
| Scanner schedule | 4 of 16 slots each (~100/s) | 1 of 16 (~25/s) |
| Scanner filter | none, +-1 hysteresis (0C80h) | 2 cascaded half-averages + 3-sample hysteresis (0C23h-0C7Ch) |
| Message | `F2h/F3h, v>>1` | `F4h..F7h, v>>1`, same ring, same PIO |
| Main CPU | CV modulation only | in a module page: parameter edit, LCD rewrite, voice parameter recompute (OS code 5900h-5BFFh) |
| Plug-in host path | GUI wheels: the same 6-control queue; MIDI wheel: CC -> bridge -> same ADC input | the same 6-control queue |

So the only slider-specific work is the stock firmware itself (filter in the
scanner, the edit in the OS). The questions were whether that work starves
MIDI note servicing or the voice updates, and whether the emulation makes it
slower than on hardware.

## 5. New measurements (emulated time unless noted)

`slider_latency_probe jitter-play|jitter-vca|jitter-filter [fast]`: 24 external
MIDI note-ons per condition, 125 ms apart; one input swept 0.5-4.5 V as a
triangle at 2 Hz and 8 Hz (rapid back-and-forth). Markers: first channel-timer
write after the note-on (the OS servicing the note; independent of the edited
envelope) and the first amplitude-CV rise. Also MIDI receiver overruns
(SCN2651 holding register) and the longest RXRDY-to-read time.

MIDI note-on -> first channel-timer write, mean / max (ms):

| Page, firmware | idle | LWheel 2 Hz | RWheel 2/8 Hz | A 2/8 Hz | B 2/8 Hz | C 2/8 Hz | D 2/8 Hz |
|---|---|---|---|---|---|---|---|
| VCA, stock | 13.5 / 15.1 | 13.7 / 16.1 | 14.7 / 16.4, 14.1 / 16.1 | 13.0 / 18.7, 12.4 / 19.5 | 14.9 / 16.6, 13.6 / 15.9 | 14.0 / 15.6, 13.8 / 16.9 | 14.4 / 15.6, 14.1 / 15.6 |
| VCA, fast | 13.5 / 15.1 | 13.6 / 16.1 | 12.8 / 15.9, 12.9 / 16.1 | 10.2 / 20.0, 13.2 / 21.0 | 12.4 / 15.1, 11.9 / 15.1 | 13.5 / 15.1, 13.9 / 15.1 | 12.6 / 15.1, 12.8 / 16.1 |
| Filter, stock | 13.6 / 15.6 | 14.9 / 15.6 | 13.7 / 16.1, 14.2 / 16.1 | 14.2 / 16.6, 14.1 / 17.2 | 12.9 / 16.4, 13.0 / 17.9 | 13.1 / 17.4, 13.1 / 15.6 | 11.8 / 18.2, 13.6 / 18.4 |
| Play mode, stock | 13.8 / 15.1 | 14.5 / 15.6 | 13.7 / 16.4, 12.9 / 16.4 | 13.9 / 16.9, 13.9 / 16.9 | 14.5 / 17.7, 14.6 / 17.4 | 13.4 / 16.4, 13.9 / 17.2 | 14.0 / 15.9, 13.6 / 17.7 |

* With 6 voices held (JITTER_HOLD=6, Filter page, stock and fast): the same,
  max <= 18.2 ms against 15.4 ms idle.
* MIDI overruns: 0 in every condition. Longest RXRDY-to-read: 48-80 us
  (the RX interrupt), the same idle and swept.
* Main CPU instruction rate is unchanged (448-459 per ms). An 8 Hz slider-A
  sweep in a module page adds 4-11 % main-CPU work (OS 5900h-5BFFh, ROM
  0600h-0B00h, 2A00h-2F00h; PC histogram with EII_DIAGNOSTICS).
* The note's amplitude-CV rise follows the timer write by ~2.3 ms in every
  condition except while the VCA attack itself is swept (the attack changes).
* Arp: the available banks ("18 Grand Piano #2", "05 Marcato Strings (no
  sequences)") do not start the arpeggiator from held notes, so no new arp
  number. The 2026-10-27 measurement (125.9 / 125.7 ms steady / moving) stands.

Whole plug-in (`vst3_host_test ... rtsliders [fast]`, Windows, real DLL,
48 kHz, 256-frame blocks), one control moved 4 Hz back-and-forth: host points
(A, B, C, D, right wheel), a no-echo GUI edit (A, B), MIDI CC1 through the
wheel bridge:
* Offline (exact): MIDI note -> audio onset beyond the reported latency,
  mean 18.7-22.6 ms in every condition, idle included; no event drops.
* Realtime (wall-clock paced): no late frames in most conditions, machine
  load 0.80-0.85. One rebase (100 ms catch-up) per run hit a random
  condition (B-gui, then A-host, then the final idle). It is host CPU
  headroom on this PC (~15 % spare at 0.85), not the slider.

## 6. Conclusion

* **A-D parity holds** at every layer: identical scanner code, one schedule
  slot each, settle 382-409 ms up / 248-285 ms down (stock).
* **No emulation fault** was found that delays notes during slider moves. In
  emulated time, slider sweeps do not delay MIDI note servicing beyond the
  idle spread (+1 to +4 ms max, mean unchanged), with or without held voices,
  stock or fast. Wheels and sliders behave the same there.
* **The slider-only lag is the stock scanner firmware** (two-stage average
  and 1/16 schedule), which the emulation reproduces to within ~5 % of the
  ROM/CTC arithmetic. No hardware-faithful change can make scanner 3.1 respond
  faster: CTC, ADC0809 and wiring match the rendered sheets.
* **Where the owner's MIDI/arp delay can come from instead:**
  1. Realtime headroom. The machine thread needs ~0.80-0.85 of a core on the
     test PC; a stall longer than the safety margin rebases (audible as a
     drop/delay). It happened with idle controls too. The status counters
     "rebases" / late frames during a slider move on the owner's PC would
     show it.
  2. The audible effect of the edit itself (VCA attack/decay or the filter
     moving 0.25-0.4 s after the hand).
  3. A different scanner EPROM in the owner's EII+ HD. Only the IC28 label
     (or a dump) can settle it.

**Fast panel:** keep it, default OFF. It is a firmware modification, not a
hardware fix; it does not make note servicing worse in emulated time.

**Regression checks** (need ROMs; exit code 3 on failure):
* `slider_latency_probe ROMDIR BANK vca|filter`: first LCD <= 60 ms per slider,
  A-D settle spread <= 25 %.
* `slider_latency_probe ROMDIR BANK jitter-vca|jitter-filter|jitter-play [fast]`:
  each swept condition within idle +3 ms mean / +6 ms max, all 24 notes, no
  MIDI overrun.
* `vst3_host_test BUNDLE ROMDIR FLOPPY OUTDIR rtsliders [fast]`.

Logs: `validation/slider-latency-2026-10-30/`.
