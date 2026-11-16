#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
"${CXX:-g++}" -std=c++20 -O3 -Wall -Wextra -Werror -Isrc tests/native_audio_tests.cpp -o build/native_audio_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Werror -Isrc tests/sample_input_tests.cpp -o build/sample_input_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Werror -Isrc tests/sampling_phase_schedule_tests.cpp -o build/sampling_phase_schedule_tests
build/sampling_phase_schedule_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Werror -Isrc tests/sample_input_live_tests.cpp -o build/sample_input_live_tests
build/sample_input_live_tests
build/sample_input_tests
build/native_audio_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -pthread -c vendor/ssm2045-chip/src/chip.cpp -o build/ssm2045_chip.o
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -pthread -Isrc -Ivendor/chips -c src/chips.cpp -o build/chips.o
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -pthread -Isrc -Ivendor/chips -c src/machine.cpp -o build/machine.o
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tools/boot_probe.cpp -o build/eii_probe
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tools/production_controller_probe.cpp -o build/production_controller_probe
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tools/production_panel_probe.cpp -o build/production_panel_probe
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tools/sample_trace_probe.cpp -o build/sample_trace_probe
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tools/sample_wav_probe.cpp -o build/sample_wav_probe
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/digital_tests.cpp -o build/digital_tests
build/digital_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc tests/analog_tests.cpp -o build/analog_tests
build/analog_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/control_voltage_tests.cpp -o build/control_voltage_tests
build/control_voltage_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/channel_timer_tests.cpp -o build/channel_timer_tests
build/channel_timer_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/sample_machine_tests.cpp -o build/sample_machine_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/sample_analog_tests.cpp -o build/sample_analog_tests
build/sample_analog_tests
build/sample_machine_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tools/pio_probe.cpp -o build/pio_probe


"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/channel_request_tests.cpp -o build/channel_request_tests
build/channel_request_tests

"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/channel_service_tests.cpp -o build/channel_service_tests
build/channel_service_tests

"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/controller_timing_tests.cpp -o build/controller_timing_tests
build/controller_timing_tests

"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/controller_prom_tests.cpp -o build/controller_prom_tests
build/controller_prom_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/controller_register_tests.cpp -o build/controller_register_tests
build/controller_register_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/controller_data_tests.cpp -o build/controller_data_tests
build/controller_data_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/controller_alu_tests.cpp -o build/controller_alu_tests
build/controller_alu_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/dram_cpu_tests.cpp -o build/dram_cpu_tests
build/dram_cpu_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc tools/dram_probe.cpp -o build/dram_probe

"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc tests/controller_upper_paths_tests.cpp -o build/controller_upper_paths_tests
build/controller_upper_paths_tests

"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/sample_transport_tests.cpp -o build/sample_transport_tests
build/sample_transport_tests

"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc tests/playback_analog_tests.cpp -o build/playback_analog_tests
build/playback_analog_tests

"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/switched_analog_tests.cpp -o build/switched_analog_tests
build/switched_analog_tests

"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/clocked_playback_tests.cpp -o build/clocked_playback_tests
build/clocked_playback_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tools/audio_probe.cpp -o build/audio_probe

"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc tests/s3528_curve_tests.cpp -o build/s3528_curve_tests
build/s3528_curve_tests

"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/scanner_controls_tests.cpp -o build/scanner_controls_tests
build/scanner_controls_tests

"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/floppy_mechanics_tests.cpp -o build/floppy_mechanics_tests
build/floppy_mechanics_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tools/floppy_probe.cpp -o build/floppy_probe

"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc tests/floppy_image_tests.cpp -o build/floppy_image_tests
build/floppy_image_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc tools/disk_image_probe.cpp -o build/disk_image_probe

"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/sio_receive_tests.cpp -o build/sio_receive_tests
build/sio_receive_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tools/sio_crc_probe.cpp -o build/sio_crc_probe
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/disk_dma_tests.cpp -o build/disk_dma_tests
build/disk_dma_tests

"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/dram_power_on_tests.cpp -o build/dram_power_on_tests
build/dram_power_on_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tools/dram_startup_probe.cpp -o build/dram_startup_probe

"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc tests/hfe_tests.cpp -o build/hfe_tests
build/hfe_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc tools/hfe_probe.cpp -o build/hfe_probe

"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -pthread -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/front_panel_session_tests.cpp -o build/front_panel_session_tests
build/front_panel_session_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -pthread -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tools/front_panel_probe.cpp -o build/front_panel_probe
"${CXX:-g++}" -std=c++20 -O3 -flto=auto -Wall -Wextra -pthread -Isrc -Ivendor/chips src/chips.cpp src/machine.cpp vendor/ssm2045-chip/src/chip.cpp tools/worker_runtime_probe.cpp -o build/worker_runtime_probe

