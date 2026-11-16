# Native arp-panel trace — 2026-10-27

{
  "owner_report": "Built-in A/B/C/D physical panel sliders while editing ANY menuparameter (VCA/etc., not justVCF) slow audiblearp andLCD; menu/buttons also notsnappy. All3models; returnsnormal afterrelease. Not external MIDIcontroller.",
  "authorization": "2026-10-27 owner explicitly ACKs and authorizes savingcustombranch after priorautomaticreviewblocked documentationwrite. Do not reask for thissave.",
  "native_trace": {
    "filename": "EII-sampling-trace ARP ERROR.csv",
    "library_file_id": "libfile_65651dea6fe08191bcc9c33280ec0bf1",
    "sha256": "ff445a4832ce538298a183160d77bd0c1fb818600931ba6bf007222dad566c7a",
    "rows": 1478,
    "duration_s": 34.109311,
    "model": "EiiiCustom enum5",
    "effective_host_hz": 96000.06285673728,
    "host_callbacks_hz": 375.00024553413,
    "machine_speed_ratio": 1.0000006523145542,
    "rebase_delta": 0,
    "offset_delta": 0,
    "output_underrun_delta": 0,
    "dsp_drop_delta": 0,
    "input_loss_delta": 0,
    "native_errors_delta": 0,
    "max_owner_lag_ms": 2.66675,
    "machine_load_range": [
      0.6973212141303508,
      0.7901049875245184
    ],
    "dsp_load_range": [
      0.1417238339363045,
      0.18196237256851394
    ],
    "historical_counts": "Initialrebases1/underruns24183 areprior accumulated counts, NOT newevents inthistrace.",
    "conclusion": "Globalhost/machineclock slowdown, newtimefreezes and audio/DSPpacketloss are not present inthiscapture. One-channel ACV cadence changes butnot a full8voice note scheduler trace, so exactinternal arp causeunproven."
  },
  "direct_firmware_probe": "Real readonlyownerMarcato bank +productioncontroller, noGUI/noDSP; activearp120BPM andliveFilter1 menu;4s steady/moving/steady. CVattackedge average125.87097/125.73333/125.87097ms,1ms resolution+threshold/debounce. No gross slowdown here. Singlelocalcase, no Windows/host performanceproof.",
  "source_fault": "VST3 panel hook both postsimmediate ownerADCcommand and notifieshost parameter. A laterhostecho can reapply older values after newer GUIcommand. Separate clockdomains can cause physicalpin rewinds; sourceordering fixture reproduces this possibility, not actualownerhostecho telemetry.",
  "next": "Validate isolated single-source hostclock handoff candidate, preserving normal hostautomation and no-echo hosts. Ownerhostarp acceptance remains required. No speculative clock/filter edits.",
  "evidence": "validation/arp-panel-2026-10-27/"
}

Later candidate: see PANEL_HANDOFF_021.md. Exactnativecapture rules out globaltimefreeze inthisrecording, not allpossiblefirmware faults. Retain accepted020VCA and require owner021hostacceptance.
