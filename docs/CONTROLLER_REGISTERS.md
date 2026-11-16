# CPU register staging and register-file write port, checkpoint 0.12

The first section records0.11; the0.12 section below supersedes its
unconnected SRAM/write-control status. Read-pipeline/ALU limits still apply.

Visually verified EII service manual PDF74/82/83 (printed7-11/7-19/7-20).

| Main OUT ports | Physical destination |
| --- | --- |
| 48h–4Fh | IC119: RFA5..0 address latch; PDO7..6 unused |
| 50h–57h | IC122: RFD7..0 latch |
| 58h–5Fh | IC121: RFD15..8 latch |
| 60h–67h | IC120: RFD23..16 latch; handshake initiation remains unconnected |
| 68h–6Fh | IC64 bank registers, PA1..0 index; PA2 alias now included |

IC119..122 are LS374s with no reset: latched bits begin unknown. Running Z80
writes are retained while selected, then captured on the first CPU boundary
where -PCWS rises. Direct Machine::output calls represent completed strobes.
Sub-CPU-tick strobe transitions and propagation delays are not calibrated.
Writes to staging do not immediately modify the register-file SRAM.

The IC118 address-selection kernel selects CPU RFA when -MPWEN is low,
controller RFA when high. Controller RFA5..2 = CHSL3..0 and RFA1..0 = UCD01..00.
An unknown enable returns bitwise consensus. The ninth scan slot therefore
addresses registers32..35, rather than wrapping to channel0.

Six 2149H-3 SRAMs IC142..147 expose 24 bidirectional data bits. A9..6 are
strapped to ground, leaving64 effective addresses. ControllerRegisterFile is
currently a separately tested component, not instantiated in the running
controller. Its endWrite API represents a completed stable-address write
aperture, and read returns consensus across compatible addresses. Uncertain
writes merge held/written states at every candidate cell; no fabricated zeros.
No power-on or CRST clearing is applied. Moving-address write hazards, chip
setup/hold constraints, bus contention and floating-bus voltages are outside
this component kernel.

Remaining work: IC92 -MPWEN handshake, actual -RFWR gate/apertures, per-trajectory
SRAM and RFD bus ownership, RFD/control pipeline and ALU. CPU staging and the
SRAM component must not be mistaken for completed microcontroller memory
service. CPU WAIT remains an observer; direct CPU DRAM access is unchanged.

Validation: bash tools/build-native.sh; all nine test targets. New tests exercise
all8 decode aliases, byte ordering, unknown startup, IC118 polarity/address
wiring, all729 partial address patterns vs independent enumeration, uncertain
write preservation/isolation, and real Z80 OUT deassertion (one latch per
held strobe). Original firmware probes use the commands in CLOCKED_PROM.md.


## Scheduled write port, checkpoint0.12

Visually traced service7-21/PDF84 and ALU output enables7-22/PDF85.
IC92 first FF has D=/PRE=1, CLK=-PCWS3 and /CLR=-MPWEN. Its /Q
feeds second D; second /Q is +MPWFG, clocked on +C10MHz.1 with its
asynchronous controls high. Thus MPWFG samples OLD pending-Q. No CRST
clears either FF. Pending begins unknown; an actual CPU strobe establishes
it, subject to asynchronous clear priority.

- -MPWEN = NAND(CHSL3, STATE3, MPWFG).
- IC115 = CHSL3 AND -UCNT2 (the complemented counter-bit2 net).
- Middle IC67 = NAND(1, -MPWEN, IC115).
- -RFWR = NAND(-C10MHz.1, middle IC67, UCD02.1).

Each of16 startup trajectories now owns its own handshake and64x24 SRAM.
Falling source clocks open low-clock apertures; the next rising edge ends
writes from OLD control/address/shift/handshake before PROM/counters change.
No preceding falling edge means no invented first write. CPU strobes only
queue a request, not an immediate SRAM transaction. SRAM contents survive
CRST; scheduling follows reset changes to scan/shift signals.

CPU latches drive RFD when -MPWEN is low. ALU IC100..102 /OE is UCD06.1,
not -MPWEN. Its data latch is not implemented yet, so possible ALU drive
makes write data unknown, including CPU/ALU contention. With ALU certainly
disabled and CPU certainly enabled, staged24-bit data can become known SRAM
content. Other writes use unknown data. Partial-address and uncertain-enable
writes preserve only provably common bits. Unknown startup is never zero-filled.

RFD SRAM-read bus/pipeline and flag feedback remain disconnected. Existing
flag inputs to IC140 remain unknown in Machine. The next step is the full
RFD bus/IC148/150/152 pipeline and ALU; scheduled register writes alone do
not provide DRAM/sample service, CPU WAIT or audio. Gated-clock glitches,
propagation delays and moving-address write setup/hold are not simulated;
this is an ideal settled low-phase/end-aperture model.

New tests: IC92 asynchronous-clear priority/two-FF latency/rearm, all32 RFWR
truth-table rows, missing-ALU bus uncertainty, synthetic PROM ninth-slot SRAM
commit with no immediate OUT write, independent per-phase vs16-path SRAM
consensus. Runtime commands and source-only asset policy remain unchanged.

A real Z80 OUT program with a synthetic PROM also writes register37 through
Machine's high-byte strobe -> IC92 -> half-edge scheduler -> RFWR -> SRAM;
it verifies the connected path rather than invoking endWrite directly.
The supplied original PROMs still leave zero fully known cells at the boot
milestone, as expected while ALU/read-bus feedback is missing.
