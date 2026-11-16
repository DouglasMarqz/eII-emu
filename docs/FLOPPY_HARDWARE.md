# Floppy hardware research, 2026-10-17

## Identity and reference profile

The supplied EII service manual's parts list, PDF page 148 / printed 9-10,
was visually checked: it lists Disk Drive, E-mu P/N **ZM302**. It does not
identify a drive manufacturer's exact model there. Do not claim that every
EII revision uses one identical mechanism or jumper arrangement.

A Shugart **SA465** is selected as an initial reference profile, not a
verified identification of a particular user's physical instrument. An HxC
forum discussion names SA465-3AA for EII; this is an identification lead,
not an E-mu factory cross-reference:
https://hxc2001.com/floppy/forum/viewtopic.php?p=1115
 EII
interface schematics are authoritative for host wiring. EII drive-model
cross-reference and installed options remain to be confirmed before
mechanism behavior is made a default. The 40-track SA455 is not silently
substituted for the 80-track SA465.

Downloaded and visually inspected the original manufacturer document:
Shugart 455/465 Flexible Disk Storage Drive Reference Manual, October 1984,
P/N 39409-1, 86 PDF pages. Source:
https://www.bitsavers.org/pdf/shugart/SA455_465/39409-1_Shugart_455_465_Reference_Manual_198410.pdf

Local source size 6,120,677 bytes; SHA-256:
3c1a32c889f7154ad12eecc91cb2ac65419b6493006d25fe1d2629bab2b7462d
No manufacturer PDF is committed to the source repository.

The manufacturer table on PDF page 12 specifies SA465: 80 tracks, two-sided
5.25-inch media, 96 TPI, 300 RPM, 3ms adjacent-track access, 15ms settling,
300ms typical / 500ms worst motor start. Section 2.2.6 (PDF page 23) starts
head motion on the STEP low-to-high trailing edge; pulse minimum 1us,
adjacent step minimum 3ms for the 465. Direction high means outward,
low means inward. Power-up head position is unspecified, so firmware must
recalibrate using TRACK 00. Motor control includes delayed stopping in
section 1.6.3; do not implement instant spindle phase/reset assumptions.

Manufacturer prose has inconsistencies: PDF 11 vs 12 seek figures,
PDF 16 vs 23 direction wording, and PDF 25 soft-sector pulse wording.
Cross-check timing figures and EII traces before implementing these areas.
Drive-format examples do not define EII encoding.

## EII external interface

Visually traced service schematic 7-14 / PDF 77 and 7-15 / PDF 78. CN11 is
a 34-pin drive header. This is a Z80 SIO/PIO implementation with external
logic, not a ready-made WD179x command/register interface.

| CN11 pin | EII net | Function |
| --- | --- | --- |
| 8 | -DIDX | Index input |
| 10 | -DSEL0 | Drive select 0 |
| 12 | -DSEL1 | Drive select 1 |
| 16 | -DMTRON | Motor control |
| 18 | -DDIR | Direction |
| 20 | -DSTEP | Step |
| 22 | -DWDTA | Write data |
| 24 | -DWTGT | Write gate |
| 26 | -DTK00 | Track-zero input |
| 28 | -DWP | Write-protect input |
| 30 | -DRDTA | Read data |
| 32 | -DSIDE | Side select |
| 34 | -DRDY | Ready input |

PIO B participates in drive selection, motor, direction, step and status.
SIO channel A and external LS393/LS221/LS74/LS10 logic form the disk data
path. Exact ready/jumper behavior, read separator, write pulse generation,
SIO synchronous framing and DMA bus ownership remain to be implemented.
A generic PC disk controller or IBM format would bypass this circuitry.

## Disk format research lead

Primary reverse-engineering report by its experimenter, ///Esynthesist:
https://emxp.net/Disk_layout_of_EmulatorII_floppy_disks_v4.pdf
(version 1.2, 2010-11-08; not an E-mu manufacturer specification).
It reports 300 RPM, FM, two sides, 80 tracks per side, one 3584-byte sector
per track, with track-ID/data framing and CRC-16 polynomial 8005h. The
reported approximately 310kbit/s encoded / 155kbit/s data rates must be
checked against the EII clock-divider circuitry before choosing exact timing.

Inference only: 80 * 2 * 3584 = 573440 bytes, matching the previously cataloged
.emuiifd size. Size agreement alone does not verify track ordering, framing,
headers or the contents of that input. The .E2O OS file remains a separate
72704-byte input type, not assumed to be a raw disk. No image parser, disk
mount, drive emulation, format encoder or successful OS boot added here.

## Compatibility-tip check, checkpoint 0.9

The user's proposed rule "any 360KB or 720KB DS/DD drive" is too broad.
EII's reported disk geometry needs 80 cylinders, two heads and compatible
300RPM/FM behavior. A 40-cylinder mechanism cannot reach all those tracks;
Shugart/DS0/READY settings cannot add cylinders. DOS capacity labels do not
define the EII layout or prove compatibility.

Manufacturer documents downloaded and visually checked:

| Reference | Evidence | Consequence |
| --- | --- | --- |
| TEAC FD-55E/F specification, printed 107/108, PDF 9/10 | E is single-sided 80 tracks; F has 80 tracks per surface, two sides | F is a geometry candidate, E is not equivalent |
| Panasonic JU-455-5 application manual, PDF 5 / printed 1-2 | 48TPI, 40 cylinders, 80 total tracks, two heads | This variant is not an 80-cylinder EII replacement |

TEAC source:
https://retrocmp.de/fdd/teac/TEAC_FD55-E-F.pdf
6,531,118 bytes; SHA-256
b41136e4da6b6056499160cc5b6128e2864eede7b0b9edc0e1278908c1ede1f0

Panasonic source:
https://www.bitsavers.org/pdf/panasonic/floppy/JU-4x5/Application_Manual_Panasonic_JU-455-5.pdf
1,156,125 bytes; SHA-256
83ec2d8f4a12fdc2839c0a8c03149a4e7a0d911c0f333c43fcea107febc6d831

Do not generalize one suffix's document to every manufacturer revision.
FD-55F is not yet an electrically/timing-tested replacement in this project.
An HD or GFR label alone is neither a compatibility certificate nor proof
that every possible reconfiguration is impossible: exact spindle speed,
read/write mode, READY, selection and interface options need verification.
DS0 pertains to the first-drive select, not a rule that every drive in a
two-drive setup must share DS0. See CN11 DSEL0/DSEL1 on schematic 7-14.

The HxC manufacturer's support table confirms its emulator supports EII on
a Shugart interface. It does not certify arbitrary physical floppy drives;
its EII row does not specify the same DS0 note as the adjacent Emax rows:
https://hxc2001.com/download/floppy_drive_emulator/support.htm
The supplied Menemszol link concerns Emax II, not an EII-specific validation.
The supplied Reddit page was unavailable and was not used as evidence.
No replacement-drive purchase or universal jumper recipe is recommended.


## Checkpoint0.25 implementation

The earlier research above is retained. A nominal SA465-reference mechanical
model and SIO modem-status interrupt subset now exist; see FLOPPY_MECHANICS.md
for the enlarged PIO/SIO pin mapping, explicit initial-condition/sensor-only
media experiment, physical timing limits and current validation. Serial data,
read separator, DMA and real disk-image transport remain unfinished.
