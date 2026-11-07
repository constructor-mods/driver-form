"""Whole-field lap time by leader lap from the [form-lab] lap lines: shows each phase against the stock laps around it."""
import collections
import re
import statistics as st
import sys

cross = collections.defaultdict(list)
phases = []
for l in open(sys.argv[1], errors="replace"):
    if "form-lab]" not in l:
        continue
    m = re.search(r"lap car (\d+) n=(\d+) t=(\d+)ms", l)
    if m:
        cross[int(m[1])].append((int(m[2]), int(m[3]) / 1000))
    m = re.search(r"phase (\S+) from leader crossing (\d+)", l)
    if m:
        phases.append((int(m[2]), m[1]))
print("phases", phases)
laps = collections.defaultdict(dict)
for c, x in cross.items():
    x = sorted(x)
    for (n1, t1), (n2, t2) in zip(x, x[1:]):
        if n2 == n1 + 1 and 40 < t2 - t1 < 200:
            laps[n1][c] = t2 - t1
print("lap: median, fastest, slowest, cars")
for n in sorted(laps):
    v = list(laps[n].values())
    print(f"  {n:2}  {st.median(v):7.2f}  {min(v):7.2f}  {max(v):7.2f}  {len(v)}")
