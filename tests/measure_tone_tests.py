"""Analytic sine/delay, DC offsets, harmonic residuals and invalid capture guards."""
import csv
import importlib.util
from pathlib import Path
import tempfile

import numpy as np

spec = importlib.util.spec_from_file_location('measure_tone', Path(__file__).resolve().parents[1] / 'tools' / 'measure_tone.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
with tempfile.TemporaryDirectory() as folder:
    path = Path(folder) / 'capture.csv'
    t = np.arange(48000) / 48000
    frequency = 997.
    # Independent continuous-time reference: amplitude0.25 and delay123us.
    # A second harmonic makes output-fit residual measurable independently.
    x = .2 * np.cos(2*np.pi*frequency*t+.7) + .8
    y = .05 * np.cos(2*np.pi*frequency*(t-.000123)+.7) - .3 + .001 * np.cos(4*np.pi*frequency*t)
    def save(time, inputs, outputs):
        with path.open('w', newline='') as f:
            writer = csv.writer(f);writer.writerow(('time_s', 'input_volts', 'output_volts'));writer.writerows(zip(time, inputs, outputs))
    save(t, x, y)
    result = module.measure(path, frequency, 'simulation', 'analytic-tone-delay', 0x3B, 5e6)
    assert abs(result['gain_db']-20*np.log10(.25)) < 1e-10
    assert abs(result['phase_deg']-(-360*frequency*.000123)) < 1e-9
    assert abs(result['input_dc_volts']-.8) < 1e-12 and abs(result['output_dc_volts']+.3) < 1e-12
    assert abs(result['output_residual_rms_volts']-.001/np.sqrt(2)) < 1e-12
    assert result['capture_kind'] == 'simulation' and result['calibration_status'] == 'unvalidated_capture_measurement'
    for time, inputs, outputs in ((t, np.zeros_like(t), y), (t, x, np.full_like(t, 3.)), (t, x, np.zeros_like(t)), (t[:100], x[:100], y[:100]), (t[::-1], x, y), (t, x*float('nan'), y)):
        save(time, inputs, outputs)
        try:
            module.measure(path, frequency, 'hardware', 'guard', 0x3B, 5e6)
        except ValueError:
            pass
        else:
            raise AssertionError('Invalid capture accepted')
print('PASS: capture-only tone gain/phase/DC/harmonic residual vs analytic delayed sine, simulation/hardware provenance and invalid-capture guards; no physical calibration data supplied')
