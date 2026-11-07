"""Form against lap time from the [driver-form] lines of a loader.log."""
import collections
import re
import statistics as st
import sys

form = {}
cross = collections.defaultdict(list)
patches = []
other = []
for l in open(sys.argv[1], errors="replace"):
    if "driver-form]" not in l:
        continue
    b = l.split("[driver-form] ", 1)[1].strip()
    m = re.match(r"car (\d+) (\S+): form ([+-][\d.]+), ([+-][\d.]+) s, quality ([\d.]+)", b)
    if m:
        form[int(m[1])] = (m[2], float(m[3]), float(m[4]), float(m[5]))
        continue
    m = re.match(r"lap car (\d+) .* t=(\d+)ms", b)
    if m:
        cross[int(m[1])].append(int(m[2]) / 1000)
        continue
    if " patch " in b:
        patches.append(b)
    else:
        other.append(b)
print("\n".join(other[:12]))
print("patches:", len([p for p in patches if "over" not in p]))
print("\n".join(p for p in patches if "over" not in p))
pts = []
for c, (n, f, s, q) in sorted(form.items(), key=lambda x: -x[1][1]):
    t = cross.get(c, [])
    d = [round(b - a, 2) for a, b in zip(t, t[1:]) if 40 < b - a < 200]
    print(f"{n:4} form {f:+.2f} shift {s:+.2f} quality {q:.2f} laps {d}")
    if d:
        pts.append((f, st.mean(d)))
if len(pts) > 2:
    mx = st.mean(p[0] for p in pts)
    my = st.mean(p[1] for p in pts)
    b = sum((a - mx) * (c - my) for a, c in pts) / sum((a - mx) ** 2 for a, c in pts)
    print(f"cars {len(pts)}, slope {b:.2f} s a lap per unit of form (negative = better form is faster)")
