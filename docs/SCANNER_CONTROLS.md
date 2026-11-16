# Scanner ADC, panel contacts and LED latches, checkpoint0.24

The scanner's ADC0809 placeholder has been replaced by a clocked nominal
pin model. Actual Z80 bus strobes select inputs and start conversions; EOC
feeds scanner CTC channel3. Switch contacts pass through the PIO-B row decoder
and column buffer. No firmware patches, MIDI shortcut or host GUI is involved.
This control ADC is separate from the unfinished channel0 audio-recording SAR.

## Sources checked visually

- EII service PDF69/71/72, printed7-6/7-8/7-9: decoder, ADC0809,
  PIO-B and CTC3 wiring.
- PDF65/66/67/68, printed7-2..7-5: diode-isolated panel contacts,
  LED latches, sliders and wheels. PDF116/7-54 confirms keyboard contact
  diodes; no key velocity/contact-timing API is implemented here.
- TI/National ADC0808-N/ADC0809-N SNAS535H, March2013, pages4..7:
  transfer thresholds, ALE/START/abort, output latch and EOC timing.
  https://www.ti.com/lit/ds/symlink/adc0809-n.pdf
  SHA256:67ef33bca3d5b74ff2671b8351292659e2746fd2bb2ebfd72c2632ee84aa57d6.
  This is a later manufacturer reference, not proof of the installed chip's
  exact revision/timing. Source PDF remains outside the source repository.

## ADC behavior and explicit approximation

ADC address pins A2..0 receive SD2..0. Inverted -SCSWC drives ALE and START;
ALE rising captures the address, START rising resets/aborts the internal SAR,
and START falling starts the new conversion. The prior output register is
retained until the new result is ready. Inverted -SCSRC controls OE; OE low
is a floating output, distinguished from a driven unknown result by its pin.
There is no CRST connection, so result/address/EOC start unknown and Node
CPU reset does not silently clear them.

The board's -C500K supplies500kHz: one ADC clock per8 CPU ticks. The
simulation uses an explicit nominal source phase at its time origin; physical
power-up phase/clock-buffer skew are not characterized. A source edge settles
before coincident CPU bus events. Constant-input tests cover all8 relative
CPU/source offsets. This does not infer a physical absolute starting phase.

The nominal conversion takes64 ADC clocks, consistent with the published
100us typical at640kHz (128us at500kHz). The manufacturer specifies a range,
not an exact chip latency. Eight nominal decisions occur at clocks7,15,..63;
clock63 publishes the result and clock64 raises EOC. The one-clock separation
is specified by TI; the precise internal decision phases are a chosen model.
EOC goes low on the first clock after START rising, within TI's0..8-clock
range. Internal startup/decision delay tolerances, chopper ripple, comparator
offset/INL and physical clock/bus propagation are not calibrated.

The transfer is nearest code in256 equally spaced steps, saturated at0/255:
first transition0.5LSB above0V, then1LSB steps. It is not floor(255*VIN/5).
Each SAR decision reads the live selected pin: no invented input sample/hold.
Changing voltage within a conversion therefore affects the remaining bits,
with the chosen decision schedule's timing limitation stated above. Invalid
nonfinite/out-of-rail known voltages are rejected, not silently clipped.
Unknown voltages/addresses retain possible SAR outcomes, merged by consensus;
unknown-address correlation over successive decisions is conservative.

## Controls API

`Machine::scannerControls` owns the model. `setVoltage(ScannerAnalogInput, V)`
sets ideal voltage directly at the ADC pin; `adc.setInput(channel,{V,known})`
also permits an unknown source. The standard reference is the nominal EII5V
and0V supply. No mechanical travel/trim calibration or resistor/capacitor
settling is implied by setting a pin voltage.

| ADC input | Source | Initial external/UI value |
| --- | --- | --- |
| IN0 | A/D IN network | 0V |
| IN1 | input envelope network | 0V |
| IN2 | left wheel | 2.5V |
| IN3 | right wheel | 0V |
| IN4..7 | sliders A..D | 0V |

These are chosen released/parked external controls, not recovered hardware
positions or forced internal startup data. Input networks/clamps, slider/wheel
trim and0.1uF capacitor dynamics remain unmodeled. Volume/input-level pots in
the audio signal path are not ADC channels and are not implemented by this API.

