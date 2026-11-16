# Slider lag in "All Vcs on Kybd" edit mode (audit 2026-10-31)

## Reproduction (core, no GUI/host)

`slider_latency_probe ROMDIR BANK replay x 4` with `REPLAY_YES=1`
(Filter, note, Enter, Yes(9), Yes(9) -> `Entire Kybd? Y/N`, `All Vcs on Kybd`,
then 1 -> Freq/Q/Env). Slider A 4 Hz sweep for 3 s:

* scanner->main ring (scanner RAM 26Eh/270h) fills to 252 of 256;
* LCD keeps changing 1.9 s after release, about 68 ms per message;
* owner VST trace `slider_trace_2.csv` shows the same (ring 255, ~2 s tail).

With `No` (single voice) the ring never exceeds 1 and the tail is 0.2 s.

## Where the time goes

* Main loop 593C-5959 walks a 61-entry keyboard table, twice per message.
* Main DRAM WAIT: 23.1% of main clocks during the sweep. Run lengths are
  2-5 CPU clocks, mostly 2-3. All of it comes from the hardware WAIT path;
  the adapter guards contribute 0 clocks, and disabling them changes nothing.
* The scanner produces about 80 messages/s at this sweep rate. The main
  consumes about 15/s (about 19/s even with zero WAIT).

## Schematic checks, rendered pages

| Sheet (PDF) | Item | Code | Result |
| --- | --- | --- | --- |
| 7-10 (73) | IC62/IC61a done FF (`+UCD09.1` clock, reset by idle `-MPRQ`), IC84a, IC61b on +PCLK, IC84b with `-IWAIT` -> `-PWAIT`, IC41 -> `-MPREQ` | `CpuRequestHandshake` | match |
| 7-17 (80) | IC111 S161 D..A=1000 (8 states per slot), /LD=!RCO, +C10MHz.1; IC112 slots 0..8; IC110 STATE shift | `controller_timing.h` phase/channel | match |
| 7-18 (81) | IC135 A8..A0 = UCD15.1, UCD14.1, UCNT2..0, -MPRQ.1, -MPRD.1, CHSL3, UREQ.1; IC137 A8..A0 = CHSL3, UCNT2..0, UREQ.1, PRECAR, LOOPMD, DECFLG, BACKMD; IC140 clocked by NAND(STATE0, -C10MHz.1) | `prom.h` upper/lowerAddress | match |

The slot is 800 ns, so a 2-3 CPU-clock wait means the CPU is served at
the next slot boundary. The PROM contents are the real dumps.

## Conclusion

The WAIT chain, slot counter and PROM addressing match the schematic. The
lag comes from the original firmware doing all-voice work per slider message
faster than the main can retire it. On present evidence it is hardware
behaviour, not an emulation slowdown. A real-unit recording in this exact
mode would confirm it.
