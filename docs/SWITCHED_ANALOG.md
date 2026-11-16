# Switched analog kernels — checkpoint 0.19

This checkpoint adds the EII 3202/TL084 small-signal deglitch kernel, S3528
programming/clock kernels, and an explicitly approximate reconstruction filter.
These are laboratory building blocks. The Machine runs original firmware and
now decodes filter writes; it does not automatically render audio.

## Sources and inspected drawings

EII service schematic PDF107, printed7-45, is the channel1 reference. PDF74
and103 establish the CPU/output-board filter selects: main OUT40h..47h select
channels0..7 directly (unlike the rotated sample-DAC ACK). FD5..0 carry the six
low PDO bits. -CSWF goes low during the selected write and high at release.
The existing service PDF SHA256 is recorded in PLAYBACK_ANALOG.md.

The National LF11331/LF11332/LF11201/LF11202/LF13201/LF13202 datasheet,
[TI archive](https://www.ti.com/lit/ds/snosbs0/snosbs0.pdf), SHA256
`6c55be51ee9f08bfd7327fb9f2309c5651538fff29d72157e5ae59fe19952aed`,
PDF4 connection diagram was visually inspected: logical0 leaves the LF13202
switches open. S3=11,D3=10,IN3=9 and S4=14,D4=15,IN4=16 match the EII
3202 drawing. The kernel uses nominal150ohm on resistance. This identifies a
pin-compatible reference characteristic; this checkpoint does not establish
an exact installed manufacturer/revision from a BOM. Propagation delays and
charge injection are excluded rather than guessed from a typical-only value.

[AMI MOS Products Catalog 1984, second edition](https://www.rsp-italy.it/Electronics/Databooks/AMI/_contents/AMI%20MOS%20Products%20Catalog%20-%201984_2ed.pdf),
SHA256 `8f7e7a059d6440ac51e81a376d96f52af91a54611bd0016480998c83e3a64dc4`,
PDF192..201 / printed4.156..165. Programming, divider ROM, pinout and response
specification pages were rendered and visually inspected. Manuals remain
outside the source repository.

## Deglitch charge transfer

Deglitch3202::advance accepts seconds, a piecewise constant DAC voltage and
settled phi1/phi2 levels. Split intervals at every input or switch transition.
Do not pass a host-rate phase snapshot across an interval containing pulses.

R26=200ohm feeds the first switch source. R34=10kohm connects that same node
to TL084 output Y: it is feedback, not a grounded load. C74=470pF connects
sample node B to ground. The second switch connects B to the ideal virtual
ground at TL084's negative input; C83=470pF feeds back from Y to that input.
The positive input is grounded.

| Settled phases | Ideal finite-Ron evolution |
| --- | --- |
| Both off | B and Y hold; DAC glitches are isolated |
| phi1 on | Y holds; B exponentially approaches `(10000*Vin+200*Y)/10200`, with tau `(150+(200 parallel 10000))*470pF` |
| phi2 on | B exponentially discharges with tau `150*470pF`; `deltaY=(C74/C83)*deltaB` |
| Unknown or overlapping | Capacitor history becomes unknown; no supported settled overlap model |

The acquisition/transfer pair inverts; the repeated-cycle small-signal DC
gain is -R34/R26=-50. That is a linear equation, not permission to generate
250V from full-scale DAC input. TL084 clipping/rails, bandwidth/slew, switch
turn-on/off delay, injection, parasitics and leakage remain unmodeled. Use
small-signal inputs within op-amp compliance. Unknown acquisition first taints
C74, then propagates to output on transfer. Known input cannot erase unknown
capacitor history. resetKnownZero is an explicit laboratory initial condition.

## S3528 control and timing

S3528Control implements all64 standard ROM divisors; they are not monotone in
binary code. For external master fM, internal filter clock is fM/N and cutoff
is fM/(40*N). The EII schematic supplies +C5MHz.C, giving61.03515625Hz at00
(N2048) and31.25kHz at3F (N4). The cosine input prefilter samples twice the
internal filter clock, i.e.80*fc. This input sampling rate is not a host rate.

| CE pin | Mode |
| --- | --- |
| VDD | Active word holds; live data ignored |
| Ground | Data latch tracks; previous active ROM word holds |
| Ground to VDD | Latched six-bit word becomes active |
| VSS | Transparent live code to ROM |
| Unknown | Programming history conservatively unknown |

Unspecified transitions from transparent/unknown into VDD remain unknown.
Power-up code is unknown. Machine commits completedWrite at deassertion of
main OUT40h..47h; PDO7..6 are ignored. Scanner writes do not program filters.
The existing deferred-strobe path now includes40h..47h. No ROM changes or
forced startup code are used.

S3528Clock consumes individual half edges of the external5MHz master, hence
10million calls/second in the EII reference. A prefilter sample event occurs
every N half edges. Its divider phase starts unknown. Changing the divisor
invalidates phase because the catalog does not specify restart behavior.
alignForExperiment establishes an explicit chosen phase for laboratory use;
it is not an invented physical reset or firmware initialization mechanism.

Machine::playbackSwitchInputs exposes the eight real divider phi1/phi2 levels,
active filter codes and nominal cutoff frequencies. It is a snapshot, not an
edge history or audio scheduler. Channel0 MODE/SAR remains unmodeled.

## Original0.19 reconstruction surrogate (selectable legacy profile)

S3528NominalResponse is an optional, named approximation. It is NOT a recovered
AMI circuit transfer, measured chip model, or automatic Machine audio path.
Each pair of samples at80*fc is averaged (cosine prefilter) then advances a
seventh-order elliptic lowpass at40*fc. Output holds between paired updates.
No analog input-amplifier, feedthrough, distortion, output loading, clock
phase uncertainty or complete switched-capacitor topology is represented.

The catalog gives response specifications but no complete poles/zeros or
ladder coefficients. Embedded SOS coefficients are reproducible with SciPy1.17:

```python
from scipy.signal import ellip
ellip(7, 0.1, 51, 0.05, output='sos')
# order7, passband ripple0.1dB, stopband target51dB, fc/(fs/2)=1/20
```

This meets a design target, not all physical chip typical-response points.
For example, the surrogate gives -8.70dB at1.06fc and -51.05dB at1.62fc;
the catalog typical response gives approximately -3dB and -75dB respectively.
Do not call this reconstruction calibrated or use those differences as a
hardware regression failure. A measured response or recovered network is
needed to replace the surrogate. Unknown state remains unknown until explicit
initial conditions; no silent pass-through or default known-zero state.

The existing PlaybackAnalogBank continues to require deglitchedVolts and
reconstructionVolts by default. Callers can supply outputs of these new
kernels; diagnostic direct routes remain off. There is no host-rate endpoint
sampling shortcut disguised as source-edge integration.

## Validation

switched_analog_tests compares the deglitch exponential solver with independent
nodal RK4 equations over changing input and phases, verifies DC inversion,
charge hold, idle-window glitch rejection and unknown-state propagation. It
also drives both switches from actual ChannelRequestTiming HC393 phases at
100ns boundaries, preserving the1.6us phase windows.

All64 S3528 code latches and integer sample cadences are exercised, including
nonmonotone ROM spot checks and the manufacturer's3.579545MHz/N28 example.
CE hold/load/transparent/unknown paths, unknown startup and phase-change guard
are checked. Nine sinusoidal responses are compared with independently
calculated SciPy complex-transfer magnitudes, with finite-window least-squares
fitting; this validates the surrogate implementation, not chip fidelity.

A200000-edge/20ms laboratory cascade runs existing Am6072/reference/I-V,
phase-switched deglitch, zero-Q SSM2045, S3528 surrogate, C93/VCA and feedback
at10MHz. It uses explicit small amplitude CV, pin gm/current, known-zero
capacitors and chosen S3528 phase, producing50000 prefilter samples and finite,
nonzero output. Both diagnostic direct routes are disabled. Its source-edge
constant-voltage intervals and nominal submodels remain approximations.

A real Z80 program writes all eight ports. The test observes asserted WR with
old active code, then one commit per channel at release; upper data bits and
scanner isolation are covered. The native suite and ASan/UBSan logs are in
validation/tests-switched-analog.txt, test-switched-cascade.txt and
sanitizer-switched-analog.txt. Original-firmware regression results are recorded
in VALIDATION.md. CMake and Windows builds/runtimes are not checked here.

## Checkpoint0.20 integration

See CLOCKED_PLAYBACK.md for opt-in Machine half-edge integration, separate
channel streams, exact rational box-integral CSV observations and an offline
capture-only tone analyzer. This advances beyond the standalone0.19 kernels;
S3528 analog fidelity and physical calibration remain explicitly unresolved.

## Checkpoint0.21 profile update

S3528_CURVE_FIT.md documents the new default AMI1993 typical-curve fit, pixel
provenance and executing-kernel holdout validation. The generic elliptic
response described above is preserved as an explicit comparison profile.
Neither profile constitutes recovered internal coefficients or physical calibration.
