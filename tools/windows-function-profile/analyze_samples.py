#!/usr/bin/env python3
"""Resolve sampled IP counts for exact accepted037; no inclusive-stack claim."""
import bisect,csv,json,sys,collections
from pathlib import Path

def resolve(ip,modules,symbols):
 for module in modules:
  base=int(module['base_hex'],16);size=int(module['size'])
  if base<=ip<base+size:
   rva=ip-base
   if module['name'].lower()=='emulatorii.vst3':
    rows=symbols['symbols'];idx=bisect.bisect_right([r['start_rva'] for r in rows],rva)-1
    if idx>=0 and rows[idx]['start_rva']<=rva<rows[idx]['end_rva']:return module['name'],rows[idx]['symbol'],rva
    return module['name'],'[no text symbol]',rva
   return module['name'],'[external module; no symbol map]',rva
 return '[outside recorded modules]','[kernel/unmapped/transient]',ip

def main(folder):
 folder=Path(folder);symbols=json.loads(Path(__file__).with_name('symbols037.json').read_text())
 metadata=(folder/'metadata.txt').read_text(encoding='utf-8-sig')
 if 'dll_sha256='+symbols['dll_sha256'] not in metadata:raise ValueError('Exact037 hash mismatch')
 modules=list(csv.DictReader((folder/'modules.csv').open(encoding='utf-8-sig')))
 threads={r['id']:r['name'] for r in csv.DictReader((folder/'threads.csv').open(encoding='utf-8-sig'))}
 counts=collections.Counter();totals=collections.Counter()
 for row in csv.DictReader((folder/'samples.csv').open()):
  tid=row['thread_id'];ip=int(row['instruction_pointer_hex'],16);n=int(row['sample_count'])
  if tid not in threads or n<0:raise ValueError('Unexpected thread or count')
  module,symbol,rva=resolve(ip,modules,symbols);counts[(tid,module,symbol)]+=n;totals[tid]+=n
 with (folder/'resolved-functions.csv').open('w',newline='') as f:
  w=csv.writer(f);w.writerow(['thread_id','thread_name','module','nearest_text_symbol','samples','percent_selected_thread_samples'])
  for (tid,module,symbol),n in counts.most_common():w.writerow([tid,threads[tid],module,symbol,n,100*n/totals[tid] if totals[tid] else 0])
 print('Resolved sampled self-IP counts. Read decoder.txt event-loss/error counters before interpreting.')
 for tid,name in threads.items():
  print(tid,name,'samples',totals[tid])
  for (t,m,s),n in [(k,v) for k,v in counts.most_common() if k[0]==tid][:12]:print(f'  {100*n/totals[tid]:6.2f}% {m}: {s}')
if __name__=='__main__':main(sys.argv[1])
