#!/usr/bin/env python3
"""Fail closed on any accepted plugin command change except output/profile paths.
Does not build. Profile/compiler bytes must match the accepted lock.
"""
from pathlib import Path
import hashlib,json,shlex,sys
ROOT=Path(__file__).resolve().parent
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def command(text):
 lines=[line for line in text.splitlines() if 'clang++' in line and not line.lstrip().startswith('#')]
 if len(lines)!=1:raise ValueError('Expected one compiler invocation')
 return shlex.split(lines[0])
def normalized(tokens):
 result=['<EXACT_COMPILER_HASH>'];i=1
 while i<len(tokens):
  t=tokens[i]
  if t=='-o':
   if i+1>=len(tokens):raise ValueError('No output path')
   result+=['-o','<OUTPUT>'];i+=2;continue
  if t.startswith('-fprofile-instr-use='):t='-fprofile-instr-use=<EXACT_OWNER_PGO_HASH>'
  result.append(t);i+=1
 return result

def verify(candidate,lock):
 base=command((ROOT/'accepted-plugin-build.sh').read_text());actual=command(Path(candidate).read_text())
 if normalized(actual)!=normalized(base):raise ValueError('STOP: plugin build command differs from accepted recipe')
 if sha(actual[0])!=lock['compiler_sha256']:raise ValueError('STOP: compiler bytes differ')
 if sha(Path(actual[0]).parent/'clang')!=lock['compiler_backend_sha256']:raise ValueError('STOP: compiler backend differs')
 profiles=[t.split('=',1)[1] for t in actual if t.startswith('-fprofile-instr-use=')]
 if len(profiles)!=1 or sha(profiles[0])!=lock['original_owner_pgo_sha256']:raise ValueError('STOP: owner PGO differs')
 print('PASS: exact compiler/profile and accepted command flags/input/link order; output path may differ. No build performed.')
if __name__=='__main__':verify(sys.argv[1],json.loads((ROOT/'build-recipe-lock.json').read_text()))
