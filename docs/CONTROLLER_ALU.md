# Controller ALU, checkpoint0.50

Visually verified service7-22/PDF85 and7-18/PDF81 enlarged pin/connection
views. Five74S283s form a20-bit carry chain, IC123 low nibble through IC127
high nibble. B19..0 comes from OLD RFD19.1..0.1 pipeline.

| Input/output | Wiring |
| --- | --- |
| A19..10 and A7..0 | UCD04.1A, one continuous vertical wire |
| A9..8 | UCD04.1B, both bits |
| initial CI | UCD03.1 |
| RFD19..0 | sum19..0 |
| RFD23 | carry OUT of the20-bit chain |
| RFD22 | delayed RFD22.1 |
| RFD21 | DECCTL |
| RFD20 | delayed RFD20.1 |

For known values, sum=B+(UCD04.1A?0xFFCFF:0)+(UCD04.1B?0x300:0)+UCD03.1.
Carry is routed to bit23, not bit20. DECCTL is RFD21.1 XOR
(!DECFLG AND PRECAR AND BACKMD), using IC141 delayed RFD21 and IC140 flags.
UCD04 A/B sample the same raw PROM bit in the ideal in-phase clock model;
the ALU receives their OLD latched values on the next gated capture.

IC99 gives NAND(UCD05.1,-C10MHz.1) to IC100..102 S374 clocks. A source
falling edge lowers this gated clock when UCD05 is high. The following
source rising edge captures arithmetic from OLD pipeline/flags/control.
Low UCD05 holds the output. Uncertain gated edges merge held/captured bits.
No CRST clears these latches. RFD read/write/pipeline sampling on that edge
uses the PREVIOUS ALU output, before the new latch value appears. UCD06.1
low enables ALU output to the shared bus; SRAM/CPU contention is still resolved
conservatively, without a priority override.

Partial-known arithmetic now preserves the shared control wires. UCD04.1A
is one unknown Boolean reused at eighteen A pins, as is UCD04.1B. Up to eight
compatible assignments of CI/A4a/A4b are enumerated. For each fixed addend,
independent operand-bit carry propagation is exact; branch outputs are merged
by consensus. Fully known operands use integer addition. An arbitrary unknown
20-bit operand has unknown sum bits. Fixed addend0 forces carry0; addend0x100000
forces carry1; intermediate addends leave carry uncertain. No startup operand
or control value is selected.

Example: B=0x100, CI=A4a=0, A4b unknown gives either0x100 or0x400. Sum bit9
is zero in both cases. The former bitwise approximation lost that certainty
because it treated repeated unknown control pins independently. The new test
fails against the old implementation and passes after the fix; the negative
control is recorded in validation/alu-shared-control-before.txt.

Checkpoint0.23 also retains the relation between the TWO UCD04 latches after
joint sampling. IC138 DE and IC152 DF receive the same raw IC137 UCD04.D;
both capture the same rising source edge in this ideal settled-clock model.
Service PDF81/84/85 (7-18/7-21/7-22) was visually rechecked. Per-phase
sameUcd4Sample starts FALSE: separate power-up latch contents are unrestricted.
It becomes true only AFTER an enabled pipeline/source rising capture, so that
edge's arithmetic still uses the old relation/operands. Falling edges, PROM
loading and CRST do not establish it; CRST and later PROM reload retain it.
Once established, compatible A4a/A4b assignments are only00 or11. A known
sample on either side constrains the other before the arithmetic fast paths.
No relation with UCD03, operand bits or input flags is inferred.

Example: B=0x100, CI=0, both UCD04 values unknown. Before their first joint
capture, offsets0/0x300/0xFFCFF/0xFFFFF remain possible. Afterwards only0 or
0xFFFFF remain: the low20 sums are0x100 or0x0FF; carry differs. Thus the
linked controls implement hold/increment or decrement/hold according to CI.

Checkpoint0.50 corrects an earlier schematic-reading error: the eighteen
other A inputs were previously treated as grounded. The vertical conductor
joins IC123/124/126/127 A0..3 to IC125 A2/A3 and UCD04.1A. The previous
0xF00 constant turned a decrement into a large forward jump. This corrupted
sample traversal, particularly reverse/looped playback. Both the reference
and production engines shared that error, so equivalence alone missed it.
The integer oracle now traces each of the five four-bit adders separately.

Exactness here means the local input abstraction: independent unknown operand
bits, CI and UCD04 controls, plus this optional proven equality. It does not
recover relations between operand bits or across controller/RFD/flag histories. Broader phase/data histories remain conservatively represented.
Physical propagation/gated-clock hazards, setup/hold and clock-buffer phase
offsets are not calibrated. Schematic PDF85/7-22 was visually rechecked for the
paired A inputs; latch ordering, DECCTL and carry-to-RFD23 wiring are unchanged.

Validation: bash tools/build-native.sh (eighteen targets); ALU ASan/UBSan.
Known operands exercise nibble/byte/20-bit carry boundaries and all control/
flag combinations against an independent integer/pin reference. Partial
operands and ternary controls now require the EXACT known-mask consensus of
compatible full integer outcomes, not merely soundness. Four variable-bit
groups spanning CI/byte/repeated-control/20-bit overflow boundaries, three
backgrounds,81 operand patterns and27 control patterns give26244 independent-control partial cases. Another20412 compatible jointly
sampled cases bring the total to46656. A connected synthetic lower PROM maps
unknown PRECAR to UCD04 while CPU staging supplies a known low20 word; actual
controller ALU captures must preserve the additional known bits. Disabling only
the call-site relation in an isolated temporary source copy fails that test,
recorded as an EXPECTED failure in validation/alu-joint-sample-negative.txt.
Relation lifetime checks include all16 startup phases, disabled datapath,
falling/rising edges, CRST and PROM reload. DECCTL
has its81 ternary cases checked independently. Gated edges, hold, uncertain
capture and reset retention are covered. A synthetic PROM repeatedly reads
register5, clocks its pipelined value+1 through the ALU and writes back over
the real RFD/RFWR path; no direct SRAM update. Per-path ALU/output/writeback
consensus is checked against16 separate concrete initial-phase runs.

DRAM service/WAIT and sample DAC transport have since been implemented; see
CPU_DRAM.md and SAMPLE_TRANSPORT.md. Nominal clocked analog experiments exist
but physical calibration, recording/SAR and floppy transport remain incomplete.
This is no claim of OS boot, playable sampler or physical timing calibration.
