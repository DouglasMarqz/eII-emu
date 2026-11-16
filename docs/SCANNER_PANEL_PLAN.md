# Scanner controls: historical starting point, checkpoint0.23

Checkpoint0.24 implementation and current limits are in SCANNER_CONTROLS.md.
The placeholder description below records the pre-implementation state.

Service PDF71 / printed7-8 (Scanner PB & ADC) visually checked on2026-10-18.
This is a wiring inventory and implementation plan, not completed peripherals.

Two separate conversion tasks must remain distinct:

- Scanner IC27 ADC0809 reads eight control-voltage inputs: A/D IN, ENV,
  left wheel, right wheel and sliders A/B/C/D, respectively IN0..IN7.
  The A/D IN/ENV input networks require their own voltage/scaling check.
- Audio recording uses the channel0 MODE/SAR/shared DAC bus path. It is not
  performed by this panel ADC0809 and remains unimplemented.

ADC0809 data connects to SD7..0; address A2/A1/A0 connects to SD2/SD1/SD0.
The inverted -SCSWC signal drives ALE and SC, inverted -SCSRC drives OE.
EOC returns on +SCI; the clock input is -C500K. Precise pulse, conversion,
EOC and retrigger behavior must be taken from the manufacturer datasheet and
checked against the scanner decoder/CTC wiring before implementation. Do not
replace the current delay with an assumed ideal instantaneous conversion.

The switch matrix is separate: IC3/IC4 74LS42 decode scanner PIO-B signals
to active-low row outputs; IC5 74C244 returns SCON7..0 on SD7..0 while
-SCSRPB enables it. RN1 4.7k pull-ups define the unpressed column levels.
Individual panel/key-contact wiring and row-selection Boolean equations
need enlarged schematic verification; no GUI note-on shortcut is implied.

Current machine.cpp placeholders:

- Scanner ADC read aliases `(port & 0x7e)==0x34` return adcValue.
- ADC write aliases0x36 ignore the selected channel, set128 and schedule
  completion512 CPU clocks later.
- Matrix read aliases0x38 always return255 (no pressed contacts).

Next controls work: verify decoder aliases/clock/EOC-to-CTC connection and
ADC0809 source specifications; implement a separately testable clocked ADC
with explicit eight input voltages and reference limits. Then connect the
real scanner write/read strobes, row decoder and contact matrix, retaining
actual firmware polling/interrupt behavior. Test channel selection, completion,
hold/restart, matrix row selection and all port aliases before a thin GUI.
No ADC, switch or knob behavior was implemented by checkpoint0.23.
