import subprocess,re,math,sys
sys.path.insert(0,'/tmp/claude-0/w'); from cmp_eii import v5,iq,Rth,lab,P
for l in open('spice/eii_osc.out'):
    d=dict(x.split('=') for x in l.split()[1:]); q=float(d['qcv'])
    out=subprocess.run([lab,'osc',*P,f'v5={v5(2.5)}',f'src5={Rth}',f'iq={iq(q)}','fs=384000','t=1.2'],capture_output=True,text=True).stdout
    sp=float(d['max'])-float(d['min']); sf=100/(float(d['t2'])-float(d['t1'])) if float(d['max'])>1e-6 else 0
    print(f"QCV={q:4.2f} iq={iq(q)*1e6:6.2f}uA SPICE ppk={sp:.5g} f={sf:.1f} | C++ {out.strip()}")
