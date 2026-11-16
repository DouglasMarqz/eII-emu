#!/usr/bin/env python3
"""Isolated, explicitly counterfactual SIO experiment; never a default boot mode."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent.parent
EXPECTED = {
    'main': '5082f8f815e873803929dc570ab8e4083646c6d79368b17b931a0328f06de226',
    'scanner': '478cf8e0956aadf27c51c58a26f24926d4e6f2658a991877dd3d0f3aa98a6227',
    'prom135': '725cd24f9e98f201efcd7cfac4e07f38713495fa960b3f37664e76bd079927f5',
    'prom137': '874b9ec1214977fb95fa77b337b7cb254d243c9a143547af3b440cd2d9ff9116',
    'image': '04d603a9e664ddfd3ac5c54e09d7c57e1c638b8fdc2d0bae83b25237becf7033',
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_hashes():
    files = sorted((ROOT / 'src').rglob('*.h')) + sorted((ROOT / 'src').rglob('*.cpp'))
    files += sorted((ROOT / 'vendor/chips').glob('*.h'))
    files += [ROOT / 'tools/sio_hunt_lab_probe.cpp']
    return {str(p.relative_to(ROOT)): digest(p) for p in files}


def replace_once(text, anchor, replacement):
    if text.count(anchor) != 1:
        raise ValueError('Source changed: isolated experiment anchor is not unique')
    return text.replace(anchor, replacement)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in EXPECTED:
        parser.add_argument(name, type=Path)
    parser.add_argument('--profile', required=True, choices=['hxc2026', 'report2010'])
    parser.add_argument('--policy', required=True, choices=['retain', 'reset-on-hunt-hypothesis'])
    parser.add_argument('--corrupt-first-data-bit', action='store_true',
                        help='Flip one FM data slot on cylinder0/side0, retain generated CRC and clock slots')
    args = parser.parse_args()
    assets = {name: getattr(args, name).resolve() for name in EXPECTED}
    hashes = {name: digest(path) for name, path in assets.items()}
    if hashes != EXPECTED:
        raise ValueError('This fixed4M-clock experiment requires identified main2.1/scanner3.1/PROMs/ZM325 assets')
    before = source_hashes()
    patches = []
    with tempfile.TemporaryDirectory(prefix='eii-sio-hunt-lab-') as tmp:
        tmp = Path(tmp)
        shutil.copytree(ROOT / 'src', tmp / 'src')
        if args.policy == 'reset-on-hunt-hypothesis':
            p = tmp / 'src/sio_sync_receiver.h'
            original = p.read_text()
            anchor = 'void enterHunt(bool disabledReceiver=false) noexcept {'
            p.write_text(replace_once(original, anchor, anchor +
                         '\n        resetCrc(); // ISOLATED UNVERIFIED HYPOTHESIS ONLY'))
            patches.append({'path': 'src/sio_sync_receiver.h', 'sha256': digest(p),
                            'purpose': 'Assume CRC zero-initialization on Hunt/receiver disable; NOT silicon evidence'})
        if args.corrupt_first_data_bit:
            p = tmp / 'src/disk_read_path.h'
            anchor = 'media_[i].id=id;}'
            p.write_text(replace_once(p.read_text(), anchor,
                         'if(id==0){auto& t=media_[i].track;t.transitions[t.dataBit*2+1]^=1;}' + anchor))
            patches.append({'path': 'src/disk_read_path.h', 'sha256': digest(p),
                            'purpose': 'Flip only the first payload FM data slot on track/side0; do not recompute CRC'})
        command = [os.environ.get('CXX', 'g++'), '-std=c++20', '-O2', '-Wall', '-Wextra',
                   '-I' + str(tmp / 'src'), '-I' + str(ROOT / 'vendor/chips'),
                   str(tmp / 'src/chips.cpp'), str(tmp / 'src/machine.cpp'),
                   str(ROOT / 'tools/sio_hunt_lab_probe.cpp'), '-o', str(tmp / 'probe')]
        subprocess.run(command, check=True, capture_output=True, text=True)
        result = subprocess.run([str(tmp / 'probe'), *(str(assets[n]) for n in EXPECTED), args.profile],
                                check=True, capture_output=True, text=True)
    if before != source_hashes() or hashes != {name: digest(path) for name, path in assets.items()}:
        raise RuntimeError('Repository sources or original assets changed during the experiment')
    summary = {}
    reads = []
    for line in result.stdout.splitlines():
        key, value = line.split('\t', 1)
        if key == 'CrcRead':
            fields = ['tick', 'pc', 'rr1', 'crc', 'dma_transfers', 'ram_known', 'ram_equal']
            reads.append(dict(zip(fields, map(int, value.split(',')), strict=True)))
        elif key in ('Lcd0Hex', 'Lcd1Hex'):
            summary[key.replace('Hex', '')] = bytes.fromhex(value).decode('latin1')
        else:
            summary[key] = int(value)
    passed = len(reads) >= 2 and not (reads[0]['rr1'] & 64)
    if len(reads) >= 2:
        block = reads[1]
        accept = args.policy == 'reset-on-hunt-hypothesis' and not args.corrupt_first_data_bit
        passed &= block['ram_known'] == 3584 and block['ram_equal'] == 3584 - int(args.corrupt_first_data_bit)
        passed &= block['dma_transfers'] == 3588 and bool(block['rr1'] & 64) != accept
        passed &= (summary['LoadedCodeFetches'] > 0) == accept
        passed &= not accept or summary['Lcd0'] == 'Testing Memory  '
    output = {
        'experiment': 'isolated-sio-hunt-initialization-sensitivity', 'cycles': 4000000,
        'policy': args.policy, 'crc_reset_verified_on_silicon': False,
        'corrupt_first_data_bit': args.corrupt_first_data_bit, 'profile': args.profile,
        'default_model_changed': False, 'assets_changed': False,
        'drive': {'reference': 'SA465 nominal', 'cylinder': 37, 'phase_cpu_ticks': 400000,
                  'ls221_source_edges': 87, 'readpulse_source_edges': 8},
        'asset_sha256': hashes, 'source_sha256': before, 'isolated_mutations': patches,
        'crc_reads': reads, 'observed': summary, 'expected_trial_result_pass': bool(passed),
        'limitation': 'Conditional diagnostic execution under an unverified reset hypothesis is NOT a verified EII boot. No RR1 override, ROM patch or decoded-sector RAM copy.',
    }
    print(json.dumps(output, indent=2))
    return 0 if passed else 1


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as exc:
        raise SystemExit(str(exc)) from exc
