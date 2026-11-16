# Emulator II ROM sets: inventory, what we boot, target configuration

No ROM data is in Git. This file records identities only. Sources:
* `Z:\EII\roms`: the owner's folder and its `ROMS.zip`.
* `F:\Claude\_assets\roms_owner\EII_plus_not_signed_IC28_2764.zip`.
* `tools/rom_catalog.json`.
* `EII ROMs.txt` and `debug_info.txt` from the archive.
* The EII service manual (`eii_service.pdf`), read from rendered pages:
  - PDF19, section 2-9 "The Main CPU": the main EPROM is a 2764 at IC42 (p.7-12).
  - PDF14: the scanner EPROM is a 2764 at IC28.
  - PDF143, parts list:
    - Main 2764 EPROM IP317 IC42
    - Scanner 2764 EPROM IP318 IC28
    - PROM UCD0.6 IP319 IC137
    - PROM UCD11.1 IP308 IC135

## 1. Inventory (`Z:\EII\roms`)

| File | Size | SHA256 (prefix) | What it is | Board position |
|---|---|---|---|---|
| `main.bin` = `EIImainV2-1.bin` = `EIImainV2-0b.bin` = `E2plus_Mainboot_2_1_DM_2764.bin` | 8192 | 5082f8f8 | **Main boot 2.1**. Four identical copies; the "2-0b" name is the same image. 4-entry jump table at 0FE4h | IC42 |
| `Emulator 2+ Mainboot 2.1 DM IC42 2764.BIN` (owner, 2022 dump; also in ROMS.zip) | 8192 | ce14aeec | Main boot 2.1 "DM". **Identical code** to 5082f8f8 except 9 bytes at 0FDBh-0FE3h: three extra jump vectors (JP 0AEEh, JP 0C58h, JP 0C58h). That makes a 7-entry table, the same shape as main boot 2.3 | IC42 |
| `main23.bin` = `E2newerMain_HDMB2_3_MAIN.bin` | 8192 | f559c0be | **HD main boot 2.3** (EII+ HD). 7-entry jump table; issues the SCSI hard-disk commands (docs/hdd-experiment.md) | IC42 |
| `scanner.bin` = `EIIScanV3-1.bin` = `EII Scanner 3-0.BIN` | 8192 | 478cf8e0 | **Scanner 3.1**. The "3-0" file is the same image | IC28 |
| `scanner21.bin` = `EIIscanV2-1.bin` = owner `Emulator 2+ not signed IC28 2764.BIN` | 8192 | 250d1651 | **Scanner 2.1**. The owner's "not signed" chip is byte-identical to the archive's V2.1 | IC28 |
| `ic135.bin` = `EMUII_IC135_82S147N_UCD11-1.bin` | 512 | 725cd24f | Microcontroller PROM UCD11.1 | IC135 |
| `ic137.bin` = `EMUII_IC137_82S147N_UCD0-6.bin` | 512 | 874b9ec1 | Microcontroller PROM UCD0.6 | IC137 |
| `diag.bin` = `E2DIAG.BIN` | 8192 | 6632df9a | E-mu "E2+ DEBUG ROM", IP341, checksum 1B72. Sum verified: 1B72. "Main CPU Debugging Monitor V0.00, Aug 4 1984". Replaces IC42 for service only | IC42 (service) |
| `HDSCSIEII.bin` | 8192 | 30ede4cc | "COPYRIGHT 1983 ADAPTEC INC.": firmware of the **Adaptec SCSI-to-disk controller board** in the HD kit, not of an EII board. **Not needed**: the emulator models the drive at the SCSI command level that main boot 2.3 uses | HD controller |
| `820816-0081/00B8/0165/0181.bin` (2 KB), `E14Voice2708.BIN`, `Emu_wildcard.bin` (1 KB) | 2048/1024 | various | One family: same leading bytes 7ea13031c0e01ded. These are not EII images; they look like **Emulator I** (voice card / 2708) dumps | not EII |
| `boot copy.png`, `EII ROMs.txt`, `debug_info.txt`, `ROMS.zip` | - | - | Notes and the archive | - |

`EII ROMs.txt` says the latest main is 2.1, the latest scanner is 3.1, and
scanner 2.1 or higher is needed for the Mac link. Both are HN482764G-3
(2764) parts.

## 2. What the emulator boots today

* **Floppy (default):** `main.bin` (main 2.1, 5082f8f8) + `scanner.bin`
  (scanner 3.1, 478cf8e0) + `ic135.bin`/`ic137.bin`. With an OS 3.1 or bank
  floppy this is an **Emulator II+ with floppy**.
