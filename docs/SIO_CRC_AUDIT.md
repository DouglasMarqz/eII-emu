# SIO Hunt retention, CRC restart and first accepted block, checkpoint 0.30

The0.28 audit isolated the CRC failure to a carried seed. The0.30 work found the
documented mechanism that clears it: the Zilog manual states that setting the
Receive CRC Enable bit (WR3, D3) **starts or restarts** the CRC calculation, and
the EII firmware re-issues WR3 `D9h` (Hunt plus D3) before every Hunt. The model
now implements that documented effect. With unchanged firmware, unchanged
prompts and no RAM injection, the first3584-byte block is accepted and the
loaded ZM325 diagnostic executes. A deliberately corrupted block is still
rejected.

This is a documented register effect, not a guessed "reset on Hunt". The
silicon-level question that remains open is narrower: whether the real chip
restarts at the same character boundary and whether the EII's identified part
behaves exactly as UM0081 describes. The discriminating hardware test below is
still unperformed.

## Documented correction (0.28, retained)

Zilog's1989 Family Data Book, printed424/PDF429, states that the sync register
shares the CRC-delay register, whose contents survive Enter Hunt. The same
book's printed423/PDF428 describes the delayed CRC-status observation. Both
pages and UM0081 Figure109/PDF246 were visually checked.

In0.27, Enter Hunt while already hunting rebuilt the window from cleared
registers, losing a partial sync pair. Acquisition also discarded the matched
register contents. The receiver now keeps the active window on repeated Hunt
and retains the matched contents. Disable/re-enable remains the distinct
clearing operation for that window. This was never a statement about the CRC
accumulator.

## The decisive text (0.30)

UM0081, printed250-251 (PDF270-271), "CRC Error Checking and Termination":

> The Receive CRC Enable bit (WR3, D3) must be set/reset by the program before
> the next character is transferred from the receive shift register to the
> receive buffer register... If CRC is enabled before the next character is
> transferred, CRC is calculated on the transferred character. If CRC is
> disabled before the time of the next transfer, calculation proceeds on the
> word in progress, but the word just transferred to the buffer is not
> included.

UM0081, printed283 (PDF303), "Receiver CRC Enable (D3)":

> If this bit is set, CRC calculation starts or restarts at the beginning of
> the last character transferred from the receive shift register to the buffer
> stack. This start or restart occurs regardless of the number of characters in
> the stack.

Both pages were re-extracted and the printed283 page was rendered and visually
checked, not only read as text. The first passage gives the timing window; the
second gives the restart semantics. MAME's BSD-licensed SIO keeps D3 as a pure
level gate and never restarts the accumulator; that implementation was
inspected as a comparison and not imported.

## What the real firmware actually does

`tools/sio_seq_probe.cpp` traces the real CPU/DMA bus against fixed assets and
prints every SIO control/data access with tick, PC, register and live CRC.

```
TICK 2843332 ch0 CMD=43 (RESET-RX-CRC)
TICK 2843353 ch0 WR3=d9
TICK 2848820 ch0 SYNC-ACQUIRED matches=1
TICK 2849697 ch0 RR1=1 ...            header CRC accepted
TICK 2849892 ch0 CMD=3                pointer 3, no CRC command
TICK 2849910 ch0 WR3=d9 ... crc=5000  same value rewritten: Hunt + D3
TICK 2852403 ch0 SYNC-ACQUIRED matches=2
```

The firmware never issues a WR0 Reset Rx CRC between the header and the data
field. Its only CRC-related act there is re-writing WR3 `D9h`. Under a pure
level gate the gap characters stay in the accumulator and the data field can
never pass, no matter how many retries occur. Under the documented restart the
accumulator is discarded and the data field starts clean.

## Model change

`SioSyncReceiver` gains a pending restart. `DiskSio` queues it whenever WR3 is
written with D3 set. It is applied at the next character boundary, or at Hunt
entry when no boundary can follow. The accumulator is otherwise retained, and
nothing in the model forces a successful CRC: an uninitialised checker still
reports an error, and corrupted data still fails.

`"Restart at the beginning of the last character transferred"` is implemented
as: the pending character is discarded, the next transferred character starts
the new calculation. The alternative reading (re-including the transferred
character) would keep a gap byte in the payload CRC and is not adopted.

