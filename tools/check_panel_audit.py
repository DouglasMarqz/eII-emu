#!/usr/bin/env python3
"""Check native_panel_audit output from caller-supplied original bank firmware."""
import argparse
import re
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("capture", type=Path)
args = parser.parse_args()
rows = {}
for line in args.capture.read_text().splitlines():
    match = re.fullmatch(r"(\S+) LCD=\[(.*)\]\[(.*)\] LEDs=(.*?) raw=.* invalid=(\d+)", line)
    if match:
        name, top, bottom, leds, invalid = match.groups()
        assert name not in rows, f"duplicate {name}"
        assert invalid == "0", f"invalid production controller: {name}"
        rows[name] = (top, bottom, set(filter(None, leds.split(","))))
expected = "Select Stop Record Play Define Edit ExtClock Setup 0 9YES 8 7NO 6 5 4 3 2 1 Transpose Tune Dynamic PresetDef Sample Disk VoiceDef Special VCALFO Filter Realtime Enter".split()
assert set(rows) == set(expected), "all30 isolated button observations required"
for name in "0 9YES 8 7NO 6 5 4 3 2 1".split():
    assert rows[name][0].startswith("P" + name[0]), f"wrong digit from {name}"
for name in "Define Tune Dynamic PresetDef Sample Disk VoiceDef Special VCALFO Filter Realtime".split():
    assert rows[name][2] == {name}, f"wrong or extra module LED after {name}: {rows[name][2]}"
for name, prefix in {"Define": "Create Seq#", "Tune": "TUNING", "PresetDef": "PresetDef", "Disk": "Disk [1-8]", "Special": "CatalogFunctions"}.items():
    assert rows[name][0].startswith(prefix), f"wrong firmware function for {name}"
for name in "Select Record Play Edit ExtClock Setup".split():
    assert rows[name][1].startswith("No files exist"), f"expected no-sequence response for {name}"
print("PASS:30 isolated firmware observations;10 numeric identities,11 steady module LEDs and function prompts; no controller invalid state. Empty-bank sequencer operations remain no-files observations.")