* **HDD selected** (menu: HDD... "next boot"): `main23.bin` (HD main boot
  2.3) with the same scanner and PROMs, plus a 20 MB raw image. That is an
  **Emulator II+ HD** (FrontPanelSession / PluginEngine: "HDD boot requires
  main23.bin").
* **Diagnostic EPROM:** `diag.bin` in IC42 (standalone only).

Measured with `tools/panel_latency_probe`:
* Owner main "2.1 DM" (ce14) boots Grand Piano #2 to the same LCD.
* It gives the same panel timings as main 2.1 in every module test.
* Scanner 2.1 versus 3.1 likewise gives identical results.
* The three extra DM vectors were not reached in these floppy flows. Which
  firmware calls them is open. Do not assume an OS needs them.

## 3. EII+ HD: what it needs and what we have

| Item | Needed | Status |
|---|---|---|
| Main boot | HD main boot 2.3 (`main23.bin`) | present, used when an HDD is selected |
| Scanner | 3.1 (2.1 works the same in our tests) | present |
| Microcontroller PROMs | UCD0.6 / UCD11.1 | present |
| HD controller | SCSI controller + 20 MB drive (Adaptec board, `HDSCSIEII.bin` is its own firmware) | emulated at the SCSI command / DMA level that main 2.3 drives; firmware not needed |
| OS | HD OS (v2.6HD, v3.1HD: "HardDisk 20 R3.1") on floppy, then copied to HD (Special 17 "Copy HD Sftwe", 22 "FormatHardDsk") | images present in `Z:\EII\disk\os-images` |
| DRAM | **1 MB as two 512 KB banks** (1 MB expansion, bank line = SIO-B /RTS, main boot 0A8Bh/0AEEh; docs/MEMORY_1MB.md) | implemented, selectable (plug-in default 1 MB for new instances; floppy boot needs main21dm.bin, HD uses main 2.3) |
| Second floppy | firmware supports "Drv1/Drv2" and "No 2nd Drive" | core supports drives 0/1; the plug-in mounts only drive 0 (see DRIVE_SLOTS_DESIGN.md) |

## 4. Recommendation

* **Target:** Emulator II+ HD. main boot 2.3 + scanner 3.1 + UCD0.6/UCD11.1
  + a 20 MB HDD image with HD OS 3.1.
* Overlays then come from the HD instead of the floppy: the slow first module
  press below is mostly floppy time. The HD timing is not yet measured.
* **Fallback (kept, selectable):** EII+ floppy, main 2.1 + scanner 3.1. It
  is selected automatically when no HDD is chosen.
* Main boot "2.1 DM" exposes the 1 MB bank-swap vector at 0FDBh (docs/MEMORY_1MB.md); with 1 MB it is required for floppy boot (plain 2.1 crashes at that vector).
* Owner main "2.1 DM" and scanner 2.1 are catalogued as alternatives:
  `main21dm.bin` and `scanner21.bin` in `tools/rom_catalog.json`. They are not
  defaults.
* Ignore the Emulator I files and `HDSCSIEII.bin` for the EII.

## 5. Slow first press of module buttons (measured)

Measured with `tools/panel_latency_probe ROMDIR "18 Grand Piano #2.hfe" ...
seq`. Real firmware, read-only bank, no GUI or DSP. Logs are in
`validation/rom-sets-2026-10-30/`.

| Press | LED latch | LCD | Floppy read |
|---|---|---|---|
| Voice Def, 1st (motor still on after boot) | 916 ms | 899 ms | 17-897 ms |
| Voice Def leave / re-enter | 6 / 19 ms | 7 / 6 ms | none |
| Preset Def, 1st (motor off) | 1631 ms | 1625 ms | 835-1621 ms |
| Voice Def again, after Preset Def | 1637 ms | 1619 ms | 832-1618 ms |
| VCA/LFO, 1st | 1634 ms | 1618 ms | 829-1616 ms |
| Filter, right after VCA/LFO | 18 ms | 3 ms | none |
| Disk, RTC, Tune, Transpose | 3-24 ms | 2-8 ms | none |

**Root cause:** real firmware behaviour, not our scanner or LED path. The OS
keeps module code as disk overlays. The first press of Voice Definition,
Preset Definition, Sample, Special (Catalog) or VCA/LFO loads that module
from the floppy. Only then does the firmware write the LCD and latch the LED.

* Re-entering the same module is fast.
* Switching to a module in another overlay reloads it.
* Filter shares the VCA/LFO overlay.

The 1.6 s breaks down as about 0.8 s motor start plus ready (after the 3 s
motor-off delay) and about 0.79 s of reading. The motor start is the SA465
reference: 500 ms worst case, 300 ms typical (FLOPPY_HARDWARE.md); a typical
drive would be about 0.2 s faster.

Neither of these changes it:
* Scanner 2.1 versus 3.1: identical timings.
* The 0.3.22 fast-panel option: identical; it patches only slider averaging.
