# Programmed SIO receive and byte-mode DMA, checkpoint0.27

Current0.30: the CRC accumulator question is resolved on the model side by the
documented WR3 D3 start/restart (UM0081 printed250-251/283). The first block is
accepted, the loaded ZM325 diagnostic executes and a corrupted block is still
rejected. Read SIO_CRC_AUDIT.md. The0.27/0.28 results below are historical; the
CRC-retry failure they describe no longer occurs.

The optional read-only disk path is connected: rotor-phase FM transitions ->
shared active-low read pulses -> LS221/LS74 -> SIO-A bit clocks -> receive
FIFO/Ready -> firmware-programmed Z80DMA -> actual bus strobes -> strict DRAM.
Original main2.1/scanner3.1 with both PROMs writes the first3584-byte ZM325
payload to2800..35ff, all bytes known and byte-exact. This works under both
explicit FM profiles. It then sees a CRC error and retries. **No accepted
OS/diagnostic disk boot is claimed.**

## Evidence and sources

EII service PDF76/78 (7-13/7-15) visually checked. SIO-A Ready enters DMA RDY
alongside the other open-collector Ready sources; the present disk experiment
treats those other sources as released. DMA BUSRQ/PBRQ and BAI/PBAK connect
to the CPU bus-ownership interface. SIO -> DMA -> CTC -> PIO interrupt order
is separate from bus ownership. The CE/WAIT NAND mux selects chip enable
while the CPU owns the bus and PWAIT while DMA owns it.

Zilog UM008101-0601, SHA256b4efc81540c05990883cf4c7792c2a3d49fb7471bb5502931383ab55b4540886:
https://www.zilog.com/docs/z80/um0081.pdf . Figure109/PDF246 was visually
checked for receive/CRC paths; synchronous receive text printed249..252,
WR0/1/3/4/5/RR1 sections and DMA register/count/command chapters were read.
DMA Figure40/PDF113 and Figure44/PDF121 were visually checked. The WR0
direction label prints0 for both directions; current B->A usage is consistent
with the command bit and the real firmware's fixed SIO-to-variable-RAM setup.
Do not infer unimplemented counter/mode timing from that printing error.

Additional primary manufacturer reference: *1989 Zilog Z80 Family Data Book*,
https://bitsavers.org/components/zilog/z80/1989_Zilog_Z80_Family_Data_Book.pdf ,
SHA256844681b63ffc45bdad283067cacd6e8be0a40d8ed75580e56d8115ffe021de28.
SIO Q&A printed423..424 read; PDF429/printed424 visually checked. It describes
FIFO preservation on Hunt and reuse/retention of the CRC-delay/sync register.
The CRC accumulator's exact behavior at the EII's Hunt transition has NOT
been established by this passage. Do not import a Z85C30 SCC or MK68564 rule
as verified Z80ASIO/2 behavior just because its register names resemble it.

MAME's pinned BSD3 SIO implementation was inspected as a comparison at
8a18b751b388390db32812be43b8802de8cac839, not imported:
https://github.com/mamedev/mame/blob/8a18b751b388390db32812be43b8802de8cac839/src/devices/machine/z80sio.cpp .
Our receiver/DMA code is new; no MAME/HxC code or private ROM/data asset is
copied into main. The existing FM reference/input hashes are in FLOPPY_FORMAT.md.

## Receive subset and limitations

`SioSyncReceiver` implements the programmed Bisync16-bit-sync, x1,8-bit,
no-parity, CRC-16 case needed here. WR6/WR7 drive sliding sync comparison;
the initial sync pair does not enter the FIFO. Input delay1 plus buffer3
clocks gives4clock raw-last-bit-to-byte latency. Receive shift-register and
CRC-delay8+8 clocks put the CRC comparison20 clocks after the corresponding
last RxD bit. WR3 CRC enable sampled at character boundaries gates the prior
character as specified by the delayed calculation; it is not an immediate
sector checksum. Unit tests check both correct and corrupted CRCs at those
boundaries. Exact silicon phase/propagation and continuous RR1 behavior
between character boundaries remain uncalibrated; current RR1 CRC status is
observed at character transfers and retained with the queued byte.

CRC state starts unresolved. An explicit Reset Receive CRC command establishes
known zero; without it, a completed byte cannot report a fabricated valid
CRC. At the byte-only RR1 boundary an unresolved CRC is exposed as an error.
Error Reset can explicitly clear status; it does not make the checker state
known. Other serial startup registers use the documented nominal reset/hunt
convention, not an enumeration of physical power-up histories.

The3-byte FIFO overwrites its newest slot on overrun, retaining the two oldest
characters. Error bits follow queued data; overrun becomes sticky when read,
with Error Reset handling. First-character IRQ/rearm and all-character IRQ,
RX-before-external priority, vectorEC/EE versusEA, nested RETI ownership,
RR0 RxAvailable independent of frozen modem bits, data-port aliases and
receive Ready are implemented. Receiver disable/DCD AutoEnable returns to
Hunt while retaining queued bytes. Hunt retains the old sync/CRC-delay
window; disable/re-enable clears the window. The CRC accumulator is currently
preserved on Hunt, which is explicitly part of the unresolved failure below.

