# Floppy mechanics and SIO modem path, checkpoint0.25

The real firmware now controls optional SA465-reference drives through its
PIO pins. TRACK00/write-protect return to PIO-B; INDEX/READY enter the SIO
DCD/CTS pins and can generate genuine IM2 External/Status interrupts. This
checkpoint stops at mechanical/sensor service, before serial data and DMA.

## Verified wiring and sources

EII service PDF76/77/78 (printed7-13/7-14/7-15), enlarged and visually checked:

| Host | CN11/function |
| --- | --- |
| PIO-B4 through noninverting7407 | DSEL0_n, pin10 |
| PIO-B3 through noninverting7407 | DSEL1_n, pin12 |
| PIO-B2 through noninverting7407 | STEP_n, pin20 |
| PIO-B1 through noninverting7407 | DIR_n, pin18 |
| AND(B4,B3), noninverting7407 | common MOTOR_n, pin16 |
| PIO-B0 | TRACK00_n, pin26 |
| PIO-B5 | WP_n, pin28 |
| SIO-A DCD input | INDEX_n, pin8 |
| SIO-A CTS input | READY_n, pin34 |
| SIO-A DTR output through7407 | SIDE_n, pin32 |
| SIO-A RTS output through7407 | WRITE_GATE_n, pin24 |

The DCD input and DTR output cross near the SIDE line on the scan; enlarged
inspection confirms their distinct nets. PIO modes/direction masks determine
physical drive-control pins, not merely the last output byte. Unconfigured
input control pins use a released-high digital convention; analog floating
TTL/bus contention is not modeled. Selected drive outputs share open-collector
status lines. The motor line affects BOTH installed mechanisms, irrespective
of their individual selection. PIO link handshakes remain independent.

Drive reference: Shugart455/465 Reference Manual, October1984,39409-1,
SHA2563c1a32c889f7154ad12eecc91cb2ac65419b6493006d25fe1d2629bab2b7462d.
PDF12/16/23..26 visually checked. Existing source link/hash and reference-only
model identification limits are in FLOPPY_HARDWARE.md. E-mu ZM302 is not
proven to identify one exact SA465 variant or jumper set.

SIO source: Zilog Z80 CPU Peripherals User Manual, UM008101-0601,
https://www.zilog.com/docs/z80/um0081.pdf,
SHA256b4efc81540c05990883cf4c7792c2a3d49fb7471bb5502931383ab55b4540886.
PDF295/298/313 (printed275/278/293) visually checked for commands/vector/
status latching; related text checked in printed296/300. Source PDFs and
original ROMs stay outside the source repository.

## Reference mechanics, with explicit model choices

`Machine::floppy` has two optional drives, absent by default. Attach with
`floppy.attach(drive, initialCylinder, initialPhaseTicks)`; both initial values
are REQUIRED. Cylinder0..79 and rotor phase0..799999 ticks are concrete
experimental conditions, not a recovered physical power-up position. Node
CPU reset does not silently home, erase media or reset the rotor. Drive power
is assumed applied at simulation time0; attach is not a hot-power model.

Clock base4MHz. Profile timing follows the manufacturer reference:

- STEP low-to-high initiates one logical movement; DIR high moves outward.
- Pulse and direction setup/hold1us; adjacent accepted steps at least3ms.
- A100ms power-up guard rejects/counts earlier pulses. This is a nominal
  guard for out-of-spec operation, not a claim of actual premature-pulse
  failure physics. Too short/close pulses are rejected and diagnosed.
- Head position changes logically on the accepted event. TRACK00 reflects
  position0 AND stepper phaseA; additional outward pulses at the outer stop
  toggle that phase and the sensor, as the manufacturer describes. Inward
  recovery from the overstep phase first returns to phaseA at cylinder0.
  Seeking past the last modeled cylinder marks the position invalid.
- The15ms data-settling guard is separate from TRACK00. Actual continuous
  head travel/sensor delay is uncharacterized. An earlier prototype gated
  TRACK00 for a full3ms and missed the firmware's next poll; that arbitrary
  sensor delay was removed. The model does not fit a guessed shorter sensor
  delay to firmware. `dataSettled()` covers head/power only, not all future
  side/drive/read-data requirements.
- Motor stable time is the500ms conservative reference start delay.
  Rotor phase is an ideal constant-rate300RPM (200ms) nominal clock while
  powered; acceleration and stopping inertia are not solved. INDEX pulses
  are suppressed before stable speed. Pulse width2ms is a project nominal
  parameter, not a recovered manufacturer soft-sector-width specification.
