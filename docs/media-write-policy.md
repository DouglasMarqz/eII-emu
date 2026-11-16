# Standalone media writes — 2026-10-23

Implemented preview, based on af80f93ef2552b2c6e6b7ea251af641b7c030cb3.
The VST3 plug-in (docs/VST3.md) applies the same policy. Native Windows operation of this R/W preview awaits owner testing.

| Mode | Firmware writes | Host image |
| --- | --- | --- |
| Protected copy (default) | Allowed in working media | Source unchanged; explicit Save copy |
| Direct image | Allowed after exclusive writable open | Changed blocks written and flushed to the mounted file |

The persisted **Direct image writes (next mount / boot)** checkbox applies to
subsequent mounts and boots, for both floppy and HDD. It does not silently switch
an already mounted dirty image. Active mode, format and dirty state are visible.
There is no separate confirmation for each firmware write in direct mode.
Protected copy is host protection, not the physical floppy write-protect tab.

Use **Floppy...** to mount/eject/export a floppy while the machine is running.
Dirty working media require save/discard/cancel before eject/reset/exit. Save copy
creates a new file without overwriting another. A mount failure keeps old media.

Supported writable floppy formats: full 573440-byte EMUIIFD and strict valid
HFE v1 accepted by the existing decoder. HFE writes patch the affected payload
and CRC in its original container, retaining size, headers and other tracks.
The experimental 72704-byte E2O import stays physically read-only. Unsupported
flux/format features are rejected; an HFE is never silently replaced by raw bytes.
Full disk formatting and arbitrary partial/invalid flux writes are not modeled:
only complete CRC-valid firmware payload frames are committed. An invalid or
blocked captured frame stops with an explicit error rather than claiming success.

The transmitter clocks real SIO data/CRC bits through the FM write path, with
actual DMA Ready pacing. The added DMA search-only subset reads the source
without destination writes for the firmware's post-write verification. TX IRQ,
async/SDLC and general DMA modes remain outside this subset.

Sheet7-15 visually confirms INDEX (CN1-8) -> CTSA (pin18), READY (CN1-34) ->
DCDA (pin19). The earlier mapping was reversed. Correcting it fixes writable
cold boot without a forced write-protect phase, changed CTC clock or ROM patch.

Direct files are opened without truncation, compared with the bytes just loaded,
and held with an exclusive writer lease. Each accepted floppy frame / completed
HDD mutating command writes changed 512-byte ranges and flushes before returning.
Host write/flush failure pauses the worker and preserves dirty RAM state. Direct
writes are in place, not crash-atomic: a failed host write can leave partial bytes.
POSIX advisory locks require cooperating applications; Windows denies competing
write/delete opens. Do not edit a directly mounted image in another application.

Validation and reproducible private-fixture probes:
validation/floppy-rw-2026-10-23.md. ROMs, media and RAM dumps are excluded from Git.
