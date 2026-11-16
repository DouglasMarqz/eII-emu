# 0.48: sample DRAM beneath CPU-local ROM/SRAM

User report: stack bank, preset02, production invalid1/code4 at CPU747251635,
controller edge1868129087, phase14, channel1, control38986. The screenshot
confirms an unknown/missing DRAM byte at CAS; DMA is disabled/not granted.
The report is a snapshot, not an input-event replay.

## Reproduction and physical cause

With the caller-supplied same bank, a deterministic preset02 note/wheel probe
reproduces code4 at CPU207283456, edge518208639, phase14, channel0,
control38986. The failing sample CAS address is2096 =0x00830. Different
channel/time reflects a different input sequence; this reproduces the failure
class, not the exact user's sequence. Boot transfer count520552 matches the
reported bank boot. See tools/preset02_fault_probe.cpp.

The model previously used one backing array for CPU ROM/SRAM overlays and
visible DRAM. Explicit concrete power-on initialized only0x02800..0x7ffff;
addresses below0x02800 retained unknown DRAM masks. Sample addressing could
therefore reach a physically installed bank0 byte that the model treated as
uninitialized/missing, even in the explicitly seeded startup realization.

Service-manual sheets7-23..7-26 were visually checked: RAM bank/RAS selection
uses the CPU or SUM16..18 path, the row/column mux uses CPU or SUM0..15, and
bank0 is eight4164 devices with all eight multiplexed address pins. The sample
ALU path does not pass through the CPU-local ROM/SRAM read decoder. The CPU
address-map overlay does not remove those physical DRAM cells.

## Change and limits

Keep existing CPU ROM and local SRAM backing and semantics. Add a distinct
0x2800-byte physical DRAM underlay with its own knownness masks. Both the
reference DramBackingView and production CAS reader select this underlay for
low DRAM addresses. CPU-local reads/writes still go to ROM/SRAM; a CPU SRAM
write cannot change sample-addressed DRAM beneath it.

The explicit seeded power-on initializes visible DRAM in the original byte
order, then the hidden region, preserving existing visible-window seed bytes.
This is a reproducible arbitrary power-on realization, not measured RAM data.
Unseeded/default underlay remains unknown. Actual-CAS unknown-byte rejection
and production invalid-state stop remain active. No zero-fill fallback, live
reference switch, firmware patch or skipped memory read was introduced.

First-CAS diagnostics now retain physical address and CPU/sample ownership.
Sample-recording writes into the hidden DRAM region remain unimplemented with
the rest of the recording transport. No new analog calibration, ASIO or audio
FIFO policy is included. The0.47 physical panel and continuous-input fixes are
retained. Windows runtime and audible behavior still require user verification.

## Checks

The new asset-free test exercises actual reference/production sample RAS/CAS
and IC40 latching at0,1fff,2000,27ff,2800,7ffff; CPU ROM/SRAM isolation; repeatable
seeded startup; and rejection of a deliberately unknown underlay byte. Full
source ASan/UBSan execution of that test passes (leak detection disabled).

Existing seeded power-on reference bytes, CPU WAIT/DRAM across8banks/16phases,
sample transport, digital CPU/PIO/IRQ, production failure guards and host worker
regressions pass. Standalone diagnostic-enabled differential tests pass200000
DRAM-port steps and20000 controller slots. The DRAM-port CMake target is now
standalone with diagnostics enabled, as required by its existing test accessors;
this avoids mixing it with the diagnostic-disabled production core.

Caller-owned media reproduction:

```
c++ -std=c++20 -O3 -flto -DNDEBUG -DEII_DIAGNOSTICS=0 -pthread \
  -Isrc -Ivendor/chips src/chips.cpp src/machine.cpp \
  tools/preset02_fault_probe.cpp -o /tmp/preset02-fault-probe
/tmp/preset02-fault-probe ROM-FOLDER BANK.hfe
```

The probe boots200M ticks, selects02 via real scanner contacts, then sends672
note-on/off pairs across eight rounds with deterministic wheel positions. It
renders the actual provisional audio path to a peak/frame-count observer, not
a host audio device. Native wall times are not Windows performance measurements.

The fixed original-bank run completes all eight rounds at339200000 CPU ticks
without a production fault; the baseline stopped at207283456. This is672
note-on/off pairs with wheel changes, not an exact replay of the user. Fresh
Win64 LLVM-MinGW20261006 Release crossbuild has0warnings, PE32+x64 GUI;
Windows audio/input/runtime still unverified. Manual state capture also retains
the exact STOP REASON after the error dialog has been dismissed.
