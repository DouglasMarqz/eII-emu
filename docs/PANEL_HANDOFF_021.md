# Custom0.3.21 — one panel control handoff

{
  "version": "0.3.21-custom-panel-handoff",
  "accepted": false,
  "parent_checkpoint": "bf532b577b57d62888c768d444bee97b4e75ca57",
  "scope": "Only VST3 built-in6panelcontrols handoff. Remove immediate UIowner voltagewrite; UI atomicparameter +hostnotification retained. Hostparameter automation supplies allsampleoffsetpoints. Noecho hosts getlatestchangedvalue once through SAME timestampqueue atoffset0. Fallback goesbefore explicitoffset0 events to preserveMIDIwheelbridge priority. Cache advances onlyifboundedqueue accepts; failedhandoff retried. No gate/drop/throttle of actualMIDI/note/hostautomation.",
  "reason": "Definite duplicated route permits newerimmediateGUI ADC to be overwritten byolderhostecho. Removing it alsoavoidsGUIvoltagecommand forcedstatuspublication. Native trace doesnotprove thisis the arp rootcause; owneracceptance needed.",
  "tests": "PASS panelhandoff/noecho4000UIupdatescoalesced/finalvalue/stationary/rejectedqueue retry/independentautomation/all6fullranges and concrete two-route-ordering fixture; actualPluginEngine8192boundedqueue admission+failure, existingVST3engine traces/timing; acceptedVCA all3models96/192 bothDACroutes; service/profile regressions. No native Windows plugin execution here.",
  "build": "Originalpinned compiler/backend,O3,LTO,static,DIAG0,background1,input/linkorder,strictPGOmismatcherror retained. Compatible018nativeprofile unchanged, originalunprofiledhelperpolicy. Four oldSDKwarnings; no generation instrumentation.",
  "dll_sha256": "11e6248a3910cce988a4eaf54b1ba130b233d55525754fb232e148f5f9cdba49",
  "dll_bytes": 2834944,
  "profile_sha256": "814828abc168a4e945711bddabab0e4252d06f7c844128dd9534a0e959a78f65",
  "runtime_changed_files": [
    "src/vst3/eii_vst3.cpp",
    "src/vst3/eii_vst3_ids.h",
    "src/vst3/panel_control_fallback.h",
    "src/vst3/plugin_editor.h",
    "src/vst3/plugin_engine.h"
  ],
  "unchanged": "Firmware,CPU/controller/ADC clocks and implementation, nativeDAC/deglitch, allVCF/VCA/DSP and calibration, sampleinputgain, finalmaster. No purebranch merge."
}

Host-provided automation remains sample-offset ordered. The fallback only handles panel values without a host point for that control in the current block. It is inserted before explicitoffset0events (including MIDI wheel bridging). Failed queue admission does not acknowledge a value. No mutex/allocation is added to process(). The UI still updates its local position immediately and preserves host begin/perform/end gesture notifications. PhysicalADC follows the next processed host block, which can add up to one block latency on noecho hosts. A stopped/nonprocessed host cannot advance the firmware, asbefore.

No firmware/ADC timer change or proven Windows arp acceptance. If ownerissue persists, retain sourcefinding but continue exactfirmware event investigation; don't call this a cure based on unit tests. VCA behavior/calibration and oldproject/globalprofileformat unchanged.
