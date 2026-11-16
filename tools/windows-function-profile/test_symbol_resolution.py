from analyze_samples import resolve
import json,random
from pathlib import Path
s=json.loads(Path(__file__).with_name('symbols037.json').read_text());rows=s['symbols']
assert rows and all(a['end_rva']==b['start_rva'] for a,b in zip(rows,rows[1:]))
for base in [0x180000000,0x7ff800000000]:
 modules=[{'base_hex':hex(base),'size':0x300000,'name':'EmulatorII.vst3'}]
 for row in rows:
  for rva in [row['start_rva'],row['end_rva']-1]:
   assert resolve(base+rva,modules,s)[1]==row['symbol']
 assert resolve(base+s['text_end'],modules,s)[1]=='[no text symbol]'
 assert resolve(base-1,modules,s)[0]=='[outside recorded modules]'
print('PASS exact037 symbol range boundaries and relocated ASLR bases; no native ETW runtime claim')
