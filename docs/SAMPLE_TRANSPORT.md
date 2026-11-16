# Playback DRAM and digital DAC path, checkpoint0.17

The shared memory port now reads playback samples through ALU/SUM address
latches and physical row/column strobes. IC40 captures MDO into CHDB; the
eight output-channel registers capture CHDB on their respective rising
-CHAK. CPU DRAM reads/writes retain their strict WAIT/consensus guards.
This milestone delivers stored eight-bit DAC codes. It does not yet convert
those codes to calibrated analog voltages, run a playable audio output,
perform ADC/SAR recording, or boot an OS disk.

## Schematic evidence and edge order

Visually checked service PDF86..88 (printed7-23..25), PDF103 (7-41), and
PDF105..107 (7-43..45). Existing source manual SHA-256:
f1071d347818ecfd107df0808ceade6d9e460987a85f86a04c71707a82700031.

| Stage | Implemented signal/behavior |
| --- | --- |
| Address | UCD15 selects CPU or ALUGO/SUM; UCD11 selects row/column byte; stable source18..16 chooses one of8 banks |
| Bank reset | UCD15 and UCD14 high assert IC99/IC31 asynchronous RASRST; an enabled playback bank requires known UCD14 low |
| Memory read | A resolved RAS row and CAS column with UCD10 low read the shared value/known-mask store; sample ownership is independent of CPU MREQ |
| IC40 | NAND(UCD08.1,-C10M.D) clocks the 74LS374; source falling can lower the gate, following rising captures OLD MDO before CAS changes |
| ACK | IC152 delays UCD08.1 to UCD08.2; the HCT138 decoder uses the post-edge CHSL selection |
| Channel clock | Y0 goes to channel7, Y1 to channel0, through Y7 to channel6; electrical -CHAK rising (release), rather than assertion, captures CHDB |
| DAC registers | Channel0 IC43 HCT374 and channels1..7 HCT273 retain separate eight-bit stored codes; none are cleared by CRST |

Counter, PROM, request pipeline and input latch still sample old state on the
shared source edge. IC40 therefore changes before the ACK release that
transfers the sample into a channel register. CRST resets CHSL; rerouting an
already asserted ACK can itself produce a channel clock. The reset path
settles that decoder change without inventing a DAC reset pin.

The class named DramCpuPort now holds the shared playback/CPU read path as
well as CPU writes. A CPU bus update/deassertion only cancels a CPU-owned
row, preserving a sample row between RAS and CAS. Sample reads never set CPU
readReady or count as CPU requests. Unresolved read strobes/addresses replace
MDO with unknown data instead of reusing a stale known sample. Active write
mode also invalidates a read result. Unsupported sample recording does not
mutate backing memory.

## Correlations and observations

Each correlated upper-controller history owns IC40 data/clock, memory strobes
and the channel ACK clocks. Channel outputs are observed only after the
history's own ACK edge. When all future controller/DRAM/ACK-clock state
agrees, the stored DAC code masks may merge as feed-forward observations.
Their values do not feed back into the current digital controller. This
avoids growing an independent branch for every past output byte. Inter-
channel output correlations can consequently be lost conservatively.

Channel0 +MODE controls the HCT374 output enable and shares the DAC input
bus with the SAR path. That driven bus, SAR conversion, analog feedback and
MODE routing are not implemented here; channel0 reports its stored register,
not an assertion about the voltage or enabled pins. Future SAR integration
must retain any output/analog state needed for its feedback.

Channel request/ACK network feedback and the lower-PROM/RFD/ALU path still use
conservative consensus. Unresolved lower-path addresses therefore yield
unknown sample data. This implementation uses ideal settled clock ordering,
not HCT propagation, hazards, setup/hold, RC, refresh retention or complete
page-mode/bank-latch timing. The tests use stable address setup. The flat
512KB address model and its CPU overlays remain as previously documented.

## Reproducible validation

Run bash tools/build-native.sh (fourteen targets). sample_transport_tests
checks IC40 old-MDO capture, row preservation across an idle CPU update,
separate CPU readiness, all rotated rising ACKs, uncertain-edge hold/capture,
bank-reset/write inhibition and absence of invented sample writes.

Its synthetic Z80 program uses real OUT instructions to initialize the
controller register file. A dummy IC92 transaction/scan establishes its idle
state before initializing the eight deliberate address registers. The test
PROM restricts the ninth-slot RF write aperture to phase2; this prevents a
write-enable word leaking into channel0 at scan wrap. No emulated SRAM or
ALU address latch is filled directly by the test program's host code.

The path is OUT staging -> RF -> RFD pipeline -> ALU SUM -> ALUGO ->
row/column DRAM read -> IC40 -> rotated ACK -> channel DAC register. Each
bank contains a different fixture byte. The test runs all16 concrete startup
phases plus their full envelope, and interleaves a strict CPU DRAM write/read.
It also invalidates one fixture byte to verify loss of stale DAC certainty,
leaves other channels intact, then verifies a subsequent new sample value.
Raw fixture bytes are host-prepared test data, not a claimed OS load/playback.

See validation/tests-sample-transport.txt, sanitizer-sample-transport.txt,
sanitizer-sample-cpu-regression.txt, sample-synthetic-cpu-original-proms.txt,
sample-main21-scan31.txt and sample-diag-testhelp.txt for runtime evidence.
No original firmware, manual images or disk data are included in this source.


Original main2.1/scanner3.1 plus both PROMs retains Insert Diskette at40M CPU
ticks, with7 CPU writes/12 reads/95 WAIT holds and32 paired PIO transfers.
The original boot snapshot has unknown IC40/DAC codes; the OS/sample data
has not been loaded. Diagnostic4M HE TE remains Static RAM OK/help.
