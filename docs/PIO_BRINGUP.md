# PIO bring-up: CTC correction and clocked mode-2 link

The CTC-only boundary below is historical checkpoint 0.2. Checkpoint 0.3
implements the link described in the final section and reaches `Insert Diskette`.

## Confirmed change

The old Machine::step cleared Z80_INT before each z80ctc_tick. The vendored
CTC core changes NEEDED to REQUESTED and expects INT to stay asserted until
the CPU acknowledges it. Clearing it every tick loses a request arriving
while the CPU has interrupts disabled. The integration now preserves INT,
following the daisy-chain example in vendor/chips/z80.h. Vendor code is unchanged.

The regression executes a synthetic Z80 program that sets up a timer, reads
its counter, delays EI, and runs an IM2 handler which returns with RETI.
Restoring the old INT clearing fails `CTC IRQ held while CPU interrupts disabled`.
An initial repeated-register-write hypothesis was not reproduced; no such fix
is included. CTC access counters and read-trace data are now also recorded.

Validation commands (from the repository root):

```sh
bash tools/build-native.sh > validation/tests-ctc.txt
g++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc -Ivendor/chips src/chips.cpp src/machine.cpp tests/digital_tests.cpp -o build/digital_tests_san
ASAN_OPTIONS=detect_leaks=0 build/digital_tests_san > validation/sanitizer-ctc.txt
build/pio_probe ../roms/main.bin ../roms/scanner.bin 40000000 > validation/pio-ctc-40m.csv 2> validation/pio-ctc-40m.txt
build/eii_probe ../roms/diag.bin ../roms/scanner.bin 4000000 /tmp/eii-diag-trace.csv '' '' 'HE TE' > validation/diag-ctc-testhelp.txt
```

ROM paths above are local inputs outside the source tree. For the negative
control, a temporary copy of machine.cpp reinstated `ctcPins&=~Z80_INT;`
immediately before `ctcPins|=Z80_IEIO;`; compiling the same digital_tests.cpp
against that copy failed the held-request assertion. That copy is not shipped.

## Reproduce the communication boundary

Build: `bash tools/build-native.sh`.

Run `build/pio_probe /path/to/main.bin /path/to/scanner.bin 40000000`
with stdout redirected to a CSV and stderr to a text report.
Main/scanner SHA-256 identities are in tools/rom_catalog.json. This run uses
main.bin (2.1) and scanner.bin (3.1); original ROM instructions are unchanged.

The recorded run has 17 A/control events, none omitted. At CPU clock 18876512
main writes 97h to low port 30h, post-I/O PC 0b64h; at 18876534 it writes
83h to control port 31h. No subsequent scanner A read occurs before 40M clocks.
The banner remains `***Emulator 2***` / `Boot in Progress`.
This is a traced transmit attempt, not a successful transfer or OS boot.

The general eii_probe stops recording after 20000 I/O events. Scanner traffic
fills that budget before this byte, so a missing byte in that trace is not
evidence that main never transmitted. pio_probe omits port-B data traffic,
limits relevant events to 4096 and reports any omissions.

## Schematic evidence and next implementation

Service-manual PDF pages 72 and 77 (printed 7-9 and 7-14) were rendered and
visually inspected. Scanner IC46 and main IC24 share the A data bus. Scanner
ARDY/ASTB are +SDOR.D/-SDOS.D; BRDY/BSTB are +SDIR.D/-SDIS.D.
Main-side IC23 is a pair of clocked 74LS112 flip-flops, with IC2/IC25 gating
and the +C2MHz.D clock. Do not replace these strobes with an assumed
instantaneous cross-copy or infer full transfer timing from the labels alone.

Both ROMs program A to mode 2 and B to mode 3. The current model copies a
byte to peer A input, but only sets peer A pending when its A interrupt is
enabled; it has no PIO interrupt arbitration/acknowledgement path at all.
Mode-2 input/output strobes and both interrupt vectors must be resolved
against the chip specification, IC23 logic and firmware ISR paths before
changing this provisional model. A ready bit is not proof of an accepted byte.

Next: model the physical handshakes and mode-2 A/B interrupt roles; integrate
PIO into the CTC-to-PIO daisy chain, including held requests, vector ACK and
RETI priority. Retain the delayed-EI CTC test when adding a second IRQ source.