Ready is active-low in the WR1E0 receive-Ready configuration. The API takes
bus-selection inhibit; DMA requests are evaluated with the CPU bus idle, and
are not retracted midway through a source cycle. System-clock Ready latency,
CE/IORQ subcycle waveform, Wait mode and electrical open-collector timing are
not calibrated. Other SIO modes, TX, SDLC, other character sizes and parity
are unsupported and counted without fabricated receive data.

`DiskReadPath` is absent by default. Its constructor requires monostable and
read-pulse widths; mount requires an image and explicit FM profile. Each
CPU boundary advances5 actual20MHz edges, derives rotational phase from the
drive, and wires both selected read-pulse lines together. It caches the
selected cylinder/side format; it does not copy decoded bytes to the FIFO.
Read permission requires selected, ready, valid head position, settled head/
power and inactive write gate. Side-select now has the reference100us read
guard (manufacturer SA4651.6.6); high selects side0, low selects side1.
The constructor's optional initial tick is an explicit known-idle laboratory
epoch; skipping CPU boundaries with an active path is rejected. Selection/
side/seek do not reset separator/rotor history. Physical read jitter, mixed
drive contention, analog thresholds and arbitrary startup remain uncalibrated.

## DMA subset and bus adapter

`DiskDma` decodes WR0..WR6/follow bytes needed by sequential byte transfers:
starting addresses, length, port type/increment/fixed source, cycle lengths,
Ready polarity, Load/Continue/Enable/Disable, EOB interrupts, vector/ACK/RETI.
Unsupported search/burst/continuous/restart/Ready-interrupt/pulse/early-timing
or fixed-destination quirks do not silently transfer data. DMA status/read
sequence registers are not implemented; unsupported reads returnFF/count.
Configuration/address/length bytes must have been programmed, rather than
using an invented power-up block. The EII programs only the low byte of its
fixed I/O source address; its unused upper byte is represented as0 at the
bus boundary, whose high I/O address bits this board does not decode.

Sequential countN transfersN+1bytes; zero has the documented65537-byte
special case. Source counter advances after reads; destination is loaded on
the first destination cycle and subsequently advances before writes. End
counter and Continue preserve the documented last-destination address.
Each read and write has its own clocked bus cycle, plus a release interval;
no callback performs an atomic block copy. EOB IRQ follows the last write.

The vendored Z80 lacks BUSREQ/BUSACK. A new explicit Machine adapter saves
CPU pins and grants only at an idle memory/I/O boundary with no pending
write strobe. It freezes the CPU instruction state, drives DMA address/data/
MREQ/IORQ/RD/WR and continues scanner, CTC, PIO, controller and disk clocks.
On release it restores CPU pins and current interrupt level. This is a
conservative bus-boundary adapter, not calibrated physical BUSACK T states.

DMA uses bank registers0/1; CPU still uses2/3. Local ROM/SRAM retain their
existing overlay behavior. DRAM transfers use the same PROM/RAS/CAS/IC103/
WAIT consensus backend as CPU requests with DMA-owned bus pins/address.
The DMA cycle advances only after controller service/known read/WAIT release;
one held write commits once. DMA DRAM W/R/WAIT counters are separate from
CPU counters. Unknown reads stall. CPU and DMA cannot access the bus in
parallel. The input/request guard is conservative, not a measured delay fit.

## Real-firmware result and remaining failure

```sh
build/floppy_probe ../roms/main.bin ../roms/scanner.bin ../roms/ic135.bin ../roms/ic137.bin 3600000 37 /tmp/eii-sio-dma.csv ../inputs/zm325.emuiifd hxc2026
```

Repeat with `report2010`. Required initial conditions remain drive0cylinder37,
phase400000CPUticks, protected diagnostic media; drive1absent. Pulse width87
source edges, read pulse8edges, explicit idle separator and nominal FM are
experiments. Legacy8-argument invocation remains sensor-only.

Both3.6M runs:37steps to0,3SIO-A status ACKs,2DMA EOB ACKs,3588 DMA transfers
(4header +3584payload),3584 strict DRAM writes/17920 DMA wait clocks. Original
CPU service remains7writes/12reads/95WAIT. All3584 bytes at2800..35ff are known
and equal the original first payload. Source bus bytes were independently
compared to the image. No block was injected into RAM by the probe.

The header CRC read at tick2849697 returnsRR1=01. At WR3D9 Hunt re-entry,
tick2849910, the0.27/0.28 model carried5000hex from gap-bit processing and the
payload end read wasRR1=41, so the real firmware took its CRC-retry branch. That
was the historical behaviour. The0.30 model restarts the checker on the D3
write, the payload is accepted (RR1=01, CRC0000) and the loaded diagnostic
executes; see SIO_CRC_AUDIT.md and `validation/sio-crc-restart-*.txt`. Both
image profiles still produce identical payload bytes, so the4-bit gap
discrepancy is not silently tuned to fix anything.

Do not force RR1 success, change clocks/gaps to hide an error, patch ROM or
claim an accepted OS load beyond what the run shows. What remains open is the
exact silicon boundary at which the restart applies and whether the identified
EII part matches UM0081; the documented hardware test in SIO_CRC_AUDIT.md is
still unperformed.

Evidence: `validation/sio-dma-zm325-{hxc,report}-3m6.txt` (historical0.27),
`sio-crc-restart-{3m6,main21-4m}.txt` (0.30) and `sio-dma-first-block.json`;
raw private data-bearing traces stay outside main. The latter records byte
equality, hashes and selected control/CRC events without payload bytes. Tests
and final corrections: VALIDATION.md.
