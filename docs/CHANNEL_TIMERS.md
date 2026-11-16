# Channel timer hardware (checkpoint 0.6)

The three 8254-2 chips on output-board schematic 7-40 (service PDF page 102)
are now connected to main CPU writes and a rational 10MHz clock. This is a
binary mode-2 rate-generator subset, not a general complete 8254 core.

## Board evidence

The schematic and decoder sheet 7-11 (PDF page 74) were visually inspected.
All nine CLK inputs share +C10M.D and all GATE pins are tied high. Each RD
pin is also tied high: software cannot read these physical counters through
the board. The model therefore does not invent CPU count/status readback.

| Chip | Main low-port registers | Outputs | Aliases |
| --- | --- | --- | --- |
| IC3 | 18h,19h,1Ah; control 1Bh | C0TIM,C1TIM,C2TIM | 1Ch..1Fh |
| IC2 | 10h,11h,12h; control 13h | C3TIM,C4TIM,C5TIM | 14h..17h |
| IC1 | 08h,09h,0Ah; control 0Bh | C6TIM,C7TIM,SIOCK | 0Ch..0Fh |

The main 2.1 ROM trace programs eight channel counters with 34h/74h/B4h
control words and low/high count bytes 68h/01h: mode 2, binary, divisor
0168h = 360. It does not program the ninth SIOCK counter in the tested run.
Do not infer its frequency from the eight initialized channel timers.

Primary behavioral reference: Intel *8254 Programmable Interval Timer*
datasheet, Control Word Format and Mode 2: Rate Generator, mirrored at:
https://www.scs.stanford.edu/10wi-cs140/pintos/specs/8254.pdf
The indexed manufacturer text was available; direct download of a different
Stanford mirror timed out. No source PDF is included in this repository.

## Implemented behavior

- LSB-only, MSB-only and LSB-then-MSB writes. A partial two-byte write does
  not replace the reload register or stop an existing rate sequence.
- Initial count transfer on the next falling CLK, with no decrement on that
  load edge. OUT goes low when the counter reaches 1, then high/reloads on
  the following CLK: one input-clock-wide low pulse per N clocks.
- Complete live writes wait for the current period's reload edge.
- Binary count zero means 65536. Mode aliases 6 -> 2 are decoded.
- An illegal mode-2 count of 1 is flagged, not turned into invented pulses.
- Power-up and unsupported-mode output levels remain explicitly unknown.
  Read-back/count-latch commands are counted as unimplemented and do not
  disturb the running mode. BCD and modes 0/1/3/4/5 are not implemented.

src/channel_timers.h separates the counter/reload state from board register
decoding. CPU writes reach it through Machine::output. Main/scanner buses
remain separate. The scheduler uses integer 5/2 to generate exactly 10MHz
falling-clock counts from 4MHz CPU ticks. Bulk stepping preserves pulse counts,
phase and deferred reload while avoiding a per-edge loop for large spans.

Edges are grouped after each CPU tick (2 or 3 input clocks). Absolute clock
phase and bus-strobe setup/hold within the 250ns CPU interval are not
calibrated. The next request-controller stage must preserve subcycle edge
ordering, not just sample OUT once per CPU tick: a 100ns pulse may begin and
end inside one CPU interval. The current model counts those pulses but does
not feed them into channel request logic or the SIO.

Edge counters represent clock-generated periodic edges, not every forced
OUT transition caused by reprogramming a mode. `nullCount` is an internal
not-yet-transferred indicator, not a claim of implemented status readback.

## Reproducible validation

```sh
bash tools/build-native.sh > validation/tests-channel-timers.txt
g++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc -Ivendor/chips src/chips.cpp src/machine.cpp tests/channel_timer_tests.cpp -o build/channel_timer_tests_san
ASAN_OPTIONS=detect_leaks=0 build/channel_timer_tests_san > validation/sanitizer-channel-timers.txt
build/eii_probe ../roms/main.bin ../roms/scanner.bin 40000000 > validation/timers-main21-scan31.txt
build/eii_probe ../roms/diag.bin ../roms/scanner.bin 4000000 /tmp/eii-timers-diag.csv '' '' 'HE TE' > validation/timers-diag-testhelp.txt
```

Tests cover byte loading, first-load edge, low pulse width, live deferred
reload, maximum binary count, illegal/unsupported states, bulk-vs-one-clock
equivalence, all nine output/register mappings, aliases, rational timing,
and actual Z80 OUT integration. ASan/UBSan passes.

At 40M CPU ticks the original main 2.1/scanner 3.1 firmware still reaches
`Insert Diskette`; the source has advanced exactly 100M falling clocks.
The eight initialized counters generate 269866..269870 pulse pairs each,
with slightly different phases due to sequential firmware writes. The ninth
counter remains unconfigured/unknown. Exactly 24 timer writes and zero
unsupported commands occur. Diagnostic HE TE still works. ROM identities
are unchanged (tools/rom_catalog.json).

Next hardware: the 74HC393 divider/reset and 74HC74 request latches in the
output channel, CHRQ/CHAK and the PROM microcontroller service path. The
timer output alone is not a completed sample request, memory read or DAC
update. SIO/DMA disk transport, full OS boot and integrated audio remain
absent. No new Windows build or hardware bench validation was performed.

## Superseded scheduling limitation in checkpoint 0.7

ChannelTimers now preserves individual source-edge ordering and drives
the output dividers/request-clock circuitry. See CHANNEL_REQUESTS.md for
the ideal propagation order, channel-0 distinction, unknown CHAK input
and remaining timing limits. The counter kernel still supports bulk advance,
but connected board stepping processes every source edge.
