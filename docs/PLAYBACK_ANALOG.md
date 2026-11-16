# Am6072 playback DAC and analog experiment bridge, checkpoint0.18

The sample-code and amplitude-CV observations from Machine now feed an ideal
Am6072 decoder and a separately clocked, explicit zero-Q analog experiment.
The experiment connects the existing SSM2045 filter/VCA component kernels.
This is not yet the complete output-channel circuit or a host audio engine.
Active deglitch and S3528 stages require external node values by default;
diagnostic direct routes must be explicitly enabled. No production route
silently substitutes a wire for either stage.

## Primary evidence

AMD, 1979 Linear and Interface Data Book, PDF151..158 / printed3-52..59:
https://ftpmirror.your.org/pub/misc/bitsavers/components/amd/_dataBooks/1979_AMD_Linear_and_Interface_Data_Book.pdf
SHA-256: d3b780ee6c3b57b8733e08bac6001a6bcd29a2eaf7e3dcb645bd59d0a0b744e3.
Table3, pinout, decoder circuit and reference-current discussion were read
and visually checked. The book is an archived AMD manufacturer publication.

E-mu service PDF107 / printed7-45: Am6072 pins, LF356 I/V circuit, reference
network, active 3202/TL084 deglitch, R138, C93/R74/R82 and R154/C161 were
checked on enlarged views. PDF27/28 distinguishes VCF, SCF and VCA stages.
The SSM2045 manufacturer's block diagram and response equations were checked
again; see ANALOG_LAB.md. Copyrighted scans are not added to this repository.

## Nominal DAC law and polarity

Raw pin code bits7,6..4,3..0 represent sign, chord C and step S. The ideal
normalized magnitude is:

    N = ((2*S + 33) << C) - 33
    Imagnitude = N * Ireference / 2112

At Ireference528uA, each normalized unit is0.25uA and full magnitude8031
corresponds to2007.75uA. This uses the table's nominal ratio, rather than
rounding the manufacturer's approximate full-scale factor to3.8. It is the
piecewise decoder law, not a smooth logarithm or complemented G.711 byte.
Both raw00h and80h are exact decode zero. SB=1 steers sink current to IOD+
(pin16); SB=0 to IOD- (pin17).

The board connects pin17 to the LF356 inverting input and pin16 to its
noninverting input. Its polarity therefore reverses the signed current
convention used by Am6072::signedUnits:7Fh gives positive andFFh negative
voltage. With a settled5V amplitude CV and nominal parts, the ideal full
magnitude is5.036365147485493V. No clipping, offsets or component tolerance
are claimed. Am6072::decodePins retains the two sink-current components.

R44/R174=4.7k/4.7k and C177=1uF give Ireference=CV/9.4k at DC and nominal
reference time constant2.35ms (ideal reference-amplifier virtual ground).
R17/R8 are nominal2.49k. C187=220pF is across R17 only. The dynamic I/V model
therefore implements:

    Vplus = -R8 * IODplus
    Vout = Vplus + [R17/(1+s*R17*C187)] * IODminus

Applying that RC pole to the combined signed signal would incorrectly filter
the noninverting component as well. The nominal polarity follows the checked
pin wiring; op-amp bandwidth, slew, compliance and the chip's finite settling
are not modeled. Multiplication below the recommended reference-current
range, including exact zero, is an explicit ideal extension.

## Filter and VCA experiment boundaries

The active 3202/TL084 deglitch circuit is not replaced by a passive resistor
chain. An external deglitchedVolts node is required; the diagnostic direct
route uses the modeled DAC output instead. From that low-impedance node,
R138=68k feeds the typical internal900ohm filter input. The diagnostic input
factor is900/(68000+900). R26/R34 are inside the preceding active network;
they are not blindly added to R138 as a passive input divider.

Filter gm in siemens must be supplied at the pin-domain experiment boundary.
The existing two-pair trapezoidal SSM2045 kernel is valid here only at zero
Q-control current. No absolute FC-CV-to-gm calibration, resonance/nonlinearity
or physical switched timing is inferred from the board's held FC/Q voltages.

The four-pole tap ends at the S3528 boundary. reconstructionVolts supplies
that external output; diagnosticDirectReconstruction explicitly substitutes
the four-pole tap for component testing. C93=1uF AC-couples that node through
R74=10k into R82=470. The ideal VCA2-input transfer used by the experiment is:

    [470/10470] * [s*0.01047/(1+s*0.01047)]

The existing small-signal VCA branch takes an explicitly supplied branch
control current. Its current sign remains the documented lab convention.
R154=4.7k/C161=1nF converts that branch current through the existing feedback
impedance kernel. Full mixer bias/current sharing, absolute VCA phase,
S3528 internals and final output-stage calibration remain unverified. This
boundary prevents numerical agreement of a diagnostic cascade from being
mistaken for measured EII fidelity.

Channel0 requires channel0PlaybackConfirmed for this experiment. Its actual
MODE/OE/SAR bus ownership is still outside the model; merely reading its
stored DAC byte does not prove its physical DAC pins are in playback mode.

## API, timing and unknown states

Machine::playbackAnalogInputs() is read-only: it captures the eight stored
DAC-code observations and the existing firmware-driven amplitude-CV holds.
It does not alter ROM execution, CPU WAIT, memory, channel clocks or controls.
PlaybackAnalogBank::process accepts that snapshot and eight explicit pin
control structs. It runs one analysis sample at the constructor's rate.
The caller owns sample timing. There is no automatic host-rate resampler,
audio sink, or calibrated sub-frame DAC-edge timing in this API.

All capacitor states start unknown. resetKnownZero establishes a diagnostic
initial condition explicitly. Unknown input taints the affected stateful
stages and stays unknown until a new initial condition is supplied; a later
known input does not erase an unknown capacitor history. This is conservative
and can retain uncertainty indefinitely. Partial digital codes are enumerated
without selecting an arbitrary representative. Exact signed zeros and a known
zero reference are recognized in the ideal decoder. Channel isolation remains.

Both active diagnostic routes are visible in PlaybackObservation. Leaving
one disabled requires its external node and preserves an unknown downstream
result when that node is absent. Thus the normal snapshot of firmware boot,
which has no known amplitude holds/sample data, cannot produce invented sound.

## Validation

bash tools/build-native.sh builds fifteen targets. playback_analog_tests
checks all256 codes against independent chord-base/step data, all6561 ternary
code masks against compatible-code enumeration, sink-pin steering, both zeros,
reference scaling, board polarity, invalid inputs and unknown/MODE/stage guards.

At100/1000/5000Hz, each sign's CV-impulse response is checked against a separate
closed-form complex transfer product for reference/I-V/input/filter/C93/VCA/
feedback. It distinguishes the two I/V current paths. The finite impulse
window is0.5s to include the10.47ms coupling tail. These are equation tests,
not hardware calibration.

sample_transport_tests also executes real Z80 OUTs for all eight sample
address registers and amplitude-CV holds, then feeds their Machine snapshot
into the diagnostic bank with explicit gm/current/route/initial-condition
settings. All eight DAC voltages settle to their nominal code/CV values and
the filter/VCA/feedback observations remain finite and known. The test uses
synthetic PROM/sample fixtures; this is not OS playback or a boot workaround.

## Checkpoint0.19 continuation

See [SWITCHED_ANALOG.md](SWITCHED_ANALOG.md) for the phase-resolved3202/TL084
kernel, firmware S3528 write ports, exact programming/divisor interface and
explicit reconstruction surrogate. These can supply the required external
nodes above. Default bank routing remains unchanged; full host audio and
calibrated S3528 behavior remain pending. The native suite now has16 targets.
