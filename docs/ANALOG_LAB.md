# SSM2045 component laboratory (checkpoint 0.4)

This is a separately testable small-signal component kernel. It is not a
completed SSM2045 model, an EII output-channel simulation or a playable sampler.
Machine does not invoke it, and no ROM or sample data is passed through it yet.

## Primary evidence

- User-supplied E-mu service manual, PDF page 27 (printed 2-17), page 28
  (output-channel block diagram), and page 107 (channel 1 schematic 7-45).
  Circuit labels and capacitor/resistor values were visually checked on
  rendered enlargements; OCR was not used as circuit authority.
- Solid State Micro Technology for Music, SSM2045 Music Voicing System,
  six-page 1983 manufacturer sheet, especially its block diagram,
  specifications, and final-page response equations. Public scan:
  https://www.ka-electronics.com/images/SSM/SSM2045.pdf

The copyrighted scans are not added to this source repository.

## Verified channel topology

The output-channel drawing places the 6072 DAC before the SSM2045 filter.
Its four-pole tap drives the S3528 reconstruction filter; the SSM2045 VCA/mixer
section and S3528 buffer/feedback network follow. The two-pole tap is unused
in this channel. The VCA is part of the same IC, not a separate generic ADSR.
Firmware-generated DAC voltages are multiplexed to sample/hold circuits for
filter/amplifier control. This lab does not replace those controls with an
envelope generator.

| Channel 1 part | Nominal value | Implemented role |
| --- | --- | --- |
| C153 (SSM C1) | 20 nF | First filter pair |
| C144 (SSM C2) | 3.9 nF | First filter pair |
| C128 (SSM C3) | 20 nF | Second filter pair |
| C120 (SSM C4) | 1.8 nF | Second filter pair |
| R154 | 4.7 kohm | Buffer feedback impedance only |
| C161 | 1 nF | Buffer feedback impedance only |

R138 input attenuation, R74/R82 VCA-input attenuation, the control networks
and the complete S3528 filter/buffer are not implemented by the lab.

## Filter equations and numerical method

For each two-pole section the manufacturer gives:

```text
H(s) = -omega0^2 / (s^2 + alpha*omega0*s + omega0^2)
omega0 = gm / sqrt(C1 * 10*C2)
alpha = sqrt(10*C2/C1)
```

The second pair uses C3 and C4. Two inverting pairs produce an overall
noninverting four-pole tap. With the EII capacitors, alpha1=1.396424 and
alpha2=0.948683; their natural frequencies differ. A cascade of four
identical one-pole stages would not reproduce these equations.

src/ssm2045.h implements the sections with trapezoidal state-variable
integrators. Frequency response is the bilinear image of the analog equations;
there is no undocumented prewarping or arbitrary cutoff correction. The
sample rate is supplied explicitly, and tests use 192kHz. High-frequency
warping remains; this is not a calibrated host-rate audio implementation.

Filter gm is explicit in siemens. No absolute gm-vs-FC calibration is assumed.
The separate pin-5 helper supplies only the typical -19.3mV/octave *ratio*;
it must not be fed a board-level FC control voltage without the actual input
network. At gm=0, capacitor state is held. No resonance-feedback gain,
saturation, self-oscillation, transistor mismatch, noise or supply effects
are synthesized. The model corresponds to zero Q-control current only.

## VCA and feedback impedance

The linear VCA kernel is an ideal small-signal branch model using the
manufacturer's typical transconductance point, 4085uS at 500uA control
current. It interpolates proportionally with explicitly supplied branch
current over 0..500uA. This interpolation is a model assumption, not a
measured characterization across that range. Input is volts; output is amps.
The exponential mixer bias, common-control current distribution, offsets,
leakage and distortion are absent. No direct mapping from EII amplitude CV
to branch current has been claimed.

Channel1Feedback models only the ideal impedance -R154/(1+s*R154*C161).
Its input-current sign convention is current entering the buffer summing
node; the physical polarity of the complete VCA branch is not calibrated.
This block does not claim to model the S3528 internally.

## Validation and reproducibility

```sh
bash tools/build-native.sh > validation/tests-analog-lab.txt
g++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc tests/analog_tests.cpp -o build/analog_tests_san
ASAN_OPTIONS=detect_leaks=0 build/analog_tests_san > validation/sanitizer-analog-lab.txt
```

The unit-impulse tests are normalized mathematical probes, not assertions
that a one-volt chip input remains in the small-signal operating range.
Complex responses at 100Hz, 1kHz, 5kHz, 15kHz and 40kHz agree with an
independent closed-form analog transfer evaluated under the bilinear map.
Tests also verify DC gain/tap polarity, FC slope sign, finite state under gm
changes, VCA nominal gm/current scaling, and R154/C161 complex impedance.
These validate equations and numerical integration, not actual EII fidelity.

Next: resolve the FC/Q/amplitude control networks and SSM Q-feedback law,
then add nonlinear behavior with justified calibration. Complete S3528,
6072/sample timing and firmware sample/hold integration remain prerequisites
for an end-to-end output channel; SIO/DMA OS boot also remains unfinished.


Checkpoint0.18 adds Am6072/reference/I-V decoding and an explicit experiment
bridge from Machine DAC/CV snapshots into these kernels. See PLAYBACK_ANALOG.md.
The original standalone-only description above is historical. Active deglitch,
S3528, full controls and physical output calibration remain incomplete.