- READY requires a present disk and two steady index crossings200ms apart;
  phase influences the first crossing. No instant disk-ready flag is forced.
- Motor input release keeps the motor powered for3 seconds. Reselecting
  during that electronic delay preserves rotor and speed history. After its
  end the model stops without solving physical spin-down inertia.

`insertSensorDisk(writeProtected)` supplies only an index-bearing soft-sector
medium and its protect sensor. It contains NO sectors, data bytes, OS or flux
transitions. Removing it deasserts READY and reproduces the manual's warning
that a selected empty drive may hold INDEX continuously active; that constant
level is not counted as recurring pulses. Selection releases unselected
outputs without resetting the running internal mechanism.

These are nominal digital/kinematic approximations, not a physically measured
EII drive. Unknown initial-cylinder/rotor histories and arbitrary power-up
stepper phases are not enumerated. Every initial condition used in a firmware
experiment must be reported, rather than claiming all physical cold starts.

## SIO register/modem/interrupt subset

Main ports38..3F mirror channel A/B data/control. WR0 pointer selection,
External/Status reset, channel reset, Return From Interrupt, WR1 enable/vector
selection, WR2 vector and WR5 DTR/RTS outputs are implemented for this path.
Other write registers retain programmed bytes for future transport; this is
not a complete programmed serial engine. Enter-Hunt bookkeeping is nominal,
without a receiver or sync detector.

RR0 CTS/DCD reflect inverted active-low pins. With External/Status enabled,
the first change freezes modem status and requests an interrupt. Short pulses
remain visible until Reset External/Status; the command then exposes current
levels and rearms the latch. Reset leaves Tx Buffer Empty and Tx Underrun set.
No Receive Character Available is asserted without received bits. Unsupported
status/data reads return255 and are counted; transmit-data writes are counted
and do not invent serial completion. CRC/errors/receiver/transmitter/DMA Ready
are not implemented, and the subset must not be used to claim data transfer.

Sheet7-13/7-15 daisy chain is SIO -> DMA -> CTC -> PIO. DMA is still passive.
SIO gates downstream acknowledge and consumes only its own RETI; A external
status modifies vector bits3..1 to101 when channel-B Status Affects Vector is
set (e.g. E0 -> EA). B external gives E2; no pending condition gives E6 when
RR2 is read. SIO INT is combined only AFTER the vendor CTC tick, so it does
not contaminate the CTC's persistent INT state. Suppressed CTC REQUESTED state
is restored after upstream service. Tests cover actual IM2 SIO/CTC/PIO order
and each RETI, beyond merely checking the vector value.

## Reproduce the original-firmware experiment

```sh
bash tools/build-native.sh
build/floppy_probe ../roms/main.bin ../roms/scanner.bin ../roms/ic135.bin ../roms/ic137.bin 4000000 37 /tmp/eii-floppy-homing.csv
```

The dedicated probe explicitly installs drive0 at cylinder37, phase400000
(100ms into a nominal revolution), sensor-only write-protected medium;
drive1 is absent. It traces real PIO/SIO accesses with tick, PC and physical
sensor/position counters. It never patches firmware or injects a sector.

Fresh4M-clock original main2.1/scanner3.1/both-PROM run:

- Drive0 cylinder37 ->0 through exactly37 outward steps, no timing violations
  or early rejected steps, final phaseA/TRACK00 active.
- Three steady index pulses by the endpoint, READY active; three actual SIO-A
  interrupt acknowledgements. Firmware executes its INDEX handler and READY
  handler, then reaches its data-read/DMA setup routine.
- Seven strict CPU DRAM writes/twelve reads/95 WAIT holds remain.
- It then waits for the unfinished serial/data/DMA transport. This is not a
  completed OS load or a successful Insert Diskette regression with media.

Evidence: validation/floppy-homing-main21-4m.txt and the12 selected real
pin events in floppy-homing-events.json. Original main2.1 PC counts in the
probe are diagnostics for this exact ROM, not generic boot criteria.

Twenty native targets, sanitizer and no-drive/diagnostic firmware regression
results are recorded in VALIDATION.md. No CMake/Windows runtime, physical
mechanics calibration, mounted OS image, read separator or disk-writing claim.


## Checkpoint0.27 receive/DMA continuation

The0.25 description above is historical. Optional image pulses now feed an
experimental Bisync8x1 receiver and byte-mode DMA; see SIO_DMA.md. Defaults
remain no drive/no image. dataSettled now also includes the SA465 reference
100us side-change guard, beyond the existing head/power guard. No physical
calibration or accepted OS boot follows: CRC/Hunt continuity remains open.
