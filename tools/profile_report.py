# Resolve a tools/sampling_profiler.h sample file.
# usage: py -3 tools/profile_report.py PROBE.exe SAMPLES.txt [top=40]
# Prints, per thread, self time by innermost (inlined) function and inclusive
# time by any function on the chain (inlined frames included).
import subprocess, sys, os, collections, re
exe, path = sys.argv[1], sys.argv[2]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 40
sym = os.path.join(os.environ.get("LLVM_MINGW_ROOT", r"F:\tools\llvm-mingw-20261006-ucrt-x86_64"), "bin", "llvm-symbolizer.exe")
rows = []
addrs = set()
for line in open(path):
    p = line.split()
    chain = [int(x, 16) for x in p[2:]]
    rows.append((p[0], int(p[1]), chain))
    addrs.update(chain)
addrs = sorted(addrs)
base = 0x140000000
inp = "\n".join(hex(a + base) for a in addrs) + "\n"
out = subprocess.run([sym, "--obj=" + exe, "--inlines", "--functions=short", "--demangle", "--output-style=GNU"], input=inp, capture_output=True, text=True).stdout
# GNU style: function line, then file:line; inlined frames follow as "(inlined by)"-less pairs. Use blank-less parsing via LLVM style instead.
out = subprocess.run([sym, "--obj=" + exe, "--inlines", "--functions=short", "--demangle"], input=inp, capture_output=True, text=True).stdout
blocks = [b for b in out.split("\n\n") if b.strip()]
frames = {}
for a, b in zip(addrs, blocks):
    ls = b.strip().split("\n")
    fr = []
    for i in range(0, len(ls) - 1, 2):
        loc = ls[i + 1]
        m = re.match(r"(.*):(\d+):\d+$", loc)
        fr.append((ls[i], os.path.basename(m.group(1)) if m else "?", int(m.group(2)) if m else 0))
    frames[a] = fr or [("?", "?", 0)]
threads = collections.defaultdict(list)
for t, c, chain in rows:
    threads[t].append((c, chain))
for t, items in sorted(threads.items(), key=lambda kv: -sum(c for c, _ in kv[1])):
    total = sum(c for c, _ in items)
    if total < 50:
        continue
    selfc = collections.Counter(); inc = collections.Counter(); lines = collections.Counter()
    for c, chain in items:
        f0 = frames[chain[0]][0]
        selfc[f0[0]] += c
        lines[(f0[0], f0[1], f0[2])] += c
        seen = set()
        for a in chain:
            for f in frames[a]:
                seen.add(f[0])
        for f in seen:
            inc[f] += c
    print(f"== thread {t}: {total} samples")
    print("-- self (innermost inlined function)")
    for f, c in selfc.most_common(top):
        print(f"{100*c/total:6.2f}%  {f}")
    print("-- inclusive")
    for f, c in inc.most_common(top):
        print(f"{100*c/total:6.2f}%  {f}")
    print("-- hottest lines")
    for (f, file, ln), c in lines.most_common(top):
        print(f"{100*c/total:6.2f}%  {file}:{ln}  {f}")
    # call tree (outermost first), inlined frames included; branches >= 0.7 %
    tree = {}
    for c, chain in items:
        path = []
        for a in reversed(chain):
            path.extend(f[0] for f in reversed(frames[a]))
        node = tree
        for name in path:
            node = node.setdefault(name, [0, {}])
            node[0] += c
            node = node[1]
    def show(node, depth):
        for name, (c, kids) in sorted(node.items(), key=lambda kv: -kv[1][0]):
            if 100 * c / total < 0.7 or depth > 14:
                continue
            print(f"{100*c/total:6.2f}%  {'  '*depth}{name}")
            show(kids, depth + 1)
    print("-- tree")
    show(tree, 0)
