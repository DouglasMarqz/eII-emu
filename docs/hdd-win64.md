# Standalone WIN64 HDD integration — 2026-10-23

This is the existing standalone application. There is no VST3 implementation.
Base remains perf/cpu-placement-072 / 6dc1133ce9cf60ad70e97534320dad5a4d0ec473.
This source milestone is being committed with explicit owner approval.
Find the containing commit on perf/cpu-placement-072; private images/binaries remain outside Git.

## Operation

The settings window has an HDD... menu: select an image for next boot, create a
blank image, select no HDD for next boot, save a copy, and inspect mounted/selected
paths. Image selection is stored in the per-user HddImage registry value; the
existing calibration/audio preferences retain their format.

The raw image must contain 22597632 bytes (613*4*18*512). HDD boot selects
main23.bin; ordinary boot still selects main.bin. Diagnostic EPROM plus HDD is
rejected. Both scanner and controller PROM files remain required. A .E2O file of
72704 bytes can be imported as experimental derived HD OS floppy media, using
the same zero-padded573440-byte mapping that booted/formatted in the core probe.

Writes stay in the worker-owned in-memory HDD. Finish the sampler's DISK7 save
or formatting operation before choosing Save HDD copy. The host pauses and
rejects active target/DMA transfers, but bus-idle alone is not proof that a whole
firmware bank transaction has finished. Wait for the sampler to return to its
Disk menu before exporting. No bank-level transaction/rollback is invented.

A copy is written to an exclusively created sibling temporary file, flushed,
and published to a NEW destination without replacement. Existing targets are
never overwritten, including a race after the file dialog. Failed saves retain
the dirty flag. A successful copy becomes the next-boot selection; the current
source path remains visible separately. The machine remains paused after saving.

Reset/exit first obtains a pause acknowledgement from the owner worker. Dirty
HDD state offers Save / Discard / Cancel. Save failure/cancellation keeps the
old machine paused. A failed replacement boot preserves it even if discard was
approved. The worker independently rejects an unapproved dirty reset. Hiding the
settings window does not quit the application. The existing trace-exit prompt
remains in place. Abrupt process/power loss is not protected by this explicit
copy-save workflow.

## Validation

- hdd_panel_probe uses the actual FrontPanelSession worker, real unmodified ROMs,
  and a saved bank02 image. HDD-only boot, DISK7 save, pause/copy/reopen, active
  transfer rejection, failed-save dirty retention, dirty-reset rejection,
  transactional failed mount, diagnostic conflict, E2O import, source preservation
  and default no-HDD boot pass on Linux.
- hdd_image_file_tests passes Unicode roundtrip, no-overwrite, competing writers,
  invalid/truncated input, failed-output preservation and temporary-file cleanup.
- disk_dma_tests and hard_disk_tests pass after the bounded256-command HDD
  history change. The existing front_panel_session_tests is rerun separately.
- WIN64 GUI and HDD-file-check.exe cross-compile with Ubuntu GCC13-posix /
  MinGW-w64 SDK11. Thread-description API declarations and property-key definitions
  are supplied by build-only SDK compatibility files; emulator source/ROM is not
  patched for the SDK. Static runtime; only system DLLs are imported.
- This is NOT native Windows runtime, audio-performance, or UI visual validation.
  The project's primary LLVM-MinGW UCRT build remains the release toolchain.

On Windows, the existing tools/build-win64.ps1 now includes the two asset-free
HDD tests and builds hdd_panel_probe.exe. The private-input probe is opt-in:

```
hdd_panel_probe ROMDIR saved-bank02.img NEW-copy.img HD-OS.e2o
```

Next: native Windows menu/save/reset/exit/Unicode test, MIDI/audio and sampling
regressions on the owner's host; then review before any commit/push. VST3 is a
separate future task. Physical AE343 timing, ACB4000 MCU/MFM/ECC and1MiB expansion
remain outside this functional-target milestone.

## Owner confirmation and next requirement

On2026-10-23 the owner tested the delivered WIN64 preview and reported it works.
This establishes general owner confirmation, not an enumerated native test suite.
Floppy writes and a switchable protected-copy/direct-image backend are the next
feature; see media-write-policy.md. Current HDD copy-save behavior is unchanged.