## Measured result

`build/floppy_probe` with original main2.1/scanner3.1, both PROMs, drive0
cylinder37/phase400000, ZM325 image and profile `hxc2026`:

| Clock | Result |
| --- | --- |
| 3.6M | RR1=`01`, CRC=`0000`, 3588 DMA transfers, 3584 strict DRAM writes, LCD cleared by loaded code |
| 4.0M | LCD `Testing Memory` / `Bank=A  Segmnt=1`; CPU DRAM 33268 reads/8295 writes/207819 WAIT holds; 40 main-to-scanner PIO transfers |

The3584 payload bytes are byte-exact on arrival; the later `3583/3584` probe
comparison is measured after the diagnostic has begun modifying its own
workspace. Evidence: `validation/sio-crc-restart-{3m6,main21-4m}.txt`.

Negative control, `tools/sio_hunt_lab.py --policy retain --corrupt-first-data-bit`
(one generated FM data slot flipped, generated CRC and clock slots retained):

```
LoadedCodeFetches 0
Lcd0 ***Emulator 2***   Lcd1 Boot in Progress
DmaTransfers 5290       CpuDram 7 writes/12 reads/95 WAIT
```

So the accepted block is not an artefact of a disabled check: a single flipped
data bit returns the machine to the boot-time retry loop.

## Regression test

`tests/sio_receive_tests.cpp` now asserts:

- a receiver configured with WR3 `D1h` (D3 clear) and no WR0 CRC command keeps
  the checker unknown and exposes an error at the RR1 boundary;
- setting D3 makes the checker known-zero (documented start/restart);
- the EII sequence (sync, four in-frame gap characters that pollute the
  checker, then WR3 `D9h`, then a fresh sync and a valid data field) reports a
  valid CRC, while a corrupted check byte in the same sequence reports an error.

Verified both ways: the suite passes against the current sources and fails
against `git show HEAD:` sources, where the first new assertion reports
`WR3 D3 starts the checker`.

## Still unproven

- The exact character boundary at which real silicon applies the restart.
- Whether the identified EII SIO/2 revision matches UM0081 rather than a
  related variant.
- Original magnetic gaps/CRCs (the payload image synthesises CRCs), physical
  timing, and anything beyond the first accepted block and started RAM test.
- The full512KB memory test has not been observed to completion, and no OS
  system disk has been booted.

## Discriminating hardware experiment, not yet performed

Use an identified NMOS Z80ASIO/2 or proven equivalent on a timed bus jig;
record the chip marking, bus clock, RxD/RxC, register writes, reads and FIFO
service. A software model of another chip cannot substitute for this test.

1. Initialize x1/Bisync16/8-bit/no-parity/CRC16, FA96, receiver enabled;
   explicitly Reset RxCRC once. Drain every received character.
2. Send sync and a deliberately bad block, followed by enough FF characters
   to leave a nonzero checker state at a controlled byte boundary. Keep the
   same stimulus and timing in every trial.
3. Write WR3 again with D3 set and Hunt, with **no** CRC-reset command. Send a
   new sync, a known payload and its independently computed low/high CRC, plus
   two zero pads.
4. Follow the manufacturer's CRC-read sequence: after the first pad arrives,
   clear CRC Enable without entering Hunt; when the second pad arrives, read
   RR1 before draining it. Respect the20-clock raw-last-CRC-bit delay.
5. Repeat with an explicit Reset RxCRC during Hunt as the positive control, and
   with one data bit flipped but unchanged check bytes as the negative control.
   Repeat with receiver disable/re-enable as a separate trial.

The prediction under the adopted model is: step3 accepts the block (step5
positive control also accepts, negative control fails). If silicon instead
retains the error, the restart reading of printed283 is wrong and another timing
or routing explanation is required.

## Validation

See `validation/sio-crc-restart-*.txt`, `validation/tests-sio-restart.txt` and
`VALIDATION.md`. No Windows-specific claim beyond the native Windows run
recorded there; no hardware measurement, host audio or complete disk boot.


Current0.32: the documented receive restart and tests are selectively integrated;
see DRAM_POWER_ON.md for OS3.0 forward-progress evidence. The Windows/full-suite
results above are historical PR1 reports, not a new run on the current host.
