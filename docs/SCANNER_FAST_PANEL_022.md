# Switchable custom panel response0.3.22

{
  "version": "0.3.22-custom-fast-panel",
  "accepted": false,
  "owner_scope": "Delay also exists witharpOFF andnoNoteOn duringownpanel menu edits; Frequency isclearrepro. Owner confirmsARPRange explained separateactivationproblem.021handoff didnotremove remainingdelay.",
  "owner_choice": "2026-10-27 selects switchablefastmode withoriginalbehavior default (explicit preference).",
  "findings": "Directactualfirmware/noGUI/noDSP reproducesFreqstep460.9ms. ScannerROM hash478cf8e0956aadf27c51c58a26f24926d4e6f2658a991877dd3d0f3aa98a6227 executes2cascadedfixedpoint halfaverages for ADCchannels4..7.16entry schedule polls eachslider once, channels0/2/3 fourtimes. ObservedFreq31,60,81,96,105,110,113,115,117 matches averaging model. No universalHWtimingclaim; this explainsmeasuredstep.",
  "corrected_wiring": "ServicePDF69sheet7-6 IC45CT1 pin22 <- /PRXC SMPTE receiveclock; ZC0 pin7NC. PDF79sheet7-16 IC130/Qbar idlehigh suppliesclock; PDF120sheet7-56 labelsSMPTEReceiveClock. Previous ZCTO0->CT1 synthesizedspurious2k/sIRQ. Explicitclockinputnowidlehigh. Pin/oracle testsPASS, oldbugreproduced. Thisfixalone doesnotremove averagingdelay.",
  "fast_mode": "Settings/Devices > Fast panel response (CUSTOM,nextboot). DefaultOFF, booleanprojectsettingfastpanel. Afterselection Boot/Reset. Statusshowsrunning andnextbootmodes. Onlyfreshloaded knownROMcopy patched beforeCPUstarts; oneopcodeJRNC->JR at0x0c1e takesexistingrawADC/hysteresis path. FNV64wholeROM+opcodeguards; originalSHA256recorded. UnsupportedROMrejectedwithoutfilechange. No ROMbytes inGit/artifact, sourcefilesunchanged.",
  "fast_measurement": "Privatecopy experiment withsamecorrectedCTC clocks/menu/bank, noteoff, noGUI/DSP:19.6ms vs460.9ms stock. Sameimage producedbyactualloader helper; knownownerROM patch/idempotence/whole-imageguard andstateolddefault/roundtrip testsPASS. Not guaranteedWindowsGUIlatency.",
  "source_policy": "Retain021singleclockhandoff asindependentordering correction; notcreditedwithlatencyfix. Retainaccepted020VCA. CorrectedCTCclock +customoption only; noCPU/ADC rate boost, noRAMinjection, nofilter/sampleinput/finalmastercalibration edits. Originalpinnedrelease recipe/strictPGOvalid.",
  "native_acceptance": "PENDING Windowshost ownpanelFreq/VCA/othermenus, buttons,wheels,arpwhileediting, projectSaveLoad. DefaultOFFstillhasoriginalFWaveraging; usermustenablefastthenBoot. OriginalmodecanberestoredwithOFFthenBoot.",
  "dll_sha256": "664b003fab93bc07dbeac25710f3e5324b08a1cb20410a029ed34c3f18792093"
}

Validation: tests/scanner_clock_pin_tests.cpp testsactualMachine withdirect/eventCTC: oldinternalZC0 pulses triggerunwantedCT1IRQ; fixedidleexternalpin ignoresZC0 andcounts onlyinjectedexternalfallingedges. ROM/ADC/FWtiming findings are not copiedROMassets; sourcefirmware isownerprovided. Userhasexplicitlychosen customfastbehavior; defaultstock ispreserved. See validation/scanner-panel-022-2026-10-27 evidence. WindowsGUI andremainingarpcadence requireowneracceptance.
