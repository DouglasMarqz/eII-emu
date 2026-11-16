#!/usr/bin/env python3
"""Recognize user-supplied EII ROMs by digest; never downloads firmware."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

def prepare(source, destination):
    catalog=json.loads(Path(__file__).with_name('rom_catalog.json').read_text())
    known={x['sha256']:x for x in catalog}
    destination.mkdir(parents=True,exist_ok=True)
    found=set()
    def accept(data):
        item=known.get(hashlib.sha256(data).hexdigest())
        if not item: return
        target=destination/item['target']
        if target.exists() and target.read_bytes()!=data:
            raise RuntimeError(f'Refusing to overwrite a different ROM: {target}')
        target.write_bytes(data);found.add(item['target'])
    for p in sorted(source.iterdir()):
        if p.suffix.lower()=='.zip':
            with zipfile.ZipFile(p) as z:
                for info in z.infolist():
                    if not info.is_dir() and info.file_size in (512,8192): accept(z.read(info))
        elif p.is_file() and p.stat().st_size in (512,8192): accept(p.read_bytes())
    return found

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path,help='Folder with your original ZIPs or loose ROM files')
    parser.add_argument('destination',type=Path,help='Your private ROM output folder')
    args=parser.parse_args()
    found=prepare(args.source,args.destination)
    print('Recognized:',', '.join(sorted(found)))
    missing={'main.bin','scanner.bin','ic135.bin','ic137.bin'}-found
    if missing: raise SystemExit('Missing recognized ROMs: '+', '.join(sorted(missing)))
