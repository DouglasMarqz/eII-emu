import subprocess,re,math
lab='/tmp/claude-0/w/b/lab'; P="a1=0.433 a2=0.433 tail=1.260362e-05 aQ=0.10396".split()
G=1/22600+1/124000+1/1000; Rth=1/G
def v5(fcv): return (fcv/22600-15/124000)/G
def iq(qcv,T=25):
    vt=8.617333262e-5*(T+273.15); Is=10e-6/math.expm1(0.6/vt); lo,hi=0,max(0,qcv/23800)
    for _ in range(200):
        m=(lo+hi)/2; f=qcv-m*23800-vt*math.log1p(m/Is)
        if f>0: lo=m
        else: hi=m
    return lo
def gain(freq,v,i,extra=[]):
    out=subprocess.run([lab,'ac',*P,f'v5={v}',f'src5={Rth}',f'iq={i}',f'freq={freq}','amp=1e-4','fs=1536000','settle=%g'%max(0.1,10/freq),'cycles=%d'%max(20,int(freq*0.05)),*extra],capture_output=True,text=True).stdout
    return float(re.search(r'gainSrc=([^ \n]+)',out).group(1))
worst=0
for l in open('spice/eii_ac.out'):
    d=dict(x.split('=') for x in l.split()[1:] if '=' in x and x.split('=')[1])
    if l.startswith('EIIAC'):
        f=float(d['fcv']); g0=10**(float(d['g0db'])/20); f3=float(d['f3'])
        gc0=gain(20,v5(f),0); g3=gain(f3,v5(f),0)
        e=20*math.log10(g3/gc0)+3.0103; worst=max(worst,abs(e))
        print(f"FCV={f:3.1f} SPICE g0={20*math.log10(g0):.3f}dB f3={f3:9.2f}Hz | C++ g0={20*math.log10(gc0):.3f}dB gain@f3={20*math.log10(g3/gc0):+.3f}dB err={e:+.3f}")
print('AC worst',worst)
for l in open('spice/eii_ac.out'):
    if l.startswith('EIIQ'):
        d=dict(x.split('=') for x in l.split()[1:] if '=' in x and x.split('=')[1])
        q=float(d['qcv']); i=iq(q); g0=float(d['g0db'])
        print(f"QCV={q:4.2f} iq_model={i*1e6:7.2f}uA SPICE g0={g0:.3f} peak={float(d['peakdb']):.3f} | C++ g0={20*math.log10(gain(20,v5(2.5),i)):.3f}")
