# eii-emu: E-mu Emulator II emulator

A hardware-level emulator of the **E-mu Emulator II** sampler (1984). It comes as a Windows x64
standalone application and a VST3 instrument. Two emulated Z80 CPUs run the original main and
scanner firmware, with the controller, DRAM and SIO/DMA floppy paths, an experimental SCSI
hard-disk path, the sample DACs and the analog voice path. The firmware draws the LCD, reads MIDI, loads the disks
and plays the voices. There is no prerecorded boot sequence and no replacement note allocator.

## Bring your own ROMs and disks

Nothing from E-mu is included: no EPROM or PROM images, no OS disks, no sample banks and no
manual scans. You need these:

- **Main boot EPROM** (2764, IC42) and **scanner EPROM** (2764, IC28).
- **Controller PROMs** (IC135, IC137).
- An **OS disk** (for example OS 3.0 or 3.1) and sample disks, as images.

E-mu Systems as it built the Emulator series no longer exists, so these cannot be obtained
from a manufacturer, and this project cannot help you find them. `docs/ROM_SETS.md` lists the
SHA-256 identities the emulator has been run with, so you can check your own images.
`tools/Prepare-ROMs.ps1` sets them up.

## Original and Custom

- **Emulator II** is the original instrument: the EII board's SSM2045 filter, S3528 switched
  stage, VCA and output path, modelled from the service manual.
- **Emulator II Custom** is a feature on top of the original. It is a separate VST3
  (`EmulatorII-Custom.vst3`, its own class ID) that can be installed beside it. It offers an
  EIII CEM3387 filter in the EII voice. See `docs/CUSTOM_EIII.md`.

All filter and analog models are this project's own work, except the EIII CEM3387 model.
That model comes from [Tuth/Emulated-3](https://github.com/Tuth/Emulated-3) under GPL-3.0,
in `vendor/eiii-cem`.

## Building (Windows x64)

Use LLVM-MinGW UCRT x86_64, tested with 20261006:

```powershell
$env:LLVM_MINGW_ROOT='C:/tools/llvm-mingw'
powershell -ExecutionPolicy Bypass -File tools/build-win64.ps1
powershell -ExecutionPolicy Bypass -File tools/build-win64-vst3.ps1
```

The first script builds the standalone and runs its tests; the second builds the VST3. The
synthetic tests need no firmware. On Linux, `bash tools/build-native.sh` builds and runs the
native test set with g++. `docs/` holds the hardware notes, measurements and design records.

## Status and limits

The firmware boots OS 3.0 and 3.1 from floppy images, plays banks over MIDI and drives the front
panel. The analog path is a documented approximation, and some areas are unfinished: absolute
filter calibration, recording/SAR, MIDI OUT and LEDs. See `docs/VALIDATION.md` and
`docs/ROADMAP.md`.

## Credits and licensing

GPL-3.0, see `LICENSE`. The project as a whole is GPL-3.0 because it includes the GPL-3.0
CEM3387 model. Vendored parts keep their own licences and notices:

- `vendor/chips`: Z80, CTC and disassembler from Andre Weissflog's
  [floooh/chips](https://github.com/floooh/chips), zlib/libpng licence.
- `vendor/vst3sdk`: Steinberg VST3 SDK 3.8.1 subset, MIT.
- `vendor/ssm2045-chip`: SSM2045 behavioural model, MIT.
- `vendor/eiii-cem`: EIII CEM3387 model, GPL-3.0.

The initial memory map and device reference came from AJR's BSD-3-Clause
[MAME emu2.cpp](https://github.com/mamedev/mame/blob/master/src/mame/emusys/emu2.cpp).
Board wiring was checked against the E-mu service manual, which is not redistributed. See
[CONTRIBUTORS.md](CONTRIBUTORS.md) for who did what.

Emulator II is an E-mu product. This independent project is not affiliated with its rights
holders.
