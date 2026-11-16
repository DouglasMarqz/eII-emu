# Channel request selection and acknowledge routing (checkpoint 0.8)

Output-board schematic 7-41 (service PDF page 103) was visually checked.
The next connected block consists of IC26, a 74HCT151 request multiplexer,
and IC27, a 74HCT138 active-low acknowledge decoder.

IC26 has /EN grounded, A2..0 = CHSL2..0, D7..0 = CHRQ7..0, and the
non-inverting output (pin 5) drives +UREQ. It selects a request; it does not
OR all pending channels or choose a priority winner.

IC27 has both active-low enables grounded and its active-high enable is
+UCD08.2. Its select is also CHSL2..0. The decoder output wiring is rotated:

| CHSL | UREQ input | Asserted ACK when UCD08 is high |
| --- | --- | --- |
| 0 | CHRQ0 | -CHAK7 |
| 1 | CHRQ1 | -CHAK0 |
| 2 | CHRQ2 | -CHAK1 |
| 3 | CHRQ3 | -CHAK2 |
| 4 | CHRQ4 | -CHAK3 |
| 5 | CHRQ5 | -CHAK4 |
| 6 | CHRQ6 | -CHAK5 |
| 7 | CHRQ7 | -CHAK6 |

This offset is physical wiring, not a software off-by-one correction.
A low UCD08 releases every ACK, regardless of channel selection. The ACKs
feed the HCT74 asynchronous clears already modeled in channel_requests.h.
The request mux is reevaluated immediately after controller-input changes
and request changes at each source edge, after acknowledge clears settle.
No CPU I/O port was invented for the controller pins.

src/channel_service.h evaluates both known and partially unknown inputs.
A known-bit mask describes CHSL. A decoder line is known inactive if its
code cannot be selected, even when another select bit or enable is unknown.
If all selectable requests are known and equal, UREQ is known even with
an unknown selection. Unknown input data outside the possible selection set
does not contaminate the result. Every uncertain value must be interpreted
through its `known` flag; its stored boolean is not an electrical assertion.

ChannelTimers::driveControllerPins is the explicit integration boundary for
CHSL2..0 and UCD08. It drives physical clears and receives request feedback.
Synthetic tests exercise this boundary. Machine leaves these controller
inputs unknown because the 74S161 scan/state counters, input latches and
clocked PROM execution are not yet connected. Thus this milestone is a
connected output-board service network, NOT completed PROM-driven service,
DRAM reads or DAC updates. PromLatch still models lookup/latching only.

Controller pin updates are atomic ideal logic operations; no decoder hazards,
propagation delays, setup/hold or physical simultaneous-edge races are modeled.
The source-clock ordering and channel-0 distinction from CHANNEL_REQUESTS.md
remain in force.

## Validation

```sh
bash tools/build-native.sh > validation/tests-channel-service.txt
g++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc -Ivendor/chips src/chips.cpp src/machine.cpp tests/channel_service_tests.cpp -o build/channel_service_tests_san
ASAN_OPTIONS=detect_leaks=0 build/channel_service_tests_san > validation/sanitizer-channel-service.txt
build/eii_probe ../roms/main.bin ../roms/scanner.bin 40000000 > validation/service-main21-scan31.txt
build/eii_probe ../roms/diag.bin ../roms/scanner.bin 4000000 /tmp/eii-service-diag.csv '' '' 'HE TE' > validation/service-diag-testhelp.txt
```

The ACK truth table is exhaustively checked against enumeration for all
select values, all select known-bit masks and known/unknown enable states.
Tests also cover every mux input, unknown-input isolation, all eight request
latches through assert/release/clear feedback, immediate UREQ updates and
held-clear dominance. Native and sanitizer results are captured in the logs.
No new Windows build or runtime validation.
