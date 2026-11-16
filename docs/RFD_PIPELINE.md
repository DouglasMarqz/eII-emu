# RFD bus and data pipeline, checkpoint0.13

Visually verified service7-18/7-20/7-21 (PDF81/83/84). The running controller
now resolves the shared24-bit RFD bus per startup trajectory. The SRAM drives
while -RFWR is high; CPU IC120..122 drive while -MPWEN is low; ALU IC100..102
are enabled when UCD06.1 is low. ALU arithmetic/latches have since been
implemented; see CONTROLLER_ALU.md for checkpoint0.23 shared-sample refinement.

The settled digital resolver preserves only bits agreed by every potentially
active driver, provided some driver is certainly active. A floating bus has
unknown voltage; it is not pulled high or zero-filled. Contention loses the
conflicting bits. Unknown enable/value correlations across devices remain
conservatively untracked. This is not an analog contention/propagation model.

At a rising source edge, sample the OLD low-phase bus before SRAM write
completion, output re-enabling, PROM latches and counter updates. During a
CPU write this may be CPU data, not the subsequently enabled SRAM output.
The same pre-edge data supplies the register write aperture and data latches.

| Device | Captured inputs |
| --- | --- |
| IC148 | RFD7..0 -> RFD7.1..0.1 |
| IC150 | RFD15..8 -> RFD15.1..8.1 |
| IC152 DA..DD | RFD19..16 -> RFD19.1..16.1 |
| IC152 DF | RAW PROM +UCD04.D -> +UCD04.1B |
| IC141 QB/QC/QD | RFD22/21/20 -> RFD22.1/21.1/20.1 |
| IC141 QA | OLD latched UCD09.1 |
| gated IC140 | RFD23/22/21/20 -> PRECAR/LOOPMD/DECFLG/BACKMD |

Important: IC152 UCD04.D is the raw IC137 PROM output, without .1 suffix.
Thus UCD04.1A (IC138) and UCD04.1B (IC152) sample the same raw PROM bit on
this ideal in-phase edge. It is NOT an additional source-clock delay like
UCD08.1 -> UCD08.2. Both raw PROM sampling paths use the old feedback,
counter and captured flags. IC140's newly captured flags cannot affect the
PROM latch until the next edge. IC141/148/150/152 have no CRST clearing.
Data pipeline bits19..0 and delayed flags remain unknown at startup.

Machine now derives IC140 input flags from the resolved RFD bus. The explicit
flag argument remains available only for isolated timing fixtures with the
register datapath disabled. All16 startup paths keep independent SRAM,
pipeline and sampled-bus state; public inspection returns bitwise consensus.

Validation: bash tools/build-native.sh (ten targets), plus ASan/UBSan of
controller_data_tests. The resolver is checked against independent concrete
enumeration of all729 ternary enable/data combinations for three drivers.
Tests cover floating/contention behavior, every data/flag lane and known mask,
raw UCD04 simultaneous capture, scheduled CPU write plus SRAM read into the
pipeline/IC140, reset retention and16-path vs concrete-phase pipeline state.

Since this original0.13 milestone, ALU/DECCTL, strict DRAM/WAIT, sample DAC
transport and an optional nominal analog engine have been connected. See
CONTROLLER_ALU.md, CPU_DRAM.md, SAMPLE_TRANSPORT.md and CLOCKED_PLAYBACK.md.
The UCD04.1A/.1B joint-sample relation is now retained from their first common
capture, without claiming full RFD/control/flag-history correlation recovery.
Unknown ALU data must not be reported as completed correct ROM register
initialization. Recording/SAR, floppy boot and host audio remain incomplete.
