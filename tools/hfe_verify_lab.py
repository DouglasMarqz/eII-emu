# Independent read-only validation lab; requires the exact80-track EII HFE fixture.
# Usage: python tools/hfe_verify_lab.py input.hfe output.emuiifd report.json
from pathlib import Path
import hashlib,json,sys
b=Path(sys.argv[1]).read_bytes()
def crc(d):
 r=0
 for x in d:
  r ^= x
  for _ in range(8):r=(r>>1)^(0xa001 if r&1 else 0)
 return r
pat=bytes(v for x in (0xfa,0x96) for k in range(8) for v in (1,(x>>k)&1))
result=bytearray();records=[]
for cyl in range(80):
 off=int.from_bytes(b[512+4*cyl:514+4*cyl],'little')*512;n=int.from_bytes(b[514+4*cyl:516+4*cyl],'little')//2
 assert off+((n+255)//256)*512<=len(b)
 for side in range(2):
  raw=bytes(b[off+(i//256)*512+side*256+i%256] for i in range(n));bits=bytes((x>>k)&1 for x in raw for k in range(8));sums=[sum(bits[::2]),sum(bits[1::2])];assert (sums[0]==0)!=(sums[1]==0)
  phase=0 if sums[1]==0 else 1;cells=bits[phase::2];data=cells+cells;found=[];pos=0
  def read(at,count):
   assert all(data[at+16*i+2*k]==1 for i in range(count) for k in range(8))
   return bytes(sum(data[at+16*i+2*k+1]<<k for k in range(8)) for i in range(count))
  while True:
   h=data.find(pat,pos,len(cells)+31)
   if h<0 or h>=len(cells):break
   pos=h+1
   try:header=read(h+32,3)
   except AssertionError:continue
   if header[0]!=cyl*2+side or crc(header)!=0:continue
   mark=data.find(pat,h+80,min(h+1104,len(data)))
   if mark<0 or mark+32+3586*16>h+len(cells):continue
   try:payload=read(mark+32,3586)
   except AssertionError:continue
   if crc(payload)!=0:continue
   found.append((h,mark,payload[:3584]))
  assert len(found)==1,(cyl,side,len(found))
  h,m,p=found[0];result.extend(p);records.append({'id':cyl*2+side,'phase':phase,'header_cell':h,'data_cell':m,'sha256':hashlib.sha256(p).hexdigest()})
Path(sys.argv[2]).write_bytes(result)
Path(sys.argv[3]).write_text(json.dumps({'input_sha256':hashlib.sha256(b).hexdigest(),'output_sha256':hashlib.sha256(result).hexdigest(),'sectors':records},indent=2)+'\n')
print('160/160 header and data CRCs valid; unique physical IDs; output',len(result),hashlib.sha256(result).hexdigest())
