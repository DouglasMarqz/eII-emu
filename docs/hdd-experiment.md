# Firmware-facing HDD experiment (local, 2026-10-23)

Base: Tuth/eii-emulator, perf/cpu-placement-072,
6dc1133ce9cf60ad70e97534320dad5a4d0ec473. This change is not committed or pushed.

The optional Machine::installHardDisk installs an in-memory logical-sector target
on ports 3C–3F. Without installation, the existing floppy SIO route remains.
The main23.bin ROM issues the commands itself. DMA uses the existing CPU bus
ownership and PROM-controlled DRAM path, including WAIT; no firmware, RAM, CPU
PC, or LCD state is patched by the probe.

3C is command/data (including DMA); 3D is phase/status; 3E selects the target.
3D reset and 3F DMA-arm/ack behavior are isolated functional hypotheses, not
verified AE343 netlist behavior. REQ has a nominal two-CPU-clock response delay.
Continuous I/O DMA and status reads are opt-in when the HDD is installed.
READY is checked at byte boundaries while continuous DMA retains bus ownership.

Implemented commands include 6/10-byte reads/writes, write-and-verify data
transfer, sense, mode-select, format with byte-from-index defect lists, and
capacity. The backing image is logical sectors: no physical MFM defects,
ECC, rotation, seek timing or ACB4000 MCU execution is modeled. WRITE VERIFY
commits bytes but does not model ECC/read-after-write hardware verification.
Unknown commands fail with CHECK CONDITION, rather than synthetic success.
The command history is a diagnostic experiment and is not yet bounded for a
long-running production UI.

Private inputs (excluded from source/patch delivery):
- main23.bin (8 KiB): f559c0be681fd50eea21198eca8fc4b436822715a283c3926357d0e893f844e1
- EMUIIOS31HD.E2O: 8c845edc0d97fc9c797d272c2f9100d55f246dc75d71dd1b5ab1da04e2b81b4f
- scanner.bin and IC135/137 PROMs from the supplied ROM archive.
- Experimental floppy: 573440 zero bytes with the complete E2O copied at offset
  zero. This is derived test media, not a verified original floppy image.
- Disposable HDD: 613*4*18*512 bytes; the OS MODE SELECT specifies 612 cylinders,
  four heads and 512-byte sectors. The extra cylinder is retained in backing.

Sources: supplied Adaptec ACB4000 manual, printed 5-9..5-12 (FORMAT), REQUEST
SENSE and completion status chapters; supplied DREM E-Mu Easy to be Hard V2
manual for geometry and AE343 overview. Zilog UM0081 for WR6 BF/87 and continuous
DMA READY behavior. The S100 host example is not used as an AE343 schematic.

Build from a full source checkout with C++20:

```sh
cmake -S . -B build-hdd -DCMAKE_BUILD_TYPE=Release
cmake --build build-hdd --target hdd_probe hard_disk_tests disk_dma_tests
ctest --test-dir build-hdd -R '^(hard_disk_tests|disk_dma_tests)$' --output-on-failure
```

The local source snapshot did not include every unrelated CMake input; validation
here compiled chips.cpp, machine.cpp and vendor/ssm2045-chip/src/chip.cpp directly
with -std=c++20 -O2 -DNDEBUG -DEII_DIAGNOSTICS=0 -pthread, then linked the probes.
The HDD target tests also pass ASan/UBSan with ASAN_OPTIONS=detect_leaks=0;
LeakSanitizer cannot inspect the process under this sandbox's ptrace setup.

Probe (never give an original image as output):

```sh
hdd_probe ROMDIR HD-OS.emuiifd 125000000 - fresh-hdd.img > format.txt
# Add input-hdd and a distinct output-hdd to reopen and save the in-memory result.
# With no input HDD, starts a disposable zero-filled image and drives SPECIAL 22.
# With an input HDD, drives DISK 2, bank 02. A final 'save' argument drives DISK 7.
```

Current evidence and remaining work are recorded in state.json / memory.md.
Windows GUI/VST3 mounting, durable save UX, native runtime validation, physical
board timing and 1 MiB expansion routing are not established by this experiment.

Owner manual Hard Disk Supplement p153 (E-mu): DISK7 saves, DISK2 loads;
loading a formatted but unsaved bank can produce invalid contents. Reference:
https://www.theemus.com/documentation/emulatorii/Emulator_II_Manual.pdf

## Final observed result

- FORMAT + catalog access: pass.
- DISK7, bank02, YES: pass,489472 bytes written, no invalid access.
- Cold boot with no floppy: OS and saved bank02 load from HDD.
- LCD at24.9m ticks: P01 NULL PRESET. Read514560 bytes, invalid0.
-950 changed sectors after save all lie within956 CDB-addressed sectors.
- Cold-boot output image exactly equals the input saved image.
- Final core rebuilt; disk_dma_tests, hard_disk_tests and ASan/UBSan target tests pass.

Reproduce the last two stages using distinct output paths:

```sh
hdd_probe ROMDIR HD-OS.emuiifd 95000000 fresh-hdd.img saved02.img save > save.txt
hdd_probe ROMDIR none 35000000 saved02.img cold02.img boot > cold.txt
python3 tools/validate_hdd_checkpoint.py format.txt save.txt cold.txt fresh-hdd.img saved02.img cold02.img
```

The format evidence was captured before the MODE SELECT cylinder-offset
correction; that parse is covered by the final target test. Save/reboot were
run after correction, and the final cold boot uses the final rebuilt core.
The package excludes private ROMs and all disk images.

Latest standalone UI integration supersedes the pending-GUI statements above;
see hdd-win64.md. VST3 does not exist. Native Windows runtime is still pending.
