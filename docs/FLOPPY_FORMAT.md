# EMUIIFD/FM and read-separator laboratory, checkpoint0.26

The recovered ZM325 diagnostic disk is a573440-byte payload image. `EmuiiImage`
validates exact length, exposes read-only cylinder/side spans and synthesizes
explicit nominal FM tracks. It is not mounted in `Machine`: the existing
floppy probe still uses sensor-only media and `DiskSio` still has no receiver.
Neither synthesized CRC validity nor a format roundtrip demonstrates OS boot
or verifies an original disk's magnetic encoding/CRC. No private bytes or PDF
are added to main; validation logs contain metadata/CRC summaries only.

## Sources and recovered input

- Service manual SHA256f1071d347818ecfd107df0808ceade6d9e460987a85f86a04c71707a82700031,
  PDF78/printed7-15. Enlarged visual inspection confirms IC10674LS221,
  C11=68pF2%, R21=90.9k1%, IC10574LS74, RXDA/Q and RXCA/Qbar wiring.
  C11's reference and68pF value run together on the scan; it is not1168pF.
- ///Esynthesist, *Disk layout of Emulator II floppy disks*,v1.2,2010-11-08:
  https://emxp.net/Disk_layout_of_EmulatorII_floppy_disks_v4.pdf
  22911bytes, SHA256734eee8bcd4f0c3bbf97c97c520ba8de3d9f3e08dda627f3b890af60d24e332d.
  Both pages read; page2 visually checked. Experimenter's reverse engineering,
  not an E-mu factory specification. Identifies FM,3584payload bytes, FA96
  marks, polynomial8005 and zero initial/final CRC values.
- HxC reference inspected at commit0c234a0141ae843bc5b1b0457602c3f892cbcb53:
  https://github.com/jfdelnero/HxCFloppyEmulator/tree/0c234a0141ae843bc5b1b0457602c3f892cbcb53/libhxcfe/sources
  `loaders/emuii_raw_loader/emuii_raw_loader.c`, SHA256
  5e01f36420729027667423aab7134747b37680c1d8bbdada208cc61d2e82138c,
  confirms offset3584*(2*cylinder+side). `tracks/track_formats/emu_emulator_fm_track.c`,
  SHA256effb39036c1dc3043d4e31f1609af3fda3fc39e8417616d0de69f10448ee848d,
  confirms ID=2*cylinder+side, bit order, CRC scope and inter-field gap detail;
  companion header SHA25696206bbda7eb7274de975cef3bc8c5e97f1bdaec58c3ac7a5d844977f3e8dbf2
  sets312500 encoded slots/s. These GPL reference files remain outside main;
  no HxC code/tables imported. Implementation is new and uses documented format
  facts and a bitwise polynomial division, without HxC dependencies.
- TI SN74LS221 SDLS213B,1983/revised2004, PDF2 describes non-retriggerable
  operation and nominal width approximately0.7*R*C:
  https://www.ti.com/lit/ds/symlink/sn74ls221.pdf . This later reference is
  not proof of the exact original EII chip revision or measured pulse width.
  Local SHA2569d7f372b30430c703fff05476d16c1961f9a047342277f5918dc518cf71f16d3;
  PDF2 visually checked for pulse relation/non-retriggering/function table.

Original ZIP `EmuII_MemoryTest_zm325_Disk.zip`:34961bytes,
SHA256926edf13fc5a326ca89efa954a72016303ca9f55befad4f7408afe66cd2c17ae.
Only member `zm325.emuiifd`:573440bytes,
SHA25604d603a9e664ddfd3ac5c54e09d7c57e1c638b8fdc2d0bae83b25237becf7033.
Recovered from the previously supplied Library item; no new disk download or
replacement by another emulator's data. E2O is a different input format and
is not accepted here. Equal length alone cannot authenticate arbitrary image
contents; the caller explicitly declares a raw EMUIIFD container.

## Nominal serialization

80cylinders x2sides x3584bytes; each byte is LSB first. Track ID is physical
cylinder*2+side, not cylinder alone. CRC is reflected8005 (A001 recurrence),
zero initial/final XOR, low byte then high. Independent MSB-first long division
over reversed bytes verifies this convention. Check string123456789 ->BB3D.
Only ID or payload bytes enter their respective CRCs; sync/marks are excluded.

