import subprocess,re,math,sys
lab='/tmp/claude-0/w/b/lab'; T0="fig2=1 a1=0.433 a2=0.433 tail=1.260362e-05 aQ=0.10396".split()
rows=[l.split() for l in open('spice/ac_fig2.out')]
worst=0
for r in rows:
    d=dict(x.split('=') for x in r[1:]); t=float(d['temp']); v=float(d['v5']); f3=float(d['f3']); g0=10**(float(d['g0db'])/20)
    out=subprocess.run([lab,'ac',*T0,f'T={t}',f'v5={v}',f'freq={f3}','amp=0.0002','fs=1536000','settle=%g'%max(0.05,8/f3),'cycles=%d'%max(20,int(f3*0.05))],capture_output=True,text=True).stdout
    g=float(re.search(r'gainSrc=([^ \n]+)',out).group(1))
    err=20*math.log10(g/g0)+3.0103; worst=max(worst,abs(err))
    print(f"T={t:4.0f} v5={v:+.2f} spice_f3={f3:9.2f} cpp_gain_at_f3={20*math.log10(g/g0):+.3f} dB (err {err:+.3f} dB)")
print('max |err| dB',worst)