## Recovery limits

Source was restored from e642a24a0dfb705fca8de09459979da2adc848b1.
The live branch listing returned only main; private/checkpoint-0.1 returned
404. Its documented binary ZIP and asset backup are not independently verified.
Original service-manual and firmware ZIPs were recovered from user uploads;
no runtime assets are added to main. CMake is absent locally, so only the
shell/GCC build was run. No new Windows binary or Windows runtime claim.

## Checkpoint 0.3: mode-2 transfer and IRQs

Visual tracing on 7-14 resolves the two IC23 flip-flops as follows:

| IC23 half | J input | K input | Active-low strobe |
| --- | --- | --- | --- |
| Upper | !main.ARDY & main.BRDY & scanner.ARDY | Q | /SDOS: scanner ASTB, main BSTB |
| Lower | main.ARDY & scanner.BRDY | Q | /SDIS: main ASTB, scanner BSTB |

Both sample the falling +C2MHz.D edge. With K=Q, Q(next)=J & !Q(previous).
The scheduler represents that 2MHz clock with a falling edge every second
4MHz CPU tick; reset initializes Q=0 (strobe inactive). These equations
give main's output priority when both endpoints request a transfer.

Original schematic page 7-9 supplies the scanner ready/strobe labels. PIO
mode-2 semantics were cross-checked against Zilog UM008101-0601, Parallel
Input/Output chapter: pin descriptions, programming and mode-2 timing:
https://www.zilog.com/docs/z80/um0081.pdf . A handshake serves output; B
handshake serves input. The two directions use A and B interrupt vectors,
respectively, despite all bytes travelling on the A data bus.

CPU OUT now loads only the source register. An active strobe loads the peer
input register; its rising edge completes the source ACK and destination
input request. CPU IN rearms the input handshake. Pending requests survive
disabled interrupt enables. Port-B mode-3 writes do not alter its mode-2
handshake role. Direction mask, interrupt mask and control are separate.

CTC requests are retained using the CTC's own previous output pin. PIO IRQs
are combined only after ticking CTC, preventing a PIO request from becoming
a persistent false CTC request. CTC is upstream of PIO; A is above B.
The highest-priority enabled pending source supplies the IM2 vector, and
RETI releases the highest-priority device in service.

Validation:

- Synthetic strobe edges, backpressure, both transfer directions and main
  priority under simultaneous requests: PASS.
- Synthetic delayed enable, A/B vectors, A-over-B priority, upstream CTC
  priority, RETI release and independent IRQ deassertion: PASS.
- Native build and ASan/UBSan: PASS (validation/tests-pio.txt and sanitizer-pio.txt).
- Main 2.1 + scanner 3.1: 32 main-to-scanner transfers, 32 main A IRQ ACKs,
  32 scanner B IRQ ACKs; real LCD `Insert Diskette` at 40M clocks.
- Main 2.1 + scanner 2.1: same counts and disk prompt at 40M clocks.
- Main 2.3 + scanner 3.1: no PIO transfer within 40M clocks; main polls low
  I/O port 3Dh and LCD remains at the initial banner. That path is unresolved.
- Diagnostic main + scanner 3.1: `Static RAM OK` and `HE TE` test help retained.

Reproduce with the earlier commands, replacing log names by `*-pio*` or
`pio-mode2-40m.*`. Additional variant commands:

```sh
build/eii_probe ../roms/main23.bin ../roms/scanner.bin 40000000 > validation/pio-main23-scan31.txt
build/eii_probe ../roms/main.bin ../roms/scanner21.bin 40000000 > validation/pio-main21-scan21.txt
```

All ROM identities remain in tools/rom_catalog.json. The reverse direction
is verified by synthetic traffic, not by a claimed real-ROM reverse exchange.
This is a board-specific mode-2 timing model: CPU I/O-to-ready propagation,
input setup/hold and subcycle timing are not calibrated. Mode-0/1 external
strobes and mode-3 bit-match IRQ generation are not implemented. No complete
OS boot, disk transfer or audio exists yet. Next: identify supplied disk layout
and implement authentic floppy SIO/DMA boot transport.
