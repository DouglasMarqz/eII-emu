# Full-suite runner (2026-10-29). Usage: py -3 tools/run_suite_win64.py [outdir] [comma-list]
#: mirrors CMakeLists.txt add_test targets (plus the
# VST3 script's tests and timing-lab tests) with LLVM-MinGW clang++, default 1MB stack.
import subprocess, sys, os, re, time
from concurrent.futures import ThreadPoolExecutor
root=os.path.dirname(os.path.dirname(os.path.abspath(__file__))); os.chdir(root)
CXX=os.path.join(os.environ.get("LLVM_MINGW_ROOT",r"F:\tools\llvm-mingw-20261006-ucrt-x86_64"),"bin","x86_64-w64-mingw32-clang++.exe")
out=sys.argv[1] if len(sys.argv)>1 else r"build\suite"
only=set(sys.argv[2].split(",")) if len(sys.argv)>2 and sys.argv[2] else None
os.makedirs(out,exist_ok=True)
BASE=["-std=c++20","-O2","-DNDEBUG","-pthread","-static","-Wall","-Wextra","-Wno-misleading-indentation","-Wno-unused-parameter","-Wno-unused-variable","-Wno-unused-but-set-variable","-Wno-unused-function","-Wno-sign-compare","-Wno-unused-value","-Wno-missing-field-initializers","-Wno-pragma-pack","-DUNICODE","-D_UNICODE"]
INC=["-Isrc","-Ivendor/chips"]
cm=open("CMakeLists.txt").read()
# expand foreach(x a b c) ... ${x} ... endforeach()
def expand(text):
    pat=re.compile(r"foreach\((\w+)\s+([^)]*)\)(.*?)endforeach\(\)",re.S)
    while True:
        m=pat.search(text)
        if not m: return text
        var,items,body=m.group(1),m.group(2).split(),m.group(3)
        text=text[:m.start()]+"".join(body.replace("${"+var+"}",i) for i in items)+text[m.end():]
cm=expand(cm)
exe={};links={};defs={};incs={}
for m in re.finditer(r"add_executable\((\w+)\s+(?:WIN32\s+)?([^)]*)\)",cm): exe[m.group(1)]=m.group(2).split()
for m in re.finditer(r"target_link_libraries\((\w+)\s+(?:PRIVATE|PUBLIC)\s+([^)]*)\)",cm): links.setdefault(m.group(1),[]).extend(m.group(2).split())
for m in re.finditer(r"target_compile_definitions\((\w+)\s+(?:PRIVATE|PUBLIC)\s+([^)]*)\)",cm): defs.setdefault(m.group(1),[]).extend(m.group(2).split())
for m in re.finditer(r"target_include_directories\((\w+)\s+(?:PRIVATE|PUBLIC)\s+([^)]*)\)",cm): incs.setdefault(m.group(1),[]).extend(m.group(2).split())
tests=[]
for m in re.finditer(r"add_test\(NAME (\w+) COMMAND (\w+)([^)]*)\)",cm): tests.append((m.group(1),m.group(2),m.group(3).split()))
lab={"playback_timing_observer_tests","native_timing_tests"}
for t in lab:
    if not any(x[0]==t for x in tests): tests.append((t,t,[]))
extra={"vst3_engine_tests":(["tests/vst3_engine_tests.cpp"],["-Ivendor/vst3sdk","-DEII_VST3_BACKGROUND_AUDIO=1"]),
       "analog_audit_tests":(["tests/analog_audit_tests.cpp"],[]),
       "ssm2_model_tests":(["tests/ssm2_model_tests.cpp"],[])}
for t,(src,fl) in extra.items():
    if os.path.exists(src[0]) and not any(x[0]==t for x in tests):
        exe[t]=src;links[t]=["eii_core"];defs[t]=[d[2:] for d in fl if d.startswith("-D")];incs[t]=[d[2:] for d in fl if d.startswith("-I")];tests.append((t,t,[]))
if "sample_analog_tests" not in exe: exe["sample_analog_tests"]=["tests/sample_analog_tests.cpp"];links["sample_analog_tests"]=["eii_core"]
core=["src/chips.cpp","src/machine.cpp","vendor/ssm2045-chip/src/chip.cpp","vendor/eiii-cem/eiii_cem3387_v2.cpp","vendor/eiii-cem/eiii_cem_cv_conditioning.cpp"]
extra_core=[s for s in sorted(os.listdir("src")) if s.startswith("ssm2_") and s.endswith(".cpp")]
core+=["src/"+s for s in extra_core]
def build_core(tag,flags):
    objs=[];ps=[]
    for s in core:
        o=f"{out}/{tag}_{os.path.basename(s)}.o";objs.append(o)
        ps.append(subprocess.Popen([CXX,*BASE,*INC,*flags,"-c",s,"-o",o],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True))
    for p in ps:
        txt=p.communicate()[0]
        if p.returncode: print(txt[-1500:]); raise SystemExit("core build failed "+tag)
    return objs
t0=time.time()
need=[t for t in tests if not only or t[0] in only]
coreN=build_core("core",["-DEII_DIAGNOSTICS=0"])
coreL=build_core("lab",["-DEII_DIAGNOSTICS=0","-DEII_PLAYBACK_TIMING_LAB=1"]) if any(t[0] in lab for t in need) else []
def run(item):
    name,target,args=item
    srcs=exe.get(target)
    if not srcs: return name,"NO_TARGET",""
    d=defs.get(target,[]);fl=["-D"+x for x in d]
    if not any(x.startswith("EII_DIAGNOSTICS") for x in d): fl.append("-DEII_DIAGNOSTICS=0")
    if target in lab: fl.append("-DEII_PLAYBACK_TIMING_LAB=1")
    fl+=["-I"+x for x in incs.get(target,[])]+["-Ivendor/ssm2045-chip/include"]
    objs=(coreL if target in lab else coreN) if ("eii_core" in links.get(target,[]) or target in lab) else []
    libs=["-lcomctl32","-lcomdlg32","-luser32","-lgdi32","-lgdiplus","-lole32","-luuid","-ladvapi32","-lwinmm"]
    exe_path=f"{out}/{target}.exe"
    b=subprocess.run([CXX,*BASE,*INC,*fl,*srcs,*objs,*libs,"-o",exe_path],capture_output=True,text=True)
    if b.returncode: return name,"BUILD_FAIL",b.stderr[-900:]
    try:
        r=subprocess.run([exe_path,*args],capture_output=True,text=True,timeout=3600)
    except subprocess.TimeoutExpired: return name,"TIMEOUT",""
    return name,("PASS" if r.returncode==0 else "FAIL(%d)"%r.returncode),(r.stdout+r.stderr)[-500:]
res=[]
with ThreadPoolExecutor(8) as ex:
    for name,st,txt in ex.map(run,need):
        res.append((name,st)); print(f"=== {name}: {st}"); 
        if st!="PASS" or "-v" in sys.argv: print("    "+txt.strip().replace("\n","\n    "))
        sys.stdout.flush()
bad=[n for n,s in res if s!="PASS"]
print(f"SUITE total={len(res)} pass={len(res)-len(bad)} fail={len(bad)} {' '.join(bad)} wall={time.time()-t0:.0f}s")
