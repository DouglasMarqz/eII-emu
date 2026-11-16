# GUI runtime routing audit — unreleased 0.36e

The GUI worker never selected the accelerated runtime. Choosing a concrete
startup in FrontPanelSession did not enable event timing, peripheral runtime,
concrete controller execution or voice transactions. The benchmark selected
these independently. This was a production-path wiring defect.

Machine::enableNormalRuntime now owns this preset, shared by the GUI worker
and the voice benchmark. It is selected only after successful boot validation.
Raw Machine construction stays reference. Unknown/all16 startup forces reference;
PanelBoot.referenceExecution permits an independent oracle at the same explicit
phase0/seed1 startup. Snapshot selectors read actual backend configuration.
Failed boots preserve the old machine. No firmware, WAIT or memory bypass added.

## Actual worker validation

worker_runtime_probe runs FrontPanelSession itself with identical media, physical
startup and CPU tick budget. It compares complete reports, LCD, LEDs and transfer
counts. Boot is outside timing; normal worker chunking and publication are included.

A clean sequential O3/LTO 24M-tick pair (6 simulated seconds):

| Worker route | Wall seconds | Simulated / wall |
| --- | ---: | ---: |
| Reference | 14.93211683 | 0.401818 |
| Normal | 9.931193062 | 0.604157 |

Reports are identical. This is 33.49% less wall time, about 1.50x throughput.
One paired container measurement, not a median or Windows CPU percentage.
**Still below real time; CPU problem is not solved.**

The full 200M-tick pair also passes exact report comparison and reaches the
original Grand Piano OS/bank state. DMA=520552, strict writes=518144. Its timing
overlapped builds/tests and is functional evidence only, not a benchmark.
Full tools/build-native.sh passes; FrontPanelSession ASan+UBSan passes with
leak detection disabled. See validation/runtime-036e-*.

## Remaining large cost

An instrumented 24M-tick concrete-engine profile before this routing change
shows 60M controller/upper-path calls and 90,481,720 DRAM settles for 6 simulated
seconds. z80_tick accounts for 2.62% sampled self-time. These instrumentation
percentages are diagnostic, not release CPU measurements. ADC/scanner helpers
are not the main cost. The next architectural target is scheduling observable
upper-controller/DRAM/WAIT events instead of evaluating them every source clock,
while preserving actual CAS reads, CPU address capture and interrupt boundaries.
No new Windows binary or main-branch promotion in this checkpoint.
