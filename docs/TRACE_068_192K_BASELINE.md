# First user Windows0.68 performance recording: healthy192k Play baseline

2026-10-21. The supplied file parses as647 JSONL records:1 header,360 samples,
286 events,527594 bytes. Recording ends at92.174s; samples span0.029–92.137s.
Header declares0 overwritten rows. User says the reported high-cost anomaly
was not reproduced. The trace confirms model3=SSM192k (not182k).
The original trace remains a user attachment and is not copied into this repo.
SHA256: `c25af534dbd76b0868f0e815871a6a6fe127a09ea485a30961aaac91d8a13e9b`.

## Observations

The sequence is already playing at the first sample, LCD S01 La Primavera /
Tempo090BPM throughout. No idle-to-Play transition is recorded. Machine
lifetime stays1; audio lifetimes1–5 correspond to SSM192 -> Legacy -> SSM192
-> Legacy -> SSM192. Model request events occur at27.344,35.184,75.368,80.296s.
No changing calibration revision/profile, UART MIDI traffic or media transfer
occurs during these samples; this is sequencer playback, not a live-key burst.

Across all samples: speed min0.9984396524, max1.001766161 (99.844–100.177%).
Maximum observed lateness1.654ms; pacing rebases/discarded debt remain0.
No production invalid flag or reported engine error. All sampled nonlinear
worker failures, dropped input, solver fallback/recovered/held and clipped
frame counters are0 in each audio generation. Largest recorded worker maximum
wait1.0009ms; observed DSP queue at most48 input frames=1ms at48k ingress.
The192k figure is internal chip rendering, not WASAPI's48k host sample rate.

Thread-time comparison below excludes the first2s of each generation, and only
uses complete thread intervals lying within the retained segment. Busy is CPU
seconds per wall second of one logical processor; these are not process-wide
Task Manager percentages. Windows reports24 logical processors.

| Audio generation | Mode | Machine busy | SSM busy | Iterations / solver sample |
| --- | --- | ---: | ---: | ---: |
| 1 | SSM192 | 0.856325 | 0.744637 | 1.992123 |
| 2 | Legacy | 0.851380 | absent | n/a |
| 3 | SSM192 | 0.831008 | 0.685928 | 1.850442 |
| 4 | Legacy | 0.837150 | absent | n/a |
| 5 | SSM192 | 0.825320 | 0.747030 | 2.051717 |

SSM busy and solver work both decrease in the middle192k phase then rise in
the last phase. This is consistent with varying musical/solver workload;
it does not establish causation or a persistent restart saving. Different
sequence positions, unequal durations, CPU frequency and scheduling are not
controlled. The prior identical-input96k native experiment remains separate.
UI busy averages0.045–0.062 of a logical CPU in these segments; WASAPI is small.
At most one named SSM worker appears simultaneously, and none during Legacy;
no unreadable process threads. Total process threads vary4–7, not monotonically
upward. No evidence of accumulating old DSP workers in this recording.

WASAPI remains running throughout:0 timeouts/restarts and empty error fields.
Host PCM dropped counter remains0. Missing-frame counter increases by2400
frames=50ms across the recording; the pre-recording total561984 is not included.

Observed increments (sample-observation times, not exact dropout times):
11.590s:+96;27.399s:+240;27.907s:+240;35.435s:+384;71.530s:+96;
75.446s:+480;75.697s:+240;80.542s:+624.
Most occur during/soon after mode changes that clear the PCM queue and rebuild
DSP. Timing alone does not prove their cause. In the steady SSM segments there
are two isolated96-frame (2ms) zero-fill increments, at11.590 and71.530s.
Therefore this is healthy real-time operation with small transport gaps, not a
claim of completely gapless audio or an explanation of the earlier sound hang.

## Implication

The trace button/save workflow has now been exercised by the Windows user and
produced useful valid data; cancellation/error dialogs remain unverified.
This is a baseline for comparison with a future anomalous recording. No new
runtime fix or executable is justified from this file. Machine-owner work is
still substantial (~0.83–0.86 of a logical CPU); the192k DSP is also substantial
(~0.69–0.75). Neither total CPU nor this non-failing run identifies a faulty
algorithm. Preserve accepted sound and distinguish host transport underruns
from nonlinear solver/firmware failure. No model-counter deltas were taken
across audio-generation resets.
