# 0.46 complete panel route audit

The0.45 user saw Filter/Stop, Special/Tune, VoiceDef/Dynamic and
Realtime/Record LED cross-coupling, plus independent button mismatches such as
Play/Define and Edit/Realtime. Two independent wiring mistakes explain this.
Do not derive input contacts by swapping the observed LED partners.

## Correct electrical routes

Service PDF69 (sheet7-6),66 (7-3),67 (7-4),65 (7-2) and71 (7-8) were inspected
visually, including enlarged decoder, wire junctions and connector labels.

| Decoder output | Canonical scanner I/O | Connector clock | Physical latch | Width | Logical LEDs array |
| --- | --- | --- | --- | --- | --- |
| IC47 Y0 |B0|CN3 pin28|IC1 right|8 bits|2|
| IC47 Y1 |B2|CN3 pin27|IC3 left1|6 bits|1|
| IC47 Y5 |BA|CN3 pin26|IC2 left0|6 bits|0|

Previously Y0 was stored in left0 (with incorrect6-bit truncation) and Y5 in
right. The0.45 LED name-to-physical-latch map was correct, but its upstream
I/O decoder-to-latch assignment was wrong. It is now corrected at the capture
point, retaining unknown bits and rising-strobe capture behavior.

Separately, service7-4's eight left contacts Select,Stop,Record,Play,Define,
Edit,External Clock,Setup run on SBUS40/SCON0..7. The previous model reversed
SCON7..0. This caused actual function swaps before any LED rendering. The0.45
numeric/YES/NO and row33 correction remains valid; right module contacts remain
valid. The keyboard LS42 logic and non-inverting input buffers were rechecked.

`PANEL_WIRING.csv` is the canonical30-row review table. It lists GUI identity,
SBUS/SCON input and independently the LED's I/O port, physical chip and bit.
LED bit numbers never determine a button's input contact. Not every button has
a lamp, and firmware need not illuminate a lamp after rejecting a function.

## Validation

Unit tests now cover all30 named contacts, not just raw matrix cells. The19
LED tests write actual port strobes and check the named lamp, other lamps,
active-low polarity and unknown state. They no longer test only a manually
constructed latch-array image. Alias/width tests explicitly distinguish
B0's8-bit latch from the two6-bit latches. Real scanner Z80 OUT/IN tests and
worker/pacing tests pass.

`native_panel_audit ROMS HFE` boots the original bank once to200M CPU ticks.
Each of30 POSIX child processes starts from the identical copy-on-write machine
state, presses exactly one named button for200ms and observes3s of firmware.
This is process isolation for a native validation tool, not a firmware bypass.
`python3 tools/check_panel_audit.py CAPTURE` checks recorded LCD/LED output:
all10 numeric identities,11 steady module lamps and distinct function prompts,
all with production invalid0. It also records the expected No files exist
responses for sequencer actions on this bank, which has no sequences. That does
not exercise every sequencer operation on populated sequence files.

Observed examples: Define -> Create Seq# and Define lamp; Tune -> TUNING and
Tune lamp; Special -> CatalogFunctions and Special lamp; Filter -> No Current
Voice and Filter lamp; Realtime -> channel assignments and Realtime lamp.
All30 observations and checker result are in validation/panel-routing-046.txt.
No additional sustained traces or whole-LCD redraws are enabled. Manual capture
now includes BA/IC2,B2/IC3,B0/IC1 raw LED value/known masks.

Win64 Release LLVM-MinGW20261006 O3/LTO/NDEBUG/DIAG0 crossbuild0warnings.
The0.46 Windows UI/runtime still needs user verification; native firmware
routing and raw CPU I/O tests do not substitute for that. No audio/core timing,
ASIO or analog-model changes are included. The audio queue backlog and ASIO
remain recorded follow-up work. The0.45 document is historical, superseded by
this corrected decoder and full left-contact route audit.
