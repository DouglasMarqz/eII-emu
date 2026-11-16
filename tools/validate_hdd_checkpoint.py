"""Validate firmware log/image evidence; private images are supplied by caller."""
import re
import sys
from pathlib import Path

format_log, save_log, cold_log, before, saved, reopened = map(Path, sys.argv[1:])
f, s, c = (p.read_text(errors="replace") for p in (format_log, save_log, cold_log))
assert "Reload Backup" in f and "Get HD Bank #" in f
assert "Writing the Bank" in s and "HD bytes R/W 3584/489472 invalid 0" in s
assert "LCD P01 NULL PRESET" in c and "invalid 0" in c
assert "HD CDB 8 0 0 0 38 0" in c, "ROM must fetch OS from HDD"
a, b, d = (p.read_bytes() for p in (before, saved, reopened))
assert len(a) == len(b) == len(d) == 613*4*18*512
assert b == d, "read-only boot must preserve saved image"
allowed = set()
for payload in re.findall(r"^HD CDB (.+)$", s, re.M):
    cmd = [int(v, 16) for v in payload.split()]
    if cmd[0] == 0x2e:
        lba = int.from_bytes(bytes(cmd[2:6]), "big")
        count = int.from_bytes(bytes(cmd[7:9]), "big")
        allowed.update(range(lba, lba+count))
changed = {i//512 for i, (x, y) in enumerate(zip(a, b)) if x != y}
assert changed and changed <= allowed, "writes must remain within firmware CDB ranges"
assert any(i >= 0x93 for i in changed), "user bank payload must actually change"
print(f"PASS firmware FORMAT / DISK7 bank02 / HDD-only reboot; {len(changed)} changed sectors within {len(allowed)} addressed sectors; read-only boot image unchanged")
