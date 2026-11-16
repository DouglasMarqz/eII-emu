# 0.72: host CPU placement, native-audio deadlines, profile-guided build

Owner report on 0.71: speed 91–99%, machine thread one full core, worse in
SSM192k. Measured on the owner's Ryzen 9 5900X (24 logical CPUs) with a
headless bench: original firmware boots `18 Grand Piano #2.hfe` (200M ticks),
then 2 s idle, 8 held MIDI notes, release. Speed = emulated / wall time.

## 1. CPU placement (`host_cpu_placement.h`)

Windows placed the machine thread and the SSM2045 DSP worker on the two SMT
siblings of one core. Same binary, SSM96k: default 0.87x, affinity on two
physical cores 1.12x, forced siblings 0.85x. The machine and DSP owners now
select two physical cores of one L3 group with soft Windows CPU Sets (no hard
affinity, process mask, priority or registry change). `EII_CPU_PLACEMENT=0`
disables it. SSM96k+native+S3528: 0.82x → 0.96–1.03x; SSM192k: 0.73x → 0.99x.

## 2. Native audio deadlines

The collector and the phase scheduler were called every CPU tick. They now
run only when a frame boundary or phase event is due (same edges). Linux
GCC bench, SSM192k+native+S3528: memory/DRAM/scanner hashes, 2,880,000
native frames and DSP audio hashes identical to the parent.

## 3. Profile-guided Release (`tools/build-win64-pgo.ps1`)

Same sources and semantics; clang uses a training profile (SSM192k+native+
S3528, then Legacy) for layout and inlining. Training needs user-supplied
ROMs/bank and stores nothing in Git. Run `build-win64.ps1` for the tests.

| Bench, 8 held notes | non-PGO | PGO |
| --- | ---: | ---: |
| SSM192k+native+S3528 (ABBA) | 1.02, 1.01 | 1.30, 1.28 |
| Legacy | 1.11 | 1.38 |

Legacy hashes (memory/known/scanner/audio) are identical between non-PGO,
PGO and the Linux GCC build. Remaining before 0.70: the native-DAC commit
`1568696` costs about 4% even with native audio off (single-pair noise is
about ±3%). Owner Windows GUI confirmation (2026-10-22): EII-0.72-pgo.exe runs well, "mini golden".
