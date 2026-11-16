# Production DRAM port: slice 4 plan

Status: slices 1..3 (register file + staging, ALU, RFD bus/pipeline) are
implemented in `src/production_registers.h`, `production_alu.h`,
`production_bus.h` and proven against the reference by
`tests/production_register_tests` (800k randomised operations).

## Why the DRAM port is the next gate

`DramCpuPort` is the largest remaining concrete component. It owns RAS/CAS edge
semantics, the IC69 address-mux latch, the IC99/IC31 bank-reset inhibition, the
IC103 read latch, the write aperture and the Ready/WAIT handshake. The reference
keeps every value as value/known and guards with validity records; the concrete
port keeps plain bytes and bits, which removes the merge paths and the subset
iterations.

## Concrete state (mirrors the reference fields that matter)

```
bool active_,cpuRead_;  uint32_t cpuAddress_;  uint8_t cpuData_;
uint8_t mad_, bank_;    bool rowValid_,casAddressValid_,rowCpu_;
bool rasQ_,casQ_,ras_,cas_;  uint8_t row_, rowBank_;
uint8_t channelLatch_, mdo_;  bool mdoCurrent_;
bool readReady, writeReady;  uint32_t writeAddress;  uint8_t writeData;
uint64_t reads,writes,readCaptures;
```

## Port order

1. `cpuBus` invalidation subset (ready flags, row capture reset) with a
   differential test over random bus sequences.
2. `rising`/`falling`: IC49 negative-edge JK for RAS, IC152 CS clock for CAS,
   MAD/bank capture from the old control word, channel/read latch ticks.
3. `settle`: +RAS/+CAS active-high, IC99/IC31 bank-reset, IC103 capture,
   write aperture commit, Ready/WAIT handshake.
4. `advanceQuietClocks`: IC69 latch only, no backing read.

## Test contract

For each step, drive `DramCpuPort` and `production::DramPort` with the same
randomised known-input sequence (control words, CPU bus pin changes, backing
memory contents) and require equality of every public observation:
`readLatch`, `channelLatch`, `writeAddress`, `writeData`, `readReady`,
`writeReady`, `reads`, `writes`, `readCaptures`.

Only when all of these agree may the concrete port be used by the production
controller. The reference engine stays the oracle and the default.

## After the port

Upper sequencer transaction (ninth slot and active CPU/sample DRAM), then
`ConcreteController`, then the integer deadline scheduler and the worker's
waitable-timer sleep.

## Concrete algorithm derived from the reference (for slice 4b)

All values are certain in the production path, so every merge/possible branch
collapses to a single case. Reference lines 71..173 map to:

falling(control, delayed9):
  rasQ = !bit12(control)
  aluAddress.driveClock(!bit7(control))
  channelClock(!bit8(control)); readClock(!delayed9)
  settle(control)

rising(oldControl, sum):
  channelClock(true); readClock(true)      // capture OLD mdo first
  source = bit15(oldControl)==0 ? cpuAddress : aluAddress.output
  mad = bit11(oldControl) ? high(source) : low(source)
  bank = (source>>16)&7
  casQ = bit12(oldControl)
  aluAddress.driveClock(true, sum)
  settle(oldControl)

settle(control):
  ras = !(rasQ && !casQ)
  cas = casQ && !bit13(control)
  rasRise = !rasOld && ras ; casRise = !casOld && cas
  cpuSelected = bit15(control)==0
  bankEnabled = cpuSelected || bit14(control)==0      // IC99/IC31 RASRST inhibit
  if (!bankEnabled) rowValid = casAddressValid = false
  if (rasRise) { row=mad; rowBank=bank; rowCpu=cpuSelected;
                 rowValid = bankEnabled && (!cpuSelected || active) }
  if (casRise && rowValid && rowCpu==cpuSelected) {
      address = (rowBank<<16)|(mad<<8)|row
      if (!cpuSelected || (active && address==cpuAddress)) {
          casAddress=address; casAddressValid=cpuSelected
          if (!bit10(control)) { mdo=memory[address]; ++reads; mdoCurrent=cpuSelected&&cpuRead }
      }
  }
  if (cas && bit10(control)) mdoCurrent=false
  if (cas && rowValid && casAddressValid && active && !cpuRead && cpuSelected && bit10(control) && !writeReady)
      { writeAddress=casAddress; writeData=cpuData; writeReady=true; ++writes }
  if (!cas) casAddressValid=false
  if (!ras) rowValid=casAddressValid=false
  rasOld=ras; casOld=cas

channelClock(level): on rising edge, channelLatch = mdo
readClock(level):   on rising edge, readLatch = mdo; ++readCaptures;
                    if (active && cpuRead && mdoCurrent) readReady = true

The fast `executeStrobes` path uses dramStrobeProgram[index] with
index = bit10 | bits12..13<<1 | rasQ<<4 | casQ<<5 | ras<<6 | cas<<7; the same
event order applies, and it is already concrete-table driven.

Remaining component needed by this slice: a concrete ControllerAluLatch
(`driveClock(clockLow, sum)` then `.output`) mirroring controller_alu.h.
