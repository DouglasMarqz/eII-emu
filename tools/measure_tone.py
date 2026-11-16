#!/usr/bin/env python3
"""Measure a simultaneous input/output sine capture; never synthesize calibration."""
import argparse
import csv
import json
import math
from pathlib import Path

import numpy as np


def measure(path, frequency, kind, fixture, program_code, master_hz, skip_seconds=0.0):
    if not (math.isfinite(frequency) and frequency > 0 and math.isfinite(master_hz) and master_hz > 0):
        raise ValueError('Frequency and master clock must be finite and positive')
    if kind not in ('hardware', 'simulation') or not fixture.strip() or not 0 <= program_code < 64:
        raise ValueError('Capture kind, fixture and six-bit program code are required')
    if not math.isfinite(skip_seconds) or skip_seconds < 0:
        raise ValueError('Skip seconds must be finite and nonnegative')
    with Path(path).open(newline='') as source:
        reader = csv.DictReader(line for line in source if not line.startswith('#'))
        if not {'time_s', 'input_volts', 'output_volts'} <= set(reader.fieldnames or []):
            raise ValueError('CSV requires time_s,input_volts,output_volts')
        rows = [[float(row[k]) for k in ('time_s', 'input_volts', 'output_volts')] for row in reader]
    capture = np.asarray(rows, dtype=float)
    if capture.ndim != 2 or capture.shape[0] < 8 or not np.isfinite(capture).all():
        raise ValueError('Capture requires at least eight finite simultaneous samples')
    if np.any(np.diff(capture[:, 0]) <= 0):
        raise ValueError('Capture timestamps must strictly increase')
    capture = capture[capture[:, 0] >= capture[0, 0] + skip_seconds]
    if len(capture) < 8 or (capture[-1, 0] - capture[0, 0]) * frequency < 10:
        raise ValueError('Retained capture must span at least ten tone cycles')
    if frequency * np.max(np.diff(capture[:, 0])) >= 0.5:
        raise ValueError('Tone exceeds Nyquist at the largest sample gap')
    time = capture[:, 0] - capture[0, 0]
    angle = 2 * np.pi * frequency * time
    design = np.column_stack((np.cos(angle), np.sin(angle), np.ones_like(time)))
    coefficients, _, rank, _ = np.linalg.lstsq(design, capture[:, 1:], rcond=None)
    if rank != 3:
        raise ValueError('Capture cannot resolve the tone fit')
    phasors = coefficients[0] - 1j * coefficients[1]
    roundoff = 32 * np.finfo(float).eps
    if abs(phasors[0]) <= roundoff * max(1., np.max(np.abs(capture[:, 1]))):
        raise ValueError('Input tone amplitude is unresolved')
    if abs(phasors[1]) <= roundoff * max(1., np.max(np.abs(capture[:, 2]))):
        raise ValueError('Output tone amplitude is unresolved')
    transfer = phasors[1] / phasors[0]
    if not np.isfinite(transfer):
        raise ValueError('Nonfinite transfer estimate')
    residual = capture[:, 1:] - design @ coefficients
    return {
        'capture_kind': kind, 'fixture': fixture, 'source_file': str(path),
        'program_code_hex': f'{program_code:02X}', 'master_hz': master_hz,
        'frequency_hz': frequency, 'samples': len(capture),
        'duration_s': float(time[-1]), 'skipped_seconds': skip_seconds,
        'input_peak_volts': float(abs(phasors[0])), 'output_peak_volts': float(abs(phasors[1])),
        'gain_db': float(20 * np.log10(abs(transfer))), 'phase_deg': float(np.angle(transfer, deg=True)),
        'input_dc_volts': float(coefficients[2, 0]), 'output_dc_volts': float(coefficients[2, 1]),
        'input_residual_rms_volts': float(np.sqrt(np.mean(residual[:, 0] ** 2))),
        'output_residual_rms_volts': float(np.sqrt(np.mean(residual[:, 1] ** 2))),
        'calibration_status': 'unvalidated_capture_measurement',
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--frequency', required=True, type=float)
    parser.add_argument('--kind', required=True, choices=('hardware', 'simulation'))
    parser.add_argument('--fixture', required=True)
    parser.add_argument('--program-code', required=True, type=lambda s: int(s, 16))
    parser.add_argument('--master-hz', required=True, type=float)
    parser.add_argument('--skip-seconds', type=float, default=0.)
    args = parser.parse_args()
    try:
        result = measure(args.capture, args.frequency, args.kind, args.fixture, args.program_code, args.master_hz, args.skip_seconds)
    except (ValueError, OSError, KeyError) as error:
        parser.exit(1, f'ERROR: {error}\n')
    print(json.dumps(result, indent=2, allow_nan=False))


if __name__ == '__main__':
    main()