| Field | Serialization |
| --- | --- |
| Leading gap/sync |20FF bytes,4zero bytes |
| Header |FA96, one ID byte,2CRC bytes, one zero byte |
| Inter-field gap, report2010 |8FF bytes |
| Inter-field gap, hxc2026 |7FF bytes then4one bits |
| Data sync/mark |4zero bytes, FA96 |
| Data/check |3584payload bytes,2CRC bytes,2zero bytes |
| Trailing gap |20FF bytes, then ones to revolution end |

Both profiles are required explicit API/CLI arguments. HxC's half-byte gap
differs from the written report; no preference is silently made or claimed
to be measured on the supplied disk. Data mark starts at bit336(report) or
332(HxC); payload begins16bits later. FM adds one clock transition per data
bit, then a transition if the data bit is1. No missing-clock mark is invented.

Each312500Hz FM slot is64edges on the existing20MHz time base; data rate
156250bit/s. At300RPM one revolution is4000000edges /62500FM slots /
31250data bits. The fractional final byte is preserved, rather than rounding
to a byte count. These are nominal synthesized media; image bytes do not
retain original speed variation, gap geometry, weak bits, bad CRCs or index
phase. Format bit0 is an explicit nominal index-origin convention.

`inspectEmuiiFm` scans every data-bit phase for zero sync/FA96 and validates
header/data CRC, including revolution wrap. It requires nominal clock/data
slot alignment and all FM clock transitions; this is an offline format
checker, not a replacement for the hardware's bit synchronization or an
arbitrary damaged-flux decoder. It never writes CPU memory or SIO FIFOs.

## Read separator and boundary

`FloppyReadSeparator` consumes actual trailing/rising edges of active-low
drive read pulses in20MHz-edge time. The LS74 captures OLD LS221Q; a trigger
when Q is already high does not extend the LS221 pulse. Expiry raises Qbar,
producing the receive clock edge with the recovered data value. No input
transition creates a byte, sync match or DMA transfer by itself.

The nominal experiment uses87edges=4.35us, near the rounded0.7*90.9k*68pF
estimate4.32684us; it is an explicit width, not a fitted/measured chip value.
Trailing read-pulse offset8edges=400ns is another explicit experiment
condition. Under ideal propagation, the usable window lies strictly between
3.2us and6.4us; tests exercise65/80/87/100/127edges, and reject recovery at
63/129edges. Equality/races, monostable recovery/setup, pulse-width variation,
drive jitter, tolerance/temperature/stray capacitance, analog thresholds and
physical cold-start convergence are not calibrated. Laboratory state starts
idle Q/data0; attaching to a live drive must handle initial phase/history.

RXCA is IC106Qbar directly. IC129's lower gates belong to the transmit path;
do not use them to gate receive clocks. Full transmitter, LS393write-pulse
generation, write-gate behavior and disk modification are still absent.

The next transport milestone must connect this per-edge output to programmed
SIO-A sync hunt/bit assembly/FIFO/delayed CRC/READY, then implement real DMA
bus request/grant/memory writes. Original ROM06A3 programs RX/CRC/hunt and
WR1DMA-ready setup, then sends the DMA block at06A7. Source-ROM disassembly
was checked locally without embedding it in main. No direct decoded-sector
copy or patched firmware is used as a boot substitute.

## Reproduction and validation

```sh
bash tools/build-native.sh
build/disk_image_probe ../inputs/zm325.emuiifd report2010 > /tmp/zm325-report.json
build/disk_image_probe ../inputs/zm325.emuiifd hxc2026 > /tmp/zm325-hxc.json
```

The probe validates exact bounded input size, all160payload mappings, both
CRCs and the bitwise read-separator output. Repository JSON logs record
synthesized CRCs, offsets and recovered bit counts without payload bytes.
The new test independently checks every serialized payload bit, all256ID-byte
CRC inputs, both profile offsets,31250-bit frame length,13-bit rotation and
revolution-wrap sync, deliberate data/header CRC corruption, missing clock,
invalid slot/size/profile/address/time and separator window negative controls.
See VALIDATION.md for executed checks and limits.


## Checkpoint0.27 connection

This0.26 laboratory description is retained. The opt-in rotor/SIO/byte-mode
DMA path now uses these frames; see SIO_DMA.md. First payload reaches strict
DRAM byte-exact, but a CRC/Hunt continuity failure causes firmware retries.
The default Machine and legacy probe invocation still have no mounted image.
