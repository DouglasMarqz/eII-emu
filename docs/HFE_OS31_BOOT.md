# 0.34: strict HFE import and original OS3.1 Grand Piano boot

The user's `18 Grand Piano #2.hfe` boots the original firmware through
`Software Rev.3.1` to `P01 GrandPiano 1` at178900000 CPU ticks.
The unchanged0.32 core executes both CPUs, nominal FM read circuit, SIO,
DMA, controller and strict DRAM/WAIT. The0.33 passive observer runs200M ticks.
No firmware patch, sector-to-RAM injection or CRC override is used.

This is a conditional startup trial: RAM seed1 and controller phase0, supplied
before tick1. Default unknown RAM and16-phase verification remain unchanged.
A loaded preset display is not evidence of working key-to-audio playback.

## Input and container correction

Original input2540544bytes, SHA256
`527c480304002b37ebc1a7c6f90518c21a1ea41f0403465aeb71d365cebfaf8a`.
HFEv1 revision0,80cylinders/two sides, E-mu FM encoding3/interface0b,
312kbit/s header,31250combined bytes per cylinder. Each side has15625bytes.
The original attachment is unchanged and excluded from Git.

The earlier PR1 helper interpreted each track as concatenated sides. HFE
stores alternating256-byte side chunks within512-byte blocks. The new
`floppy_hfe.h` reads that layout and validates padded storage, LUT bounds,
track non-overlap and supported format. Primary layout reference:
https://hxc2001.com/floppy_drive_emulator/HFE-file-format.html
and its track-data diagram. This implementation is independently written.

Supported sampled FM has one zero alternate sample lane; either active lane
is accepted. Circular decoding verifies FM clocks, physical ID, header CRC,
data CRC, bounded inter-field gap and exactly one complete valid sector per
side. Complete EMUIIFD export requires80cylinders/all160sectors; missing,
corrupt or ambiguous sectors fail. A rejected input cannot open/truncate the
requested export. Input/output file identity is rejected.
Unsupported pulse packing, HFEv2/3, weak-bit behavior and timing replay are not
implemented. This imports payloads and the existing runtime synthesizes its
nominal FM timing/gaps; it does not replay original HFE transitions.

## Independent checks

`tools/hfe_verify_lab.py` is a separate bytes-search Python decoder for the
exact80-track fixture; it does not invoke the C++ importer. Both decode all160
physical sectors with valid header/data CRC and produce identical573440bytes:
SHA256 `71462c5c8cf4de721b00f61a2bc47d0d1f3c5a415d84f795f8bfb87aed551947`.
Per-sector hashes/positions are in validation/hfe-grand-piano-sectors.json.

`hfe_tests` independently writes FM frames and computes CRC through MSB-first
polynomial division; published123456789 check isBB3D. Tests cover all160 side
payloads/interleaving, both sample phases, circular origin crossings, CRC and
clock corruption, physical-ID mismatch, duplicate/missing sectors, truncated
metadata/storage, overlap and unsupported formats. Native GCC and ASan/UBSan
pass. LeakSanitizer cannot enumerate processes in this environment; rerun uses
ASAN_OPTIONS=detect_leaks=0, so no leak-check claim. CMake targets were added
but cmake/ctest are unavailable here; native builds exercised the same sources.
No Windows or new whole-project suite run is claimed.

A one-bit payload mutation in a temporary COPY of the real HFE is rejected;
preexisting export sentinel remains intact. Exporting over a temporary input
copy is rejected with its hash unchanged. The original attachment hash is
rechecked unchanged. See validation/hfe-validation.txt.

## Real firmware load result

At200M ticks:520552 DMA transfers, including519680 payload-bus bytes and872
header/other transfers.145 actual post-payload RR1 reads report0CRC errors.
Every518144 strict DRAM payload write is fully known and equals the input;
1536 final payload bytes wrap into protected ROM and are ignored normally.
Bus/RAM/unexpected/final mismatch counters are all0. All514048 modeled DRAM
addresses2800..7ffff are touched; final514043 bytes match and5 later CPU-modified
addresses are excluded from final comparison (their DMA writes were checked).
LCD remains P01 GrandPiano1, normal dispatch/IRQ activity continues, no held
DRAM request. The observer and emulator core are unchanged from0.33.
The bank label differs from the attachment filename; it is reported literally
from the executing firmware. No waveform identity or audible quality claim.

## Reproduction (private assets kept outside repository)

```
g++ -std=c++20 -O2 -Wall -Wextra -Isrc tools/hfe_probe.cpp -o build/hfe_probe
g++ -std=c++20 -O2 -Wall -Wextra -Isrc tests/hfe_tests.cpp -o build/hfe_tests
build/hfe_tests
build/hfe_probe '../upload/18 Grand Piano #2.hfe' /tmp/grand-piano.emuiifd
python tools/hfe_verify_lab.py '../upload/18 Grand Piano #2.hfe' /tmp/grand-piano-independent.emuiifd /tmp/grand-piano.json
cmp /tmp/grand-piano.emuiifd /tmp/grand-piano-independent.emuiifd
build/dram_startup_probe ../roms/main.bin ../roms/scanner.bin ../roms/ic135.bin ../roms/ic137.bin /tmp/grand-piano.emuiifd 200000000 1 0
```

Next: expose the actual LCD/media/panel in the thin GUI; establish verified
keyboard contacts and first firmware-triggered sample playback using this
nonempty bank. Preserve explicit startup assumptions and analog limitations.
