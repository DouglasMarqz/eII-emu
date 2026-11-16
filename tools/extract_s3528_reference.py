#!/usr/bin/env python3
"""Digitize specified AMI1993 scan plots using visually checked pixel axes."""
import argparse
import hashlib
import json
from pathlib import Path

import fitz
import numpy as np
from PIL import Image


def extract(pdf):
    digest = hashlib.sha256(pdf.read_bytes()).hexdigest()
    if digest != "6f6deb600c1547e1b5be41b9a35b81eb68db51ac3c38e694f4653003e7e47c9e":
        raise ValueError("This extraction grid applies only to the verified AMI1993 scan")
    document = fitz.open(pdf)
    # Exact crop rectangles in PDF points, scale4 pixels/point. The axes below
    # were checked against rendered tick marks, not inferred from OCR text.
    def crop(page, box):
        pix = document[page].get_pixmap(matrix=fitz.Matrix(4, 4), clip=fitz.Rect(*box))
        return np.asarray(Image.frombytes('RGB', [pix.width, pix.height], pix.samples).convert('L'))
    delay = crop(6, (340, 195, 560, 365))
    loss = crop(5, (30, 510, 285, 740))
    data = {
        'source': 'AMI S3528B February1993; uploaded63928.pdf; printed49/50 Figures5/9',
        'source_sha256': digest,
        'provenance': 'digitized_manufacturer_typical_curves_not_independent_hardware',
        'reference_cutoff_hz': 3200,
        'digitization': {'render_scale': 4, 'threshold': 110,
                         'delay_crop_pdf_points': [340, 195, 560, 365],
                         'loss_crop_pdf_points': [30, 510, 285, 740],
                         'delay_axis_pixels': {'x_0hz': 64, 'x_3600hz': 811, 'y_0us': 638, 'y_2000us': 47},
                         'loss_axis_pixels': {'x_0hz': 134, 'x_20000hz': 850, 'y_minus5db': 740, 'y_75db': 134}},
        'reading_allowances': {'delay_us': 30, 'loss_db': 1.5,
                               'note': 'Conservative scan-reading allowances, not manufacturer tolerances'},
        'notch_frequency_ratios': [1.325, 1.62],
        'group_delay': [], 'stopband_loss': []}
    for index, frequency in enumerate([100, 300, 600, 900, 1200, 1500, 1800, 2100, 2400, 2700, 2850, 3000, 3100, 3200, 3250, 3300, 3350, 3400, 3500, 3550]):
        x = round(64+frequency/3600*(811-64))
        ys = np.where((delay[280:615, x-1:x+2] < 110).any(axis=1))[0]+280
        if not len(ys):
            raise ValueError('Missing delay curve pixel')
        y = float(np.median(ys))
        data['group_delay'].append({'frequency_hz': frequency, 'delay_us': round((638-y)*2000/(638-47), 1),
                                    'pixel_x': x, 'pixel_y': y, 'split': 'holdout' if index % 3 == 1 else 'fit'})
    for index, frequency in enumerate([6000, 6500, 7000, 8000, 10000, 12000, 15000, 18000, 19500]):
        x = round(134+frequency/20000*(850-134))
        ys = np.where((loss[90:320, x-1:x+2] < 110).any(axis=1))[0]+90
        if not len(ys):
            raise ValueError('Missing loss curve pixel')
        y = float(np.median(ys))
        data['stopband_loss'].append({'frequency_hz': frequency, 'loss_db': round((740-y)*80/(740-134)-5, 2),
                                     'pixel_x': x, 'pixel_y': y, 'split': 'holdout' if index % 3 == 1 else 'fit'})
    return data


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pdf', type=Path)
    parser.add_argument('--output', type=Path, default=Path('data/s3528b_1993_reference.json'))
    args = parser.parse_args()
    result = extract(args.pdf)
    args.output.write_text(json.dumps(result, indent=2)+'\n')
    print('Extracted20 delay/9 stopband plot readings with10 held-out points; source SHA256:', result['source_sha256'])
