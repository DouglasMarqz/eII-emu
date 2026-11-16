# 0.43 continuous memory DMA

The reported Play failure includes loss of panel response and sound. The0.42
capture at751240000 CPU ticks has production invalid0, no active CPU DRAM
request, main PC47d8 and PIO B in service, and DMA enabled1/supported0. All ACV
holds are0V. One snapshot cannot establish a persistent PC loop or identify
which supported() condition failed; it does not prove an audio driver defect.

A native boot with the supplied original bank identifies the OS region47c3..
4817 as DMA setup code. Its WR4 template contains BD (continuous mode), with
memory ports. The previous implementation rejected every non-byte WR4 mode.
This is a confirmed missing firmware-used operation, not yet proof that it
caused this particular snapshot or the complete user freeze.

Zilog UM008101-0601 (https://www.zilog.com/docs/z80/um0081.pdf), printed150
(continuous bus ownership) and173 Figure80 (WR4), checked against the rendered
page. WR4 D6..5=01 selects continuous. The memory-only implementation retains
bus ownership between read/write byte pairs until EOB; scanner/controller/IRQ
clocks continue and all bytes still traverse the existing bus/DRAM WAIT path.
There is no atomic block copy and no firmware patch.

Byte mode is unchanged. Burst, continuous I/O, search, auto-restart, pulse/Ready
interrupts, read registers and physical BUSACK/subcycle calibration remain
unsupported. The same count, address, port direction, EOB and IRQ machinery is
used. The first bus request still requires Ready or Force Ready; continuous
memory transfer then retains the bus through the block.

Tests extend disk_dma_tests with a WR4=BD forced-Ready memory copy, data/pin
checks, source/destination WAIT, one bus grant and EOB. Machine integration
checks16 strict DRAM reads plus16 writes in each of8 DMA banks, CPU PC held
while granted, continued scanner clocks and real Z80 IM2 EOB/RETI completion.
Existing byte-mode, zero count, wrap and disk-stream tests remain present.

Manual Capture state now includes IX/IFF, CTC interrupt state and DMA WR0..6,
timing, interrupt control, start/current addresses, count, loaded/forced/EOB,
grant and interrupt state. This remains a one-shot report in diagnostics-off
Release. Windows application title is0.43.

A fresh user reproduction is needed to establish whether the Play/panel/audio
failure is resolved. If it recurs, take two manual captures several seconds
apart before rebooting; the expanded DMA record identifies the rejected setup
and the two captures distinguish a persistent wait from a transient ISR.

Validation: Release DMA tests PASS (both port directions, descending addresses,
WAIT, all8 banks/all16 reference phases and EOB IRQ), original byte tests PASS,
full-core ASan/UBSan DMA tests PASS (leak detection off), digital CPU/IRQ and
Release worker/manual capture PASS. The current original-bank reference vs
production probe passes through224M CPU ticks, including nonzero PCM MIDI,
Play/Stop/Filter/Select; baseline mono PCM hashes remain unchanged. Win64
LLVM-MinGW20261006 O3/LTO/NDEBUG/DIAG0 crossbuild has zero warnings.
No Windows runtime or new CPU usage percentage has been measured here.
