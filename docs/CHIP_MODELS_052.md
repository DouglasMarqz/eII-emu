# Selectable SSM2045 models - checkpoint 0.52

The Windows Chip model selector switches the audio renderer without resetting firmware, RAM, disk or MIDI. The choice persists in the per-user preferences and also applies on boot. The default remains Legacy VCF/VCA (zero-Q). Available:

| Choice | Renderer | Ownership |
|---|---|---|
| Legacy VCF/VCA (zero-Q) | Original48k small-signal filter and linear VCA | Emulation worker, unchanged |
| SSM2045 nonlinear (96k DSP) | Two internal samples per48k frame | Dedicated DSP worker |
| SSM2045 nonlinear (192k DSP) | Four internal samples per48k frame | Dedicated DSP worker |

The new model is vendored from https://github.com/Tuth/ssm2045-chip at commit7ea4df873d9bb01fbc312882edb842eabc35aa5b, MIT license retained. Its VCF uses the implicit nonlinear four-state trapezoidal/Newton solver with Q/Ga feedback, and its VCA uses the nonlinear dual-cell/exponential mixer. The upstream numerical fallback remains unchanged. This is a behavioural/SPICE-validated model, not transistor-level or hardware-measured.

## EII board adapter

The chip demo configuration is not the EII circuit. src/nonlinear_voice.h uses the visually checked service sheet7-45 and SSM2045 datasheet:

- EII C153/C144/C128/C120:20nF/3.9nF/20nF/1.8nF.
- FCV node:R12222.6k, R130124k to-15V, R1121k; C1363.3uF at the Thevenin node. Use the actual pin-5 voltage and upstream gm law, not normalized cutoff or the old arbitrary frequency anchor.
- QCV reaches Q through R14622k. Nominal current=max(0,(QCV-0.6V)/22k), approximating the datasheet grounded-base PNP emitter. The600mV drop is an explicit unmeasured board approximation; temperature/current dependence and driver dynamics remain unmodeled. At this approximation160uA corresponds to4.12V QCV, not a measured EII oscillation threshold.
- Keep the existing Am6072 reference RC, LF356 I/V and nominal deglitch DC gain. No source-clock deglitch switching dynamics are added.
- Route the four-pole output through the existing C93/R74/R82 coupling and divider to VCA2; VCA1 input is grounded. EII R82 is470ohm, rather than the datasheet example's910ohm. The VCA returns current into the existing R154/C161 feedback impedance; the demo200ohm voltage load is not used as the board output.
- VCA controls remain the explicit provisional bias of0/500uA and EXPO0V, retaining the original nominal small-signal gain. Total control current is500uA. Exact EII bias wiring/calibration remains open.
- Typical per-device DC offsets are disabled; no fabricated noise or thermal model is added. No synthetic note-on reset/seed: the physical channel states persist as firmware reallocates notes, and sample audio can excite resonance.

With these EII capacitors and the full0..5V FCV range, the highest natural section frequency is about38.94kHz, below the96k path's47.04kHz safety margin. The upstream demo capacitor ratio has a different limit. Both new paths use a127-tap Blackman-windowed sinc20kHz lowpass before48k output; filter group delay is0.656ms at96k and0.328ms at192k. Held48k DAC/control observations remain a runtime approximation, not source-clock analog reconstruction. S3528 is still bypassed. Absolute amplitude/headroom, analog calibration and complete switched deglitch/reconstruction remain separate tasks.

## Threading, latency and overload

One DSP thread owns all eight nonlinear chip instances. It receives copied immutable DAC/ACV/FCV/QCV snapshots; it never accesses Machine, CPUs, controller state or disk. Parameter setters and processing run on that same owner. The emulation thread enqueues blocks; the Windows audio callback consumes already rendered stereo frames. The legacy path creates no DSP thread.

Blocks contain at most96 frames (2ms). The input queue holds at most8 blocks (16ms), plus one in-flight block and the partial producer block. It is deliberately bounded: overload drops the oldest snapshots and resets nonlinear history at the discontinuity. It cannot accumulate an unbounded delay. This is an overload safeguard, not a claim of lossless rendering on an undersized host. The existing host output queue bound is retained.

Capture state includes HOST CHIP model/queueFrames/rendered/dropped/failures. These are block-level counters, not per-CPU diagnostics. A nonzero dropped count means the host could not sustain this rendering/pacing workload; use96k or Legacy. Model changes join the previous worker, reset only renderer history, and clear queued host audio. They may produce a brief discontinuity; they do not re-boot firmware.

The Sink passed to Machine::enableRuntimeAudio is called on the DSP thread in either nonlinear mode; it must not read a running Machine. The built-in queue sink follows this rule. Shutdown cancels pending blocks and joins the current processor.

## Validation

See validation/tests-052-chip-models.txt for final measurements and limits. Legacy48k PCM was compared against0.51 on48000 frames with all eight channels and changing DAC/CVs; exact checksum9d05d4552d2db341. Worker/session tests cover model switching without advancing/resetting firmware, nonlinear boot, frame count, partial flush, bounded overload and shutdown. EII adapter tests cover Q response, FC/Q extremes and unknown-input recovery. Upstream chip tests exercise its original validated configuration.

Native timings are measurements on the execution host, not Windows performance guarantees. The user's standalone chip measurement17.5x at192k for one voice suggests approximately46% of one core for eight voices, excluding host/adapter overhead. Windows playback of this checkpoint has not been tested here.

The supplied-bank eight-note real-time test on this host rendered193296/193920 frames at96k (624dropped) and164256/193920 at192k (29664dropped), with0DSP failures/NaNs and identical firmware/memory state in all modes. This is NOT a real-time performance pass for the nonlinear routes on this host. The separate offline-drain mode waits only in the diagnostic probe to verify lossless audio/firmware integration; production never waits for DSP. Hardware target performance must be tested separately.
