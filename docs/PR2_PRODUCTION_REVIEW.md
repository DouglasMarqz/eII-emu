# PR2 production controller review — 0.42

Reviewed head: `b0d27d9a8d2b8d5d736dc78bc4d3885af2ac3f74`.

The reported failure is a persistent loss of all emulated panel buttons after
pressing Play on a booted bank. The Windows application remained responsive.
This failure has **not been reproduced as a persistent freeze** by the native
probes. It is not justified to call it fixed by this revision.

## Runtime selection

Concrete GUI startup selects production with phase0, DRAM seed1 and controller
seed `0xe1122026`; explicit reference override uses those same startup values.
Unknown/all16-phase verification retains the reference. Production owns its
controller edges, RF, ALU, DRAM service and DAC state. Shared PROM tables and
address-latch components do not run a second reference controller. No global
unknown-state fallback is introduced.

## Changes

* Enforce the existing production invalid-state contract at Machine boundaries.
  Previously invalid states were flagged but execution continued. Stop before
  host audio/DRAM write publication, report the first failure code, source edge,
  phase, channel and control word, and retain the actual engine for inspection.
* Stop controller edges after a failure; do not latch a chosen RFD value when
  drivers disagree or the bus floats. Close the low-phase write aperture at the
  rising edge.
* Validate backing-byte knownness at the actual CAS read for both CPU and sample
  ownership. The earlier check covered only a CPU request, prematurely checked
  writes, and missed sample reads. Valid writes can initialize unknown bytes.
* Reject live engine selection after clocks have started: reference state would
  otherwise be stale. A fresh boot is required.
* Pass the old control word to upper DRAM rising, rather than the lower PROM
  address. This corrects the argument contract; the differing low bits were not
  used by that rising/settle path, so it is not an established freeze cause.
* Avoid polling the reference quiet-span interpreter in production. Defer
  production aggregate publication to the CPU interval boundary and omit the
  private diagnostic path copy with diagnostics disabled. No new Windows speed
  percentage is claimed.
* Show the actual selected engine. Add a manual Capture state command with CPU
  PCs/IY/pins, PIO handshake/IRQ state and firmware task flags. It remains usable
  after a reported controller error. No periodic full report is enabled in the
  diagnostics-disabled GUI.

## Validation and limits

The full native build/test script passes without compiler warnings. The production failure tests also pass ASan/UBSan with leak detection disabled. Focused tests cover stop-before-run,
first-fault context, no live reference switch, contention latch suppression,
unknown sample CAS and a valid 1000-tick concrete fixture. Worker tests cover
visible fault publication and manual state capture after failure, with both
`EII_DIAGNOSTICS=0` and `1`. The previous all-zero synthetic PROM fixture itself
caused contention; it now uses an explicitly valid staging aperture.

Both the original PR and this revision passed native seeded bank boot plus two nonzero-PCM MIDI notes,
Play, another MIDI note, Stop, Filter and Select. Both engines were compared at
20k-tick boundaries after boot: CPU pins/PCs, LCD, all64 RF cells, eight DAC
codes and bitwise mono PCM hashes (the current engine mirrors mono to both outputs). Some panel operations waited over one simulated
second; the firmware was observed waiting on a task flag. This observation is
not proof of the user's persistent failure cause.

`tools/production_panel_probe.cpp ROMS IMAGE` reproduces this sequence with
caller-supplied assets. It boots200M CPU ticks and ends at224M. It does not test
all possible firmware states or exercise the Windows UI/audio driver.

Win64 Release is cross-compiled with LLVM-MinGW20261006, O3/LTO/NDEBUG and
`EII_DIAGNOSTICS=0`, with zero compiler warnings. Cross-compilation is not a
Windows runtime test. SSM2045/S3528 and audio fidelity are unchanged.

If the panel becomes persistently unresponsive again, press **Capture state**
and copy the bottom report (Ctrl+A, Ctrl+C). Preserve that captured report
before rebooting. It contains the actual engine and the two CPU/PIO wait state,
without leaving continuous instruments enabled.
