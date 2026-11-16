import subprocess,re
lab='/tmp/claude-0/w/b/lab'; T0="fig2=1 a1=0.433 a2=0.433 tail=1.260362e-05 aQ=0.10396".split()
log=open('spice/thd_fig2.log').read()
cases=re.findall(r'THDCASE fr=([\d.]+) oc=([\d.]+)\s+Fourier analysis for v\(8\):\s+No. Harmonics: 10, THD: ([\d.e+-]+) %.*?\n\s*1\s+[\d.]+\s+([\d.e+-]+)',log,re.S)
print("fr      oc(peak)  SPICE_THD%  CPP_THD%   SPICE_h1   CPP_h1")
for fr,oc,thd,h1 in cases:
    out=subprocess.run([lab,'thd',*T0,f'freq={fr}',f'amp={oc}','fs=1536000','settle=0.25','cycles=30'],capture_output=True,text=True).stdout
    c=float(re.search(r' thd=([^ ]+)',out).group(1))*100; ch=float(re.search(r' h1=([^ ]+)',out).group(1))
    print(f"{float(fr):8.2f} {float(oc):6.3f}  {float(thd):10.4f}  {c:9.4f}  {float(h1):9.5f} {ch:9.5f}")
