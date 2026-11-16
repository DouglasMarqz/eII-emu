import numpy as np,subprocess,re,math,sys,glob,os
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
sys.argv=['x'];exec(open('/tmp/claude-0/w/cmp_eii.py').read().split("worst=0")[0])
V='/tmp/claude-0/w/val'
def cgain(freq,v,i,fs=192000):
    out=subprocess.run([lab,'ac',*P,f'v5={v}',f'src5={Rth}',f'iq={i}',f'freq={freq}','amp=1e-4',f'fs={fs}','settle=%g'%max(0.08,12/freq),'cycles=%d'%max(20,int(freq*0.04))],capture_output=True,text=True).stdout
    return float(re.search(r'gainSrc=([^ \n]+)',out).group(1))
fr=[20,50,100,200,500,1000,2000,5000,10000,15000,20000]
rows=[]
fig,ax=plt.subplots(1,2,figsize=(13,5))
for f in [0.5,1,1.5,2,2.5,3,3.5,4,4.5]:
    d=np.loadtxt(f'spice/curve_fcv_{f}.txt'); ax[0].semilogx(d[:,0],d[:,1],lw=1,label=f'FCV {f} V (SPICE)')
    for x in fr:
        g=20*math.log10(cgain(x,v5(f),0)); s=np.interp(np.log(x),np.log(d[:,0]),d[:,1]); rows.append(('fcv',f,x,s,g)); ax[0].plot(x,g,'k.',ms=4)
ax[0].set_title('EII 4-pole response vs FCV (QCV 0): SPICE lines, C++ 192k dots');ax[0].set_xlabel('Hz');ax[0].set_ylabel('dB (pin 8 / deglitch Y)');ax[0].set_ylim(-100,-10);ax[0].grid(True,which='both',alpha=.3);ax[0].legend(fontsize=7)
for q in [0,1,1.5,2,2.25,2.5,2.6]:
    d=np.loadtxt(f'spice/curve_qcv_{q}.txt'); ax[1].semilogx(d[:,0],d[:,1],lw=1,label=f'QCV {q} V ({iq(q)*1e6:.0f} uA)')
    for x in [100,300,600,800,1000,1200,1500,2000,3000]:
        g=20*math.log10(cgain(x,v5(2.5),iq(q))); s=np.interp(np.log(x),np.log(d[:,0]),d[:,1]); rows.append(('qcv',q,x,s,g)); ax[1].plot(x,g,'k.',ms=4)
ax[1].set_title('EII Q family at FCV 2.5 V: SPICE lines, C++ 192k dots');ax[1].set_xlabel('Hz');ax[1].set_ylim(-60,10);ax[1].grid(True,which='both',alpha=.3);ax[1].legend(fontsize=7)
plt.tight_layout();plt.savefig(f'{V}/fig_eii_fc_q_families.png',dpi=110)
with open(f'{V}/eii_families_spice_vs_cpp192k.csv','w') as o:
    o.write('family,cv,freq_hz,spice_db,cpp192k_db,diff_db\n')
    for a,b,c,s,g in rows: o.write(f'{a},{b},{c},{s:.4f},{g:.4f},{g-s:+.4f}\n')
e=[abs(g-s) for a,b,c,s,g in rows if s>-80]
print('families: points',len(rows),'max |diff| (where > -80 dB)',max(e),'rms',math.sqrt(sum(x*x for x in e)/len(e)))
