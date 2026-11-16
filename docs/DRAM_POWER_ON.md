# OS 3.0 startup: unknown DRAM backing, checkpoint0.32

The OS stall is traced to an uninitialized backing byte, not a missing floppy
payload or a demonstrated phase-consensus defect. On the unchanged PR1 core,
the firmware performs eight3584-byte payload transfers into physical2800..97ff.
All28672 bytes are known and byte-identical to the first28672 disk-image bytes
when the CPU later blocks. Both regions have SHA256:
`b64330778953a225b917441d0b4702b36ad497347afd32b8f32322c8d05844d0`.
CPU and DMA page-register traces confirm the mapping, including the8000 crossing.

## Blocking instruction and purpose

The observed call path enters4cfa ->2a4b ->43c5. Routine2a4b prepares a MIDI
control-change message with accumulatorB0 and DE7b00 (controller123, value0).
At43c5 it loads HL98c0, then43c8 reads C from that address for the message
channel/configuration. The visible vendor-core PC43c9 is already the following
instruction cursor; the blocked instruction is43c8. The routine also checks
98c1. These are bank/preset parameters outside the loaded OS range.

The bank/default parameter routine5eb0 contains a35-byte copy into
98ab..98cd, including98c0. That copy has not run before this old-bank/MIDI
shutdown path. The boot observer finds exactly2800..97ff known;98c0 was never written.
It observes the same unknown-backing read for8192 consecutive clocks and stops
at12436965 ticks. Original masks and backing bytes remain unchanged.

`DramCpuPort::readClock` makes readReady depend on a fully known read latch.
The Machine verification adapter also refuses to hand an unknown byte to the
concrete Z80 core. Waiting cannot initialize the backing byte, so this
verification condition remains blocked. The knowledge mask is simulator
metadata, not a physical DRAM pin; this does not prove a hardware WAIT fault.
No read-completion, CPU, PROM, WAIT or controller logic is changed here.

## Explicit concrete power-on state

`Machine::initializeDramPowerOn(nonzero32BitSeed)` selects one reproducible
physical-memory realization before tick1. It fills only the modeled DRAM window
2800..7ffff using xorshift32 shifts13,17,5 and the low byte per increasing
address, with known masks255. ROM and local SRAM overlays remain untouched.
This is an explicit simulation initial condition, not a measured chip-specific
power-up pattern, RAM clearing by firmware, or an assertion that real EII RAM
starts randomly with this distribution. A seed represents one possible initial
state; it is not generated again when an unknown read occurs.

Default construction still retains unknown masks. Zero seeds and calls after
the first tick are rejected before mutation. Initial-state selection is not
counted as a CPU/DMA write and does not patch any media/firmware/control state.
The16 startup trajectories and all-history read/write agreement remain active.

## Comparative observations

| Startup | Budget | DMA transfers | CPU DRAM reads | LCD / result |
| --- | --- | --- | --- | --- |
| unknown |15M maximum, bounded early stop|28720|3542|Rev3.0, blocked read98c0|
| seed1 |24M|55558|8949|This Will Take / A While, loading continues|
| seed305419896 |24M|55558|8965|same loading display, loading continues|

The first98c0 values areE5 and8E respectively, so this is not a zero-memory
shortcut. Both choices pass the original blocker, retain actual FM/SIO CRC and
strict DMA writes, and continue to new disk blocks. Timing and serial shutdown
traffic can differ because the old parameters differ. No claim is made that
all power-on realizations converge or that the complete OS/bank has loaded.
The24M comparisons used an inline implementation of the same initialization
loop before extraction into the API. Final API unknown-control and seed1
integration trials are recorded separately; the reference-byte/range/guard
unit test verifies the extracted method. Logs escape nonprintable serial bytes.

## Reproduce

The documented WR3 D3 restart from PR1 is selectively integrated in main:
`src/disk_sio.h`, `src/sio_sync_receiver.h`, the receive regression tests and
CRC documentation. The PR's HFE/LCD helper additions are not integrated;
`docs/PR1_REVIEW.md` records their outstanding defects. The PR is not merged.

```sh
bash tools/build-native.sh
build/dram_startup_probe ../roms/main.bin ../roms/scanner.bin \
  ../roms/ic135.bin ../roms/ic137.bin ../upload/emuiios.v30.emuiifd \
  15000000 unknown
build/dram_startup_probe ../roms/main.bin ../roms/scanner.bin \
  ../roms/ic135.bin ../roms/ic137.bin ../upload/emuiios.v30.emuiifd \
  24000000 1
```

The probe is observational; exit0 means a bounded run finished, not a boot pass.
It reports its stop reason, mapping, DMA writes, recent PC changes and normal
Machine status. Known ranges and arbitrary serial captures are metadata only.
No ROM, disk image, RAM dump or disassembly is committed. Exact asset hashes
and source/observer provenance are in validation/os30-dram-root-cause.json.

Native focused checks cover power-on initialization, existing unknown-data
stall semantics, real-Z80 DRAM/WAIT and byte DMA, and the adopted SIO receive
regressions. No new Windows, CMake or full-suite run is claimed.

Next: follow a complete bank load with the explicit startup choice, then locate
any next unsupported hardware request. Keep strict unknown-state verification
available, and investigate any later loss of known data instead of filling it
on demand.


Checkpoint0.33 continues these explicit-startup trials to preset readiness and
physical panel response. See OS30_BOOT.md; the default16-trajectory reference
is still bounded to80M and no full unknown-startup completion is implied.
