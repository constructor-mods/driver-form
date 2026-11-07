"""Per-group lap time report from the [form-lab] lines of a loader.log."""
import collections
import re
import statistics as st
import sys

cars = {}
cross = collections.defaultdict(list)
stock = {}
after = {}
for l in open(sys.argv[1], errors="replace"):
    if "form-lab]" not in l:
        continue
    m = re.search(r"(stock|after) (\S+) car (\d+) group (\S+): diff ([\d.]+) base ([\d.]+) \| ag ([\d.]+) ct ([\d.]+) mi ([\d.]+) re ([\d.]+) in ([\d.]+) out ([\d.]+)", l)
    if m:
        c = int(m[3])
        cars[c] = (m[2], m[4])
        (stock if m[1] == "stock" else after)[c] = dict(diff=float(m[5]), ag=float(m[7]), ct=float(m[8]), inn=float(m[11]), out=float(m[12]))
    m = re.search(r"(stock|after) (\S+) params: awr \S+ spc (\S+) bkdist (\S+) bkoff \S+ curv (\S+) foll (\S+) slide \S+ thrc (\S+) fall (\S+) thron (\S+)", l)
    if m:
        for c, (n, g) in cars.items():
            if n == m[2]:
                (stock if m[1] == "stock" else after).setdefault(c, {}).update(spc=m[3], bk=m[4], curv=m[5], foll=m[6], thrc=m[7], fall=m[8], thron=m[9])
    m = re.search(r"lap car (\d+) n=(\d+) t=(\d+)ms", l)
    if m:
        cross[int(m[1])].append((int(m[2]), int(m[3]) / 1000))
    m = re.search(r"groups on", l)
    if m:
        print(l.strip()[-40:])

print("read-back (stock -> after):")
for c, (n, g) in sorted(cars.items(), key=lambda x: x[1][1]):
    s, a = stock.get(c, {}), after.get(c, {})
    print(f"  {n:4} {g:9} diff {s.get('diff')}->{a.get('diff')} ct {s.get('ct')}->{a.get('ct')} in {s.get('inn')}->{a.get('inn')} out {s.get('out')}->{a.get('out')} ag {s.get('ag')}->{a.get('ag')} | bk {s.get('bk')}->{a.get('bk')} curv {s.get('curv')}->{a.get('curv')} thrc {s.get('thrc')}->{a.get('thrc')} thron {s.get('thron')}->{a.get('thron')} foll {s.get('foll')}->{a.get('foll')}")

laps = {}
for c, x in cross.items():
    x = sorted(x)
    d = {}
    for (n1, t1), (n2, t2) in zip(x, x[1:]):
        if n2 == n1 + 1 and 40 < t2 - t1 < 200:
            d[n1] = t2 - t1
    laps[c] = d
print("laps:")
groups = collections.defaultdict(list)
for c, (n, g) in sorted(cars.items(), key=lambda x: x[1][1]):
    d = laps.get(c, {})
    base = [v for k, v in d.items() if k in (1, 2)]
    tre = [v for k, v in d.items() if k >= 4]
    print(f"  {n:4} {g:9}", {k: round(v, 2) for k, v in sorted(d.items())})
    if base and tre:
        groups[g].append(st.median(tre) - st.median(base))
print("change, treated laps minus baseline laps, by group:")
none = st.median(groups["none"]) if groups.get("none") else 0
for g, v in groups.items():
    print(f"  {g:9} n={len(v)} median {st.median(v):+.2f}  vs none {st.median(v) - none:+.2f}  each {[round(x, 2) for x in v]}")
