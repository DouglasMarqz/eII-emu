# Custom EIII CEM3387 filter (Emulator II Custom)

Branch: custom/eiii-cem3387. Pure feature/vst3 remains at 34dbbfbe6ddbabb43e03fd37090da15b3076afac, accepted runtime0.3.14. This branch includes the separate0.3.17 fine-service/global-save UI candidate; it does not include rejected0.3.15 AVX integration.

Imported portable CEM3387 V2 files from Tuth/Emulated-3 commit4028d1496f5b27fdcda3e4a4c6ecd5161504a8af, with their original license retained in vendor/eiii-cem.

New model: EIII CEM3387 VCF - custom / EII VCA (96k). It uses the original EIII3+4pole VCF output. EIII VCA/pan/output current is discarded; existing EII DAC/deglitch, S3528 switch, coupling, VCA, channel order, FIR, host trim and sampler/firmware timing remain downstream/upstream as before. No duplicate active SSM solver: custom smoke asserts zero SSM solver samples.

Adapter: existing EII board-smoothed FC and nominal SSM frequency law provide a target frequency; the CEM Fc2 law is inverted to set its CV. Q current divided by the EII normalization current maps to the CEM no-resonance/oscillation CV endpoints, bounded to0..4.5V. CEM Fc1 stays open at4.96V. Original EIII CV conditioning/nonlinear limits remain. The imported optional noise generator is disabled for this migration. This mapping is a custom bridge, not a claim that an EII becomes a hardware EIII or that SSM tuning controls tune CEM silicon.

Select the custom model in Settings > VCF / VCA model. New custom instances start with it selected. EIII has a separate project calibration array; existing SSM and Legacy state indices are retained. EIII Filter Service shows an explanatory message instead of misleading SSM chip controls; use sampler FC/Q for this first migration. Existing pure profiles are not overwritten. Custom global service profiles live under EmulatorII-Custom.

Separate VST3 class ID0E11CE03 suffix, name Emulator II Custom and bundle EmulatorII-Custom.vst3 allow coexistence with the pure instrument.

Validation: Linux custom8voice96/192 finite/nonzero output, FC/Q sweep/reset, independent custom profile/state roundtrip PASS. Four original-model PCM fingerprints match0.3.17 exactly for6000eightvoice frames/model/rate. This is not native Windows/DAW acceptance.

Release build gate: original pinned compiler/backend/O3/LTO/static/diagnostics0/PGO and strict mismatch errors retained. Existing0314 profile fails1of563functions; no DLL released and no guard relaxed. Separate instrumented Windows trainer compiles PASS for ssm192,legacy,eiii read-only ROM/bank boot/play/release/monitor/RAMrecord workloads. Run-PGO.cmd returns three source-matched raw profiles. After native return, merge with matching llvm-profdata and rerun the exact strict build using the fresh profile. Do not use training timing as performance evidence.

## Native profile return and final host-test DLL

Owner PGO-CUSTOM-EIII-RETURN-20261025-182827-442c91 verified against the full expected manifest/source and all three raw/log completions. Fresh profile merge814828abc168a4e945711bddabab0e4252d06f7c844128dd9534a0e959a78f65. The previously blocked strict recipe now PASSES without changing code, flags or mismatch guards. FinalDLL1fb8ec1e3a7e264d1a86d15ed63788a24c74e1c25d9529d2003360d44da57a13,2824704bytes, no generation instrumentation. Prior block paragraph is historical. Pure branch unchanged; final native Windows/DAW host acceptance pending. Evidence: validation/custom-eiii-2026-10-25/final-build/.

## CEM Filter Service candidate0.3.19

Owner confirms0.3.18 migration succeeds, reports resonant presets quieter while zero-Q presets match other models, and requests experimental controls. This update enables the custom service with an independent CEM group, real EII board/VCA/native groups and no misleading SSM chip knobs. Fields: input drive0.01..16x, output makeup0.0625..16x, Q passband makeup0..100%, Q scale0..2x, Fc scale0.125..8x, Fc1CV0..4.96V. Neutral defaults reproduce0.3.18 bitwise in the tested direct-bank sequence.

Q makeup multiplies the VCF output by1 + amount/100 * currentSmoothedLoopK. For the model's linear four-pole low-frequency response1/(1+k),100% cancels this nominal passband attenuation. It is not perceived-loudness normalization or peak protection: the resonant peak/other spectral content also rises and may clip. At settled zeroQ,k=0, so makeup is exactly neutral. Input drive and output makeup affect zero-Q presets too; both start neutral. Gain uses direct factors; no added per-sample transcendental or DSP state reset.

Calibration schema3 adds six fields; real0.3.18 V1/V2/project fixtures remain readable with neutral CEM defaults. Importing an older profile preserves current new CEM fields. Global schema2 stores all three profiles; old two-model schema1 remains readable and leaves an existing custom profile intact on Load. DAW project restore remains authoritative over new-instance global defaults. VST3 customID unchanged. Final Windows UI/host regression pending. Pure branch not modified.
