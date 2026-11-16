# OS3.0 null-bank boot and physical panel path, checkpoint0.33

The original OS3.0 disk now reaches `P01 NULL PRESET` and continues handling
interrupts and controls in two explicit startup experiments. Firmware, disk,
PROM images and emulator core are unchanged from0.32. This is a headless
firmware boot with a nominal read path, not playable audio or a completed GUI.

## Conditions and scope

Both runs select a reproducible DRAM power-on realization before tick1. One
uses seed1/controller microcounter phase0; the other uses seed305419896/phase15.
These were predetermined initial-condition experiments, not measured EII
power-up states. The existing ControllerTiming constructor supplies the single
phase; all upper-control histories inside that phase remain active. There is
no mid-run history pruning, patched firmware, injected sector, CRC override,
WAIT bypass or forced quiet channel request.

The default still retains unknown DRAM and16 controller startup trajectories.
The16-trajectory seed1 reference was followed to80M ticks without a new stall:
224286DMA transfers,223906strict DRAM writes,9005CPU DRAM reads, and
`This Will Take / A While`. Its DMA counts at2M progress boundaries match the
single-phase trials over this prefix; CPU/WAIT counts differ, so they are not
claimed cycle-equivalent. Full16-trajectory boot beyond80M and all16 concrete
phase/seed combinations have not been verified.

All trials use main2.1/scanner3.1, both original PROMs, drive0 initially at
cylinder37/rotor400000, nominal HxC gap profile and LS22187/readpulse8 source
edges. Timing/flux/calibration limits in FLOPPY_FORMAT.md remain applicable.

## Completed transfer audit

At200M ticks, the phase15/seed305419896 pure-boot trial reports:

| Observation | Result |
| --- | --- |
| LCD first observed ready | by178900000ticks, `P01 NULL PRESET` / blank |
| DMA transfers |520552 |
| Payloads / actual firmware RR1 checks |145 /145, no CRC errors |
| Actual payload bytes on destination bus |519680, all equal to selected image bytes |
| Strict DRAM payload writes |518144, all known/equal at transfer completion |
| Payload writes discarded by ROM overlay |1536, bus bytes still checked |
| Other DMA transfers |872, non3584-byte/header transfers |
| Unique model DRAM bytes touched |514048, every address2800..7ffff |
| Final non-CPU-modified bytes compared |514043, zero mismatches |
| Final bytes last modified by CPU |5, excluded only from final image comparison |
| CPU state |normal0035/0038 dispatch idle and interrupt activity; no held DRAM cycle |
| Control bus |24/24 CV holds known;112728DAC/230153mux writes |

The pure phase0/seed1 trial independently reaches the same preset screen and
520552DMA transfers. It had no per-byte oracle; the audited phase15 run and
phase0 panel run provide that evidence. The complete source/destination list
is checked against the observed firmware mapping:

- OS sector IDs0..7 -> physical2800,3600,...,8a00.
- Bank/header IDs21..157 -> physical8800 onward in3584-byte chunks.
- The last chunk starts7f800. Its final1536 transfers wrap into the ROM overlay
  and are ignored by the normal write protection; they are not DRAM data loss.

The audit compares the actual DMA destination pins and committed memory with
a read-only oracle indexed by the selected drive cylinder/side and DMA byte
counter. Oracle arrays never drive Machine. Each DRAM write is checked before
later CPU modification. Final comparison separately excludes bytes whose last
writer was the CPU. It does not infer byte equality solely from the screen,
sector checksum or already populated memory.

An observer-only negative control changes the expected first payload byte by
one bit, without altering the emulated machine or disk. The tool exits1 with
one bus mismatch, one RAM mismatch and one final mismatch; the real SIO still
reports valid CRC and the firmware starts. The4M all-phase positive control
exits0 with all3584first-payload bytes equal and a valid RR1 check. This tests
the oracle independently of the unchanged emulation path; the separate SIO
corruption regressions from0.32 remain applicable.

## Filter contact and slider input

The phase0/seed1 panel trial waits for the actual preset screen, then applies:

1. Filter switch closed at178900000ticks through setButton: SBUS33/SCON5.
2. The same switch released800000ticks later (200ms nominal).
3. SliderA ADC IN4 set to2.5V another800000ticks later.

The scanner/PIO/main firmware subsequently reads OS sectors5and6 again into
6b00and7900, then the LCD reports `No Vce in Preset` on its second row. This is
an expected response to selecting Filter on the supplied null preset, not a
filter sound or invented voice. The actual scanner CPU consumes ADC channel4
code128 on111reads after the voltage change. No menu text, key command or ADC
result is injected into firmware state.

With those additional reads, the panel trial ends at527732DMA transfers,
147CRC checks/no errors,526848payload bus bytes and525312DRAM payload bytes,
all matching. Final comparison checks514037bytes, excluding11CPU-modified
bytes; no mismatches. It retains the full514048-byte DRAM coverage.

The draft measured probe's displayChanged flag checked only the first LCD row
and consequently printed0, although the recorded second-row response is
present. The final tool checks both rows; a replay check covers this actual
second-row-only response. The original log and measured source are retained
unaltered. The low-level data audit and emulator behavior are unaffected.

## Reproduce

```sh
bash tools/build-native.sh
build/dram_startup_probe ../roms/main.bin ../roms/scanner.bin \
  ../roms/ic135.bin ../roms/ic137.bin ../upload/emuiios.v30.emuiifd \
  200000000 305419896 15
build/dram_startup_probe ../roms/main.bin ../roms/scanner.bin \
  ../roms/ic135.bin ../roms/ic137.bin ../upload/emuiios.v30.emuiifd \
  200000000 1 0 panel-filter
```

Omitting the phase argument, or supplying `all`, retains16 trajectories. The
panel plan defaults to `none`. All phase/seed choices occur before the first
tick. DRAM_CHUNK lines group every3584DRAM writes for progress; they are not
firmware sector boundaries after a partial ROM-overlay transfer. Use
PAYLOAD_SOURCE and PAYLOAD_RR1 for actual payloads. Exit0 certifies the recorded
byte/CRC audit only, not readiness on arbitrary media or startup conditions.

Logs, source/asset hashes and summary checks are in
validation/os30-boot-provenance.json and validation/os30-boot-*.txt. The measured
panel probe source is archived for provenance; the current tool additionally
corrects the display flag, clarifies chunk naming and fails on CRC errors.
No private disk/ROM/PDF, RAM dump or disassembly is committed. Core and scanner
unit results from0.32 are retained; this checkpoint adds observer, CLI, data
oracle and genuine-firmware panel validation. No new full-suite, Windows,
CMake, sanitizer, physical calibration, host audio or MIDI-performance claim.

Next: thin GUI displaying this real LCD and accepting media/physical panel
inputs; a valid nonempty sound-bank disk is needed to test sample playback.
Keep explicit startup choices and the verification mode distinguishable.
