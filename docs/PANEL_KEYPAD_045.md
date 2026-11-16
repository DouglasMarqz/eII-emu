# 0.45 keypad wiring and firmware LEDs

Historical: superseded by PANEL_ROUTING_046.md for decoder Y0/Y5 and the first eight button contacts.

The user reports0.44 stable100% speed and no reproduced earlier freeze. Disk
operations instead reach Insert Libr Disk / and Press Enter on a bank that
booted successfully; YES and NO are also reversed.

## Root cause and correction

Service manual sheet7-4 (PDF67), verified visually: row41 has0,9/YES,8,7/NO,6
on SCON0..4. Row32 has5,4,3,2,1 on SCON0..4. Row33 has Transpose,Tune,Dynamic
Allocation on SCON0..2. The previous named mapping reversed those columns.
The first row of eight transport controls and the right module contacts are
unchanged. All ten digits and the three row33 contacts now have explicit
per-contact tests with release-to-pull-up checks.

Reproduction on the caller-supplied original Grand Piano bank:
old Disk -> named1 actually selects5, Catalog Voice, then asks Insert Libr Disk.
The original performance bank is not a Library disk. Corrected Disk ->1 starts
This Will Take / A While and returns to P01 GrandPiano1 after491828 DMA bytes.
The bank_reload_probe deliberately changes one sample-RAM byte before reload;
the firmware/media path restores the baseline hash of0x10000..0x7ffff. This
proves actual reload, not merely retaining the previous preset display.
The probe is specific to this original fixture; assets are never embedded.
An initial provisional test threshold expected500k transfers and was too high
because normal bank reload does not include the OS boot payload. The final
check instead verifies completed DMA, correct LCD and sample-memory restoration.

Owner manual pages139/144: Disk1 loads a performance Bank; Disk5 catalogs a
Library disk's voice files. Enter confirms a requested media replacement and
does not choose the Disk menu function. For the loaded Bank's preset list use
Preset Definition19, then Slider A (owner page112). No file-type checks or
firmware responses are bypassed.

## LEDs

The existing three firmware LED latches now drive19 button lamps. Service
sheets7-2/7-3 (PDF65/66) and connector pins were visually checked. LEDs are
active low. Unknown power-on states remain gray, known off is dark red and
known on is bright red. No lamp is inferred from a click or selected menu.

| Latch | Bits | Lamps |
| --- | --- | --- |
| left0 IC2 |0..5|Play,Record,Stop,Transpose,Tune,Dynamic Allocation|
| left1 IC3 |0..3|Setup,External Clock,Edit,Define|
| left1 IC3 |5|Preset Definition via CN1 pin3|
| right IC1 |7..0|Sample,Disk,Voice Definition,Special,VCA/LFO,Filter,Real Time,Enter|

The19 wired controls use native owner-draw buttons with keyboard focus and
pressed/disabled rendering retained. Only changed LED bits invalidate their
button; the LCD still redraws only for LCD changes. There is no extra worker
trace or polling thread. Unwired keypad/Select buttons do not invent lamps.

Validation: scanner ADC/matrix/LED real-Z80 tests PASS, including all keypad
pins and all19 named LED routes, active-low/unknown/independence checks. Release
worker/pacing tests PASS. Real firmware reload probe PASS through416M CPU ticks.
Win64 LLVM-MinGW20261006 O3/LTO/NDEBUG/DIAG0 crossbuild0warnings; Windows visual
and runtime verification of the new lamps remains with the user.

The user's capture still has6752 queued audio frames (~140.7ms). Queue latency
and device-clock drift remain follow-up work; this patch changes keypad input
and LED presentation only. ASIO is a separately requested backlog item.
