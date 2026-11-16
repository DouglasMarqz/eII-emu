# Drive slots: design (not implemented)

Goal: copy and archive banks on the emulated EII itself, using the
firmware's own disk functions, without EMXP. This is a design only; nothing
here is implemented.

## 1. What hardware and firmware support

* **Floppy drives:** two. Sheet 7-14 drives them from PIO port B: B4 = DSEL0,
  B3 = DSEL1, B2 = STEP, B1 = DIR, with the common MOTOR_N = B4·B3.
  * The core already models both (`FloppyBus::drives[0..1]`,
    `DiskReadPath::mount(0..1)`, `mountWritable`).
  * The plug-in and the standalone currently mount only drive 0.
* **Hard disk:** one 20 MB SCSI drive, used with main boot 2.3 (ROM_SETS.md).
  It is modelled at the command/DMA level as a raw image (`hard_disk.h`).
* **Firmware functions** (OS 3.1 / 3.1HD strings; the exact numbers of the
  Disk 1-8 menu still need verifying with a probe):
  * **Disk module ("Disk [1-8]"):** Save Bank to Drive ("Which Drive?"),
    load, format Perf/Library disks ("Format in Drive", "Format Another"),
    "Copy Frm Another" disk.
  * **Two-drive copy:** "Src Disk in Drv1" / "Dst Disk in Drv2".
  * **One-drive swap copy:** "Insert Dest Disk", "Copy will Erase".
  * **Voice/Preset copy:** "Copy Voice", "Copy Preset", "Copy Frm Another".
  * **HD Special functions:**
    * 17 "Copy HD Sftwe": OS to HD.
    * 18 "AutoHD Backup": "Saving All Banks", "Backup Floppy", "Reload
      Backup".
    * 22 "FormatHardDsk".
  * **HD OS bank access:** "Get HD Bank #", "Save Bank to Drv", "Use Floppy
    Sftwe".
  * **Error strings:** "No 2nd Drive", "Write Protected", "Disk is Full".

## 2. Slot model

| Slot | Hardware | Media | Default |
|---|---|---|---|
| A | floppy drive 0 (DSEL0) | .hfe / .emuiifd / E2O import | as today |
| B | floppy drive 1 (DSEL1) | same | empty, drive installed (so the firmware sees a 2nd drive) |
| H | SCSI HDD (main 2.3) | raw 20 MB image | as today, next boot |

Each slot has three properties:
* **Source:** a file path.
* **Access:**
  * "protected copy" (default): a working copy; Save Copy writes a new file.
  * "direct": writes go to the file, never the default.
  * "write-protect tab": the sensor reports protected, so the firmware itself
    refuses ("Write Protected").
* **State:** clean / dirty / busy, shown as an LED.

Hot-swap is allowed only while the drive is idle (existing floppy guards:
write gate, DMA grant, rescue copies on project load).

## 3. UI

The Settings menu gets "Drives" with three rows:
* **A / B:** Insert, Eject, Save copy, Write-protect tab, Direct writes.
* **H:** Select image (next boot), New blank 20 MB (existing), Save copy.

In the editor, each slot shows a small label with the file name, a
protect/dirty/busy marker and its drive LED (motor or select).

Typical workflows, all run by the firmware:
* **Archive a bank:** source in A, a blank image in B (New blank floppy), then
  the firmware's two-drive copy.
* **Collect banks on HD:** load from A, then "Save Bank to Drv"; or use
  Special 18 to back the HD up to B.

## 4. Media-write protections

* Never write a source file unless that slot is set to "direct".
* Protected copies are saved under new names only. No silent overwrite.
* A dirty slot asks before eject, project load or exit. The existing rescue
  path writes recovery files.
* An image open in two slots is refused, or forced read-only in the second.
* There is no write while the firmware has a transfer active (write gate /
  DMA / HD busy). Eject and insert are refused then.
* E2O imports are always read-only.
* The project state stores paths and access modes only, never media
  contents.

## 5. Questions for the owner

1. Should slot B be present by default (a second drive installed) or opt-in?
   The firmware behaves differently with "No 2nd Drive".
2. Blank media: offer "New blank floppy" for Perf and Library formats, or
   only unformatted, so the firmware's own Format is used?
3. Should drive-copy jobs run faster than real time (turbo while copying),
   or stay hardware-timed?
4. HD in the plug-in: a single fixed image per project, or a global library
   image shared by projects (risk: two instances writing one file)?
5. Do you want a bank browser (read-only directory of A/B/H) outside the
   firmware, or only the firmware's own menus?
6. Is the target the EII+ HD (main 2.3) by default, with floppy-only as the
   fallback (see ROM_SETS.md)?
