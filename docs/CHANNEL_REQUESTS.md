# Channel divider and request timing (checkpoint 0.7)

The CPU-connected 8254 clocks now feed eight output-channel divider models.
Service manual PDF pages 106..113 (schematics 7-44..7-51) were visually
inspected, including enlarged traces around the clear and request-clock nets.
No manual scans, ROMs or disks are included here.

## Connected logic

The first HC393 half counts falling +C10M.D edges. Its Q3 falling edge
clocks the second half. Inverted -CnTIM asynchronously clears only the
second half. This distinction matters: the first half can advance during a
one-clock timer pulse and is not reset directly by that pulse.

| Channels | First-half clear | Request latch CLK | Settled switch phases |
| --- | --- | --- | --- |
| 1..7 | Second Q2 | Second Q2 rising | Q0 & !Q1; Q0 & Q1 |
| 0 (sampling) | Second Q3 | Second Q2 rising | Q0 & !Q1 & !Q2; Q0 & Q1 & !Q2 |

Channel 0 additionally decodes +ISMPL as Q0 & !Q1 & Q2. It continues past
the Q2 request edge and stops when Q3 goes high. Channels 1..7 stop at Q2.
Thus a synchronized playback divider takes 64 source clocks to stop; the
sampling divider takes 128. Starting partway through a first-half count
changes the delay. Phase decodes expose settled levels only, not actual
3202 switch conduction, capacitor voltage or sampling conversion.

The HCT74 has D and /PRE tied high, +CHRQ at Q, and -CHAK at /CLR. Explicit
`acknowledge(false)` clears it asynchronously; release alone does not set
it. A subsequent Q2 rising edge sets it if clear is known inactive. The
PROM/controller does NOT yet drive -CHAK in Machine, so Machine reports
CHRQ unknown instead of inventing an inactive acknowledge input. Synthetic
tests drive this input explicitly. Request-clock counts are observable
upstream of that unknown latch clear; they are not completed sample reads.

Primary chip behavior references:
- TI HC393: https://www.ti.com/lit/ds/symlink/sn74hc393.pdf
- TI HCT74: https://www.ti.com/lit/ds/symlink/sn74hct74.pdf

## Startup and scheduling

There is no assumed zero power-up state. A set of possible divider states
is retained until feedback stopping followed by a timer clear establishes
a unique phase. Unused second-half bits are omitted. Unknown timer inputs
keep a conservative divider set. Guaranteed Q2 rising edges and ambiguous
startup-edge opportunities are counted separately; ambiguous counts are
not claims of physical pulse counts. The request state abstraction is
conservative and does not retain correlations with individual divider
candidates. After synchronization it follows the deterministic circuit.

Every 10MHz falling edge is now processed separately inside the existing
2/3-edge CPU frame. The divider first consumes the previous timer level,
then the 8254 advances, then its new output applies asynchronous clear.
This explicit ideal propagation order preserves 100ns pulses that could
otherwise disappear within a 250ns CPU frame. Request timestamps use the
monotonic 10MHz source-clock index, not rounded CPU ticks. CPU bus writes
still occur at the CPU-frame boundary. Actual clock phase, propagation
and setup/hold delays are uncalibrated. Ripple decode glitches and races
between an HC393 output and an 8254-generated reset on the same source edge
are not modeled. Do not describe this as transistor/cycle-exact board timing.

## Validation

```sh
bash tools/build-native.sh > validation/tests-channel-requests.txt
g++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc -Ivendor/chips src/chips.cpp src/machine.cpp tests/channel_request_tests.cpp -o build/channel_request_tests_san
ASAN_OPTIONS=detect_leaks=0 build/channel_request_tests_san > validation/sanitizer-channel-requests.txt
build/eii_probe ../roms/main.bin ../roms/scanner.bin 40000000 > validation/requests-main21-scan31.txt
build/eii_probe ../roms/diag.bin ../roms/scanner.bin 4000000 /tmp/eii-requests-diag.csv '' '' 'HE TE' > validation/requests-diag-testhelp.txt
```

Tests cover unknown startup and convergence, both feedback stopping taps,
channel-0 sampling decode, playback phases, asynchronous acknowledge priority,
no retrigger on acknowledge release, the first half running while the second
is cleared, an independent elementary two-counter reference, 100ns pulses
inside CPU frames and Machine integration. Firmware results and sanitizer
status are recorded with this milestone. Full OS boot, PROM service/sample
transfer, audio and Windows runtime validation remain unfinished.
