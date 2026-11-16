# EII+ / EII+ HD memory: 1 MB as two 512 KB banks (implemented 2026-10-30)

Sources:
* Rendered `Emulator-II-Memory-Upgrade-Instructions.pdf`: one page, "1MB
  Memory Expansion - Revision of 1/22/84". Kept outside Git in
  `F:\Claude\_assets\manual\` (with the owner's `Memory.txt`).
* Rendered `EMU-Emulator-II-SRAM-Map.pdf`: the 512 KB map (segments 0-15,
  32 KB each).
* Service manual PDF19: "large (512K) dynamic RAM ... sixteen 32K blocks",
  memory option p.7-32..7-37.
* Disassembly of main boot 2.1, 2.1 DM and 2.3; OS 3.1 strings.

## Findings

* **Standard EII:** 512 KB = 32 x 4164 on the digital board plus the 256 KB
  memory option board.
* **1 MB kit** (instruction page): 32 x 41256, a 74LS153, a 74S74, a
  "2764 MBMEM vers" main PROM and a kluge wire. The memory board and the 32
  4164s come out. The page ends at step 5 (trace cuts at IC79, IC20, IC39,
  IC11, IC10); the wiring steps are missing.
  * The 41256 needs a 9th multiplexed row/column address bit: that is the
    74LS153 (dual 4-to-1 mux) job, with the 74S74 synchronising the select.
* **Main boot firmware:**
  * 0A8Bh probe: SIO channel B WR5 = 62h (RTS on), write+verify a 6-byte
    pattern at 2800h; WR5 = 60h (RTS off), compare. Same data = one bank:
    20FFh = 22h and WR5 back to 62h. Different = second bank: 20FFh = 60h.
    20FFh is literally the WR5 value that 0AEEh toggles.
  * 0AEEh swap (only when 20FFh bit 6): DI, all eight channel timers get the
    8254 control word 30h (mode 0, stops them, OUT low), WR5 bit 1 (RTS)
    toggles, then 34h (mode 2) without counts, EI.
  * Boot (0991h-09A9h): with two banks the OS boot sector is read into both.
  * **2.1 DM** (owner IC42, ce14aeec) has `JP 0AEEh` at 0FDBh. **2.3** (HD)
    has `JP 0DB6h` there. **Plain 2.1** (5082f8f8) has FFh: OS 3.1 calls
    0FDBh once it sees a second bank and runs into RST 38h (PC stuck at
    0039h, measured). So the kit's "MBMEM" PROM is the 2.1 DM.
* **Conclusion:** EII+ / EII+ HD = 1 MB as two 512 KB banks, selected by
  SIO-B /RTS. The firmware stops sample playback around the swap, so the
  bank bit applies to every DRAM agent (CPU, playback controller, disk DMA).

## Model

* `Machine::installSecondBank(true)` before the first tick (default off: a
  plain `Machine` stays 512 KB and every existing fingerprint is unchanged).
* A change of SIO-B WR5 D1 exchanges the whole 512 KB DRAM array: the window
  2800h-7FFFFh and the bank-0 underlay (DRAM hidden by the CPU ROM/SRAM
  decoder). SRAM 2000h-27FFh is not DRAM and is not banked. All access paths
  keep their 19-bit addressing, so this equals a global 9th address bit. The
  internal 41256 row/column assignment is not observable and not modelled.
* Power-on seed: bank A gets the unchanged 512 KB sequence; bank B continues it.
* 8254: the mode-0 control word now drives OUT low (datasheet), as the swap
  routine needs; mode-0 counting stays unmodelled (a count makes OUT unknown).
  Before, the swap stopped the production controller ("unknown channel
  request"). The 512 KB firmware never writes mode 0.

## Plug-in

* Setting `memory1mb` (menu "Memory 1 MB, two 512 KB banks", next boot).
  New instances: on (EII+ HD target). Older project states without the key
  keep 512 KB.
* HD boot: main23.bin. Floppy boot with 1 MB: `main21dm.bin` (main boot 2.1 DM,
  produced by `tools/prepare_roms.py`); without it the floppy boot falls back
  to 512 KB with main.bin. Status shows running/next memory.

## Evidence (`tests/memory_bank_tests.cpp`, ROM arguments optional)

* Bank swap unit checks (bank A = 512 KB contents, SRAM unbanked, banks keep
  their data, 512 KB ignores /RTSB, size fixed after the first tick).
* Real main 2.1: 512 KB -> 20FFh = 22h; 1 MB -> 20FFh = 60h, 2 swaps.
* Main 2.1 DM + "18 Grand Piano #2": OS 3.1 boots to "P01 GrandPiano 1" with
  8 swaps; a note sounds (amplitude CV 187).
* Main 2.3 + `EII-HDD-20MB-bank02.img`: OS 3.1 HD boots to "P01 NULL PRESET"
  with 6 swaps; 20FFh = 60h.

Not verified: the user-level bank swap function of the OS ("Swapped > Bank
A") through the panel; it calls the same 0FDBh routine the boot uses.
