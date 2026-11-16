#!/usr/bin/env python3
"""Package only explicitly selected source, diagnostics and built executables."""
from pathlib import Path
import argparse
import hashlib
import json
import zipfile

root=Path(__file__).resolve().parent.parent
parser=argparse.ArgumentParser()
parser.add_argument('output',type=Path)
args=parser.parse_args()
source=[root/'CMakeLists.txt',root/'README.md',root/'LICENSE',root/'.gitignore']
for folder in ('src','tools','tests','vendor','docs'):
    source += [p for p in (root/folder).rglob('*') if p.is_file() and '__pycache__' not in p.parts]
files={'Source/'+p.relative_to(root).as_posix():p for p in source}
for name in ('EII-Bringup.exe','eii_probe.exe','digital_tests.exe'):
    p=root/'build/win64'/name
    if not p.is_file():raise SystemExit('Missing build: '+name)
    files[name]=p
for name in ('Prepare-ROMs.ps1','prepare_roms.py','rom_catalog.json'):
    files['Tools/'+name]=root/'tools'/name
for name in ('boot21-scan31.txt','boot23-scan31.txt','boot21-scan21.txt','diag-help.txt','diag-testhelp.txt','sanitizer.txt'):
    files['Results/'+name]=root/'build'/name
info={
    'project':'EII digital bring-up 0.1','date':'2026-10-17',
    'status':'Diagnostic prototype; not a playable sampler or complete OS boot',
    'platform':'Windows x64/UCRT; LLVM-MinGW 20261006; static runtime',
    'native_windows_execution_tested':False,'firmware_included':False,
    'chips_revision':'9e88298ce56319953ac7a43213a1120359f7a3a6',
    'sha256':{n:hashlib.sha256(p.read_bytes()).hexdigest() for n,p in sorted(files.items())}
}
args.output.parent.mkdir(parents=True,exist_ok=True)
with zipfile.ZipFile(args.output,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for n,p in sorted(files.items()):
        if p.suffix.lower() in ('.bin','.e2o','.emuiifd','.pdf','.zip'):
            raise SystemExit('Unexpected private/media file: '+n)
        z.write(p,n)
    z.writestr('BUILD_INFO.json',json.dumps(info,indent=2)+'\n')
    z.write(root/'README.md','README-FIRST.md')
print(args.output)
print(hashlib.sha256(args.output.read_bytes()).hexdigest())
