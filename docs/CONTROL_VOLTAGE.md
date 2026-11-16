# Firmware control-voltage bus (checkpoint 0.5)

This checkpoint connects CPU writes to an ideal control DAC and track/hold
router. It does not connect the SSM2045 laboratory to Machine audio, calibrate
the analog circuits, or claim that the OS is running.

## Wiring evidence

Service-manual PDF page 74 (7-11) and page 101 (7-39) were rendered and
visually checked, including enlarged DAC-bit and mux-inhibit connections.
IC86 write decoder uses PA5..3 with PA6/PA7 enables: output 6 is PCSWMX,
output 7 is PCSWDA. Thus main low ports 70h..77h select the mux latch and
78h..7Fh write the DAC. PA2..0 are decode aliases, not channel selectors.

IC45 is a six-bit 74HCT174 latch with its reset pin tied high, not to CRST.
PDO2..0 select X0..7 on all three 4051s. The remaining bits are:

| Latched bit | Signal | Analog bank |
| --- | --- | --- |
| PDO3 / QC | IC77 inhibit | ACV (amplitude) |
| PDO4 / QB | IC87 inhibit | FCV (filter frequency) |
| PDO5 / QA | IC97 inhibit | QCV (resonance) |

High inhibit disconnects a bank. More than one bank may be enabled at once.
All three switches share the DAC output at COM. Every X output drives its
own nominal 1000pF hold capacitor and TL084 voltage follower. Twenty-four
voltages exist, eight per bank. A mux change can connect a different hold
capacitor to the *previous* DAC value; it is not just an address for a later
write. A DAC change updates all currently connected banks.

IC63 AD7524: PDO7..0 connect normally to MSB..LSB (pins 4..11); there is no
bit reversal. VREF is -5V. OUT1 plus the internal feedback resistor and
IC44 inverting amplifier give the ideal unipolar conversion:

```text
Vout = -VREF * code / 256 = 5 * code / 256 volts
```

Hence code 00h -> 0V, 01h -> 0.01953125V, 80h -> 2.5V, FFh -> 4.98046875V.
Dividing by 255 would be incorrect. Manufacturer checks:

- Analog Devices AD7524 Rev. B, Unipolar Binary Operation / Table I:
  https://www.analog.com/media/en/technical-documentation/data-sheets/ad7524.pdf
- TI CD405xB truth table: inhibit high selects no channel:
  https://www.ti.com/lit/ds/symlink/cd4053b.pdf

## Implementation

src/control_voltage.h contains the register/routing subset. Machine routes
main CPU output strobes through it. The main/scanner buses remain separate;
scanner writes cannot reach the main-side DAC. Decoder aliases are retained.
Report output includes DAC/mux write counts, known hold count and only those
voltages established by writes to known DAC and mux states.

The DAC latch, mux latch and all capacitor charges start *unknown*. Numeric
storage defaults are not treated as valid power-up voltages. The hardware
does not supply a reset that establishes zero for these two latches. A known
output requires both DAC and mux writes which establish a connection.

This is an ideal instantaneous-acquisition, zero-leakage model. DAC/IC44
settling (including C39), switch on-resistance, charge injection, capacitance
loading, break-before-make duration, hold droop, follower offsets/bandwidth
and sample/hold-to-chip control networks remain unmodeled. `lastTrackTick`
records the last model write event which updated a held voltage, not a
measured acquisition/settling completion. No guessed time constant is used.

## Reproduce validation

```sh
bash tools/build-native.sh > validation/tests-control-voltage.txt
g++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc -Ivendor/chips src/chips.cpp src/machine.cpp tests/control_voltage_tests.cpp -o build/control_voltage_tests_san
ASAN_OPTIONS=detect_leaks=0 build/control_voltage_tests_san > validation/sanitizer-control-voltage.txt
build/eii_probe ../roms/main.bin ../roms/scanner.bin 40000000 > validation/control-main21-scan31.txt
build/eii_probe ../roms/diag.bin ../roms/scanner.bin 4000000 /tmp/eii-control-diag.csv '' '' 'HE TE' > validation/control-diag-testhelp.txt
```

Tests verify all 24 output addresses, individual and simultaneous bank
enables, hold under inhibit, DAC endpoints/LSB size, unknown power-up, mux
changes with existing DAC data, aliases and actual Z80 OUT integration.
Digital and analog component tests remain passing. ASan/UBSan control-bus
tests pass.

Main 2.1 + scanner 3.1 still reaches `Insert Diskette` in 40M clocks.
That boot ROM run has zero control-DAC/mux writes and zero known held
voltages. This is evidence of an integrated port model, not evidence of
real OS envelopes or successful audio generation. Diagnostic `HE TE`
continues to show `Static RAM OK` and test help. ROM hashes are unchanged
and listed in tools/rom_catalog.json. No new Windows build was run.

Next: trace the channel's FC/Q/amplitude pin networks and their dynamics;
implement timing/settling only with justified parameters, and preserve the
unknown-state boundary until firmware establishes controls. SSM Q feedback,
S3528 reconstruction, channel timers, sample DAC/PROM datapath and SIO/DMA
OS boot are still required for an operational instrument.