## Matrix decode and named buttons

`setContact(SBUS,SCON,pressed)` connects one diode-isolated switch. Valid rows
are0..15,32,33,40,41, with eight columns0..7; open contacts read the4.7k
pull-ups. The keyboard's two contacts can be driven independently through
these raw rows, but no key/velocity mapping or automatic contact delays are
claimed. The two foot-switch transistor circuits are not separately modeled.

PIO-B bits5,3,2,1,0 feed OR/inverter gates and two decimal74LS42s. The
selected bank is PB3, code=(PB5?8:0)|(PB&7). Codes0..7 address keyboard
rows0..7 or8..15; codes8/9 address32/33 or40/41. Codes10..15 select no row.
PB4 (serial RX) and PB6/7 (gain outputs) do not select rows. Mode3 direction
masks determine physical PIO pins, not the last raw output byte alone.

`setButton(ScannerButton,pressed)` maps all30 panel switches:

| Row | Column7 | Column6 | Column5 | Column4 | Column3 | Column2 | Column1 | Column0 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 40 | Select | Stop | Record | Play | Define | Edit | Ext clock | Setup |
| 41 | Disk | Sample | Preset definition | 0 | 9/Yes | 8 | 7/No | 6 |
| 32 | VCA/LFO | Special | Voice definition | 5 | 4 | 3 | 2 | 1 |
| 33 | Enter | Real time control | Filter | foot circuit | foot circuit | Transpose | Tune | Dynamic allocation |

**Schematic discrepancy:** left-panel7-4 calls CN3 pin8 `SBUS42`; digital7-8
calls the SAME pin `SBUS40` and wires it to IC3Y8. The implementation follows
that connector/decoder wiring, with row40 for Select..Setup. It does not invent
an additional row42. Panel return columns follow connector pins15..22. No
switch bounce, diode-drop or analog contention law is fabricated.

## Bus strobes and LEDs

IC47 qualifies /IORQ plus /RD or /WR with A6 low; A3..1 selects outputs.
Its A7/A5/A4/A0 aliases exist electrically. Firmware's nonoverlapping panel
ports are30..3F/B0..BF; odd/even aliases apply except LCD's A0. Existing
CTC/PIO address abstractions do not fully model simultaneous chip-selection
or bus contention on other aliases. Raw panel strobes are tracked there and
`overlappingBusCycles` reports these accesses; this is not full bus arbitration.

SCSWL0/1/2 release captures left0/left1/right LED outputs,6/6/8 bits with
unknown initial contents, exposed as `leds[0..2]` with known masks. Reads of a
LED strobe capture an unknown undriven data bus. Drivers/LED optics and the
metronome one-shot remain outside this model. ADC OE and START track the
actual held bus pulse rather than an instantaneous software OUT event.

The Z80 core has no unknown-data mask. Any unresolved ADC bits read by the
CPU are exposed as1 at that boundary and `unknownCpuReads` records the
transaction; the ADC model retains its actual known mask. Initial EOC is
unknown; the CTC sees low until a known level exists, without a fake startup
completion interrupt. These boundary policies are observable limitations,
not measured electrical pull-up behavior. Normal parked-input firmware must
be checked for zero unresolved reads and zero overlapping decoder cycles.

## Validation

`scanner_controls_tests` checks2048 channel/code conversions,765 probes below/
on/above every half-LSB threshold, full-scale rail, live input, ALE/START
edge/hold/restart, unknown states, OE and data-before-EOC. All256 PIO-B values
are compared to an independent OR-gate/decimal-decoder reference, with every
20x8 contact exercised. Named Select/Zero/Filter and the SBUS40 discrepancy,
multiple held contacts, LED release/aliases and invalid-input guards are covered.

Real scanner-Z80 programs use all eight ADC inputs and four benign port
aliases, read contacts via actual PIO mode3 outputs/direction, and write all
three LED latches. Another configures CTC3/IM2, holds completion pending under
DI, then handles exactly one EOC interrupt, reads the selected voltage and
returns through RETI. Full-suite/sanitizer/firmware results are recorded in
VALIDATION.md and validation/. No OS boot, GUI, physical calibration or audio
recording claim follows from these scanner checks.
