# 0.40: correct the basic audio path's control direction and missing stage gain

User Windows0.39 screenshot: selected OUT01–02 (BEHRINGER UMC1820), MIDI1124
UART bytes/1124 firmware reads/0overruns/drops, original P01GrandPiano1 preset.
User confirms all01–12 outputs available. Speed varies81–85% during disk boot,
then stays87–88%. These are speed ratios, not CPU percentages. The working
boot does not need an invented firmware patch. The expensive source-clock
runtime remains a weak implementation requiring event-based execution.

## Measured near-silence before correction

A corrected-startup original-bank probe sampled the held DAC/CV state every
20000 CPU ticks after actual note-ons60/64 velocity100. Channels0/1 had many
different definite DAC codes, with sampled raw I/V peaks0.468624V/0.417318V;
channels2–7 stayed at zero. Output was not just one transient:95147of96000 PCM
frames were nonzero. But float PCM peak was0.000163143821 and RMS0.00003434444
(about-89.3dBFS RMS). This explains why a mere `peak>0` test was misleading.
No native Windows loopback capture was performed, so this does not prove that
the WASAPI backend is correct on the user's machine.

## FCV direction and relative slope

Visually checked service-manual page7-45 / PDF107: R122=22.6k from FCV,
R130=124k to-15V, R112=1k to ground, C136=3.3uF. Ignoring pin bias current
and RC dynamics, Kirchhoff gives the static pin5 voltage:

```
Vpin = (FCV/22600 - 15/124000) / (1/22600 + 1/124000 + 1/1000)
```

The FCV-to-pin slope is+0.0420481519V/V. The visually checked1983 SSM2045
manufacturer sheet page2 specifies typical -19.3mV/octave at pin5. Therefore
the nominal relative board slope is-2.178660721octaves/V: increasing positive
FCV must LOWER frequency. The previous provisional increasing map reversed it.

The old midpoint678.822509939Hz at FCV2.5V is retained as an explicitly
provisional first-section pole anchor. Absolute frequency, pin bias, RC settling,
temperature and component tolerances are not calibrated by this correction.
For the observed FCVs0.8203125V/1.0546875V, the previous83.49Hz/111.84Hz poles
become nominal8577.61Hz/6020.79Hz. These numbers are model settings, not measured
hardware cutoffs or manufacturer absolute calibration.

## Preserve gain when bypassing switched dynamics

The same service sheet shows R26=200ohm and R34=10kohm around the active
3202/TL084 charge-transfer stage. The existing Deglitch3202 kernel and independent
nodal-RK4 test already establish its small-signal DC gain -R34/R26=-50. The
basic host path had bypassed it as a unity wire, losing that inversion and gain.
The dynamic bypass now retains the nominal low-frequency gain before R138.
This is not a calibrated broadband replacement for the switched stage: its
actual frequency response, switching, settling and rail/nonlinear behavior
remain omitted. S3528 is still dynamically bypassed in the basic host route.

There is no added test oscillator, note allocator, firmware/RAM injection,
per-bank makeup gain or output normalization. The hardware gain omission and
control direction are corrected at their actual locations in the basic path.

## Verification

New regressions check the independent Kirchhoff residual, exact nominal octave
ratio, filter attenuation direction and agreement with the settled switched
deglitch kernel. Original-bank before/after tests use identical real MIDI
messages/firmware/assets and report peak/RMS/nonzero counts. The final probe
prints boot/final RAM hashes and optionally exports exactly96000 stereo frames
as48kHz PCM16 WAV; its rounding has no automatic gain or normalization.

ROM/disks and generated sample audio remain outside git. A short WAV from the
user's supplied bank may accompany the private Windows preview for listening.
Passing native DSP tests and cross-compiling do not establish native Windows
device output, listening quality or low CPU usage. CPU architecture work remains
the next priority; the speed display has not been smoothed to conceal it.
