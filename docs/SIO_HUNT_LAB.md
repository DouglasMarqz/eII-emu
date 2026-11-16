> Historical0.29 experiment. The documented WR3 D3 restart from PR1 is now integrated; use docs/SIO_CRC_AUDIT.md and docs/DRAM_POWER_ON.md for current behavior. This source-mutating Hunt-reset launcher is retained only for historical reproduction and may reject changed source anchors.

# Isolated Hunt-reset hypothesis laboratory, checkpoint0.29

The real ZM325 diagnostic program executes **conditionally**, in an isolated
source copy that assumes CRC initialization on Hunt. Both nominal FM profiles
reach the firmware-driven LCD `Testing Memory` / `Bank=A  Segmnt=1`. The default
receiver still preserves CRC across Hunt and still retries. This experiment
does not establish the missing NMOS Z80ASIO/2 silicon behavior and is not a
verified system-disk boot or completed RAM test.

No new exact-chip reset evidence was found in the documentation search. The
firmware command sequence remains one explicit ResetRxCRC at setup, then
Hunt between the header and data fields. Related SCC/MK68564 descriptions
remain leads, not substituted proof. The independent0.28 audit and physical
test proposal in SIO_CRC_AUDIT.md remain relevant.

## Reproduce

```sh
python3 tools/sio_hunt_lab.py ../roms/main.bin ../roms/scanner.bin ../roms/ic135.bin ../roms/ic137.bin ../inputs/zm325.emuiifd --profile hxc2026 --policy retain
python3 tools/sio_hunt_lab.py ../roms/main.bin ../roms/scanner.bin ../roms/ic135.bin ../roms/ic137.bin ../inputs/zm325.emuiifd --profile hxc2026 --policy reset-on-hunt-hypothesis
python3 tools/sio_hunt_lab.py ../roms/main.bin ../roms/scanner.bin ../roms/ic135.bin ../roms/ic137.bin ../inputs/zm325.emuiifd --profile report2010 --policy reset-on-hunt-hypothesis
python3 tools/sio_hunt_lab.py ../roms/main.bin ../roms/scanner.bin ../roms/ic135.bin ../roms/ic137.bin ../inputs/zm325.emuiifd --profile hxc2026 --policy reset-on-hunt-hypothesis --corrupt-first-data-bit
```

Requires Python3.10+ and a C++20 compiler (`CXX`, default g++). The launcher
validates the exact original main2.1/scanner3.1/PROM/ZM325 hashes before
compiling. It copies source into a temporary directory and compiles its
laboratory harness against that copy, with the existing vendor headers.
The temporary executable and sources are removed after the run. There is
no experimental switch in Machine/DiskSio/SioSyncReceiver or the normal GUI.

For the hypothesis, the isolated enterHunt function calls resetCrc before
its existing logic. This also hypothesizes initialization when the receiver
is disabled or enabled into Hunt. These are assumptions, not recovered
transistor behavior. The accumulated CRC is then calculated normally; RR1
is never overridden. Neither the ROM nor image is patched, and DMA uses
the existing strict PROM/RAS/CAS/WAIT path without a decoded-sector RAM copy.

The negative control flips exactly the first payload FM data slot on track/
side0 **after** formatting, preserving the clock slots and original computed
CRC. This mutation is also confined to the isolated copy. It therefore tests
a real received-data/checksum disagreement rather than regenerating a valid
CRC for a changed payload image. The underlying image remains unchanged.

Every4M-clock run uses drive0 initially at cylinder37, phase400000CPUticks,
protected diagnostic media, drive1absent, nominal87-edge LS221 and8-edge
read-pulse widths. The output is JSON metadata only: original asset hashes,
all core/vendor-header/harness source hashes, isolated mutation hashes,
bounded RR1 observations, memory equality counts, fetch/cycle counts and LCD.
Sources and original assets are hashed again afterward and must be unchanged.
No raw payload, ROM bytes or data-bearing trace is emitted.

## Results

All four launched trials returned0 and `expected_trial_result_pass=true`.
The following table refers to the first **payload** RR1 read, before the
loaded program can modify its own memory:

| Isolated policy / stimulus | Profile | RR1 / CRC (hex) | Known/equal RAM bytes | Loaded-code fetches at4M | LCD at4M |
| --- | --- | --- | --- | ---: | --- |
| Retain, intact | hxc2026 | 41 / B04A | 3584 / 3584 | 0 | Boot in Progress |
| Reset hypothesis, intact | hxc2026 | 01 / 0000 | 3584 / 3584 | 29033 | Testing Memory |
| Reset hypothesis, intact | report2010 | 01 / 0000 | 3584 / 3584 | 29026 | Testing Memory |
| Reset hypothesis, one bad data bit | hxc2026 | 41 / 3F1A | 3584 / 3583 | 0 | Boot in Progress |

The good hypothesis trials retain3588 DMA transfers,3584 strict writes and
17920 DMA WAIT clocks. The RAM snapshot is taken inside the real I/O trace
callback for the RR1 read, before the Z80 executes its CRC decision branch.
This avoids mistaking the diagnostic's later self/workspace modification
for a transfer error: the earlier exploratory end-of-run comparison showed
3583 equal bytes after the program had already started.

The two successful conditional runs execute actual fetched instructions in
2800..35ff, send40 main-to-scanner PIO bytes, and the scanner firmware drives
the LCD. At4M, mainPC283c and the second LCD line is `Bank=A  Segmnt=1`.
HxC CPU DRAM W/R/WAIT is8295/33268/207819; report8293/33260/207769. These
are new real CPU transactions through the existing strict backend under the
experiment's assumptions; they do not certify the full512KB memory test.

Both the unchanged retain trial and the corrupted hypothesis trial reject
the first payload and retry, with zero loaded-code fetches. At4M they have
already started another read, so cumulative DMA counts5290/writes5282 are
larger than the3588/3584 counts at the first payload CRC observation.

## Interpretation and boundary

The conditional result demonstrates that the received block is executable
diagnostic code and that the following CPU/DRAM/PIO/scanner/LCD path can
operate. The corruption control demonstrates that the hypothesized reset
does not simply force CRC success. It strengthens the initialization lead.
It **does not prove** that this is the exact hardware mechanism: successful
firmware execution in a modified model cannot validate the modification.

The default core and its CRC-known guard remain byte-for-byte unchanged from
0.28. Physical initialization evidence or a controlled identified-chip test
is still needed before promoting a reset behavior. No normal OS image was
recovered here, no default-model boot or complete diagnostic/RAM pass is
claimed, and GUI/audio/Windows runtime remain unchanged.

Evidence: validation/sio-hunt-lab-{retain,reset-hxc,reset-report,corrupt}.json.
Core23-target suite and SIO sanitizers remain the0.28 evidence; new launcher
guards plus existing native SIO/DMA targets were checked in0.29. No CMake/
Windows execution or new sanitizer run is claimed for this laboratory tool.
