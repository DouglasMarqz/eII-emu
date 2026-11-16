# MAME floppy reference check, 2026-10-17

Inspected upstream MAME src/mame/emusys/emu2.cpp through the GitHub connector.
Upstream master at inspection: 8a18b751b388390db32812be43b8802de8cac839.

The EII driver is still a skeleton marked MACHINE_NOT_WORKING and
MACHINE_NO_SOUND. It instantiates Z80SIO (disksio, IC126) and Z80DMA, maps
SIO at 38h..3Bh mirrored at +4, and DMA at 20h mirrored through 27h.
The configured DMA has memory callbacks; this driver does not connect the
complete floppy bitstream, drive mechanics, SIO clocks/data or DMA I/O and
bus-grant transport. No floppy image connector or EII disk format is
registered in this driver. Therefore it is not a ready-made EII boot path.

Useful future references exist in the wider MAME framework:
- src/devices/machine/z80sio.cpp and .h
- src/devices/machine/z80dma.cpp and .h
- src/devices/imagedev/floppy.cpp and .h
- src/lib/formats/ (generic format infrastructure)

The directory listing under src/lib/formats had no filenames containing
emu/eii/e2/sound/akai. This filename check alone cannot prove that no EII
format support exists anywhere in MAME. We have not validated .E2O or
.emuiifd with MAME and have not imported any MAME implementation.

Decision: retain these as SIO/DMA/drive implementation references, continue
output-board hardware first as requested, and identify the user-provided
disk containers before implementing actual firmware-driven disk loading.
If code is reused later, audit dependencies and preserve the specific
files' licenses and copyright notices.

Source links:
https://github.com/mamedev/mame/blob/8a18b751b388390db32812be43b8802de8cac839/src/mame/emusys/emu2.cpp
https://github.com/mamedev/mame/blob/8a18b751b388390db32812be43b8802de8cac839/src/devices/machine/z80sio.cpp
https://github.com/mamedev/mame/blob/8a18b751b388390db32812be43b8802de8cac839/src/devices/machine/z80dma.cpp