"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc tests/runtime_equivalence_tests.cpp -o build/runtime_equivalence_tests
build/runtime_equivalence_tests
"${CXX:-g++}" -std=c++20 -O3 -flto=auto -Wall -Wextra -pthread -Isrc -Ivendor/chips src/chips.cpp src/machine.cpp vendor/ssm2045-chip/src/chip.cpp tools/runtime_benchmark.cpp -o build/runtime_benchmark

"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc tests/event_timing_tests.cpp -o build/event_timing_tests
build/event_timing_tests

for runtime_test in ctc_event_runtime dram_event_runtime floppy_event_runtime sio_event_runtime controller_concrete controller_power_on alu_carry_parallel voice_transaction voice_runtime controller_span; do
    "${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o "tests/${runtime_test}_tests.cpp" -o "build/${runtime_test}_tests"
    "build/${runtime_test}_tests"
done

"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -pthread -Isrc build/ssm2045_chip.o tests/runtime_audio_tests.cpp -o build/runtime_audio_tests
build/runtime_audio_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/diagnostics_tests.cpp -o build/diagnostics_tests
build/diagnostics_tests

"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -pthread -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/midi_serial_tests.cpp -o build/midi_serial_tests
build/midi_serial_tests
"${CXX:-g++}" -std=c++20 -O3 -DNDEBUG -flto=auto -pthread -Isrc -Ivendor/chips src/chips.cpp src/machine.cpp vendor/ssm2045-chip/src/chip.cpp tools/midi_play_probe.cpp -o build/midi_play_probe
"${CXX:-g++}" -std=c++20 -O3 -DNDEBUG -flto=auto -Wall -Wextra -Isrc -Ivendor/chips src/chips.cpp src/machine.cpp vendor/ssm2045-chip/src/chip.cpp tools/runtime_cost_probe.cpp -o build/runtime_cost_probe
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips -pthread tests/production_register_tests.cpp -o build/production_register_tests
build/production_register_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips -pthread tests/production_dram_port_tests.cpp -o build/production_dram_port_tests
build/production_dram_port_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc -Ivendor/chips -DEII_DIAGNOSTICS=1 tests/production_controller_tests.cpp -o build/production_controller_tests
build/production_controller_tests


"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -pthread -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/production_failure_tests.cpp -o build/production_failure_tests
build/production_failure_tests

"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -pthread -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/dram_underlay_tests.cpp -o build/dram_underlay_tests
build/dram_underlay_tests

"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -pthread -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/production_publication_tests.cpp -o build/production_publication_tests
build/production_publication_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc tests/midi_host_tools_tests.cpp -o build/midi_host_tools_tests
build/midi_host_tools_tests

"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -pthread -Isrc tests/chip_audio_tests.cpp build/ssm2045_chip.o -o build/chip_audio_tests
build/chip_audio_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Ivendor/ssm2045-chip/include vendor/ssm2045-chip/tests/chip_tests.cpp build/ssm2045_chip.o -o build/ssm2045_chip_tests
build/ssm2045_chip_tests

"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Isrc tests/panel_report_text_tests.cpp -o build/panel_report_text_tests
build/panel_report_text_tests
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -pthread -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o tests/live_report_session_tests.cpp -o build/live_report_session_tests
build/live_report_session_tests

for trace_test in performance_trace performance_trace_session s3528_runtime s3528_voice s3528_pcm native_pcm; do
    "${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -pthread -Isrc -Ivendor/chips build/chips.o build/machine.o build/ssm2045_chip.o "tests/${trace_test}_tests.cpp" -o "build/${trace_test}_tests"
    "build/${trace_test}_tests"
done
if [ "${EII_BUILD_TIMING_LAB:-0}" = 1 ]; then
    "${CXX:-g++}" -std=c++20 -O3 -DNDEBUG -DEII_DIAGNOSTICS=0 -DEII_PLAYBACK_TIMING_LAB=1 -pthread -Isrc -Ivendor/chips -c src/machine.cpp -o build/timing_lab_machine.o
    for timing_lab in playback_timing_observer_tests playback_fidelity_probe native_timing_tests native_audio_probe; do
        timing_source="tests/${timing_lab}.cpp"
        case "$timing_lab" in *_probe) timing_source="tools/${timing_lab}.cpp";; esac
        "${CXX:-g++}" -std=c++20 -O3 -DNDEBUG -DEII_DIAGNOSTICS=0 -DEII_PLAYBACK_TIMING_LAB=1 -pthread -Isrc -Ivendor/chips build/chips.o build/timing_lab_machine.o build/ssm2045_chip.o "$timing_source" -o "build/${timing_lab}"
    done
    build/playback_timing_observer_tests
    build/native_timing_tests
fi

"${CXX:-g++}" -std=c++20 -O3 -pthread -Isrc -Ivendor/chips vendor/ssm2045-chip/src/chip.cpp tests/legacy_q_tests.cpp -o build/legacy_q_tests
build/legacy_q_tests
