"""Report from a udp_record.py capture (format 2026) plus the [form-lab] lines of a session log."""
import collections
import re
import statistics as st
import struct
import sys

import udp_read as R

CARS = 24
LAP_SIZE = 57


def lap_entry(p, i):
    o = 29 + i * LAP_SIZE
    last, cur = struct.unpack_from("<II", p, o)
    dist, total = struct.unpack_from("<ff", p, o + 20)
    return dict(
        last=last, cur=cur, dist=dist, total=total, pos=p[o + 32], lap=p[o + 33], pit=p[o + 34], stops=p[o + 35],
        grid=p[o + 43], status=p[o + 44], result=p[o + 45],
    )


def names(p):
    out = {}
    for i in range(CARS):
        o = 30 + i * 60
        raw = p[o : o + 60]
        m = re.search(rb"[A-Za-z][A-Za-z .'-]{2,}", raw)
        out[i] = m.group(0).decode("utf-8", "replace") if m else ""
    return out


def main(path, logpath=None):
    lastlap = collections.defaultdict(dict)
    name = {}
    events = []
    final = {}
    prev = {}
    for t, p in R.packets(path):
        h = R.header(p)
        pid = h["pid"]
        if pid == 4:
            name = names(p)
        elif pid == 2:
            for i in range(CARS):
                e = lap_entry(p, i)
                if e["lap"] == 0:
                    continue
                final[i] = e
                if e["last"] and prev.get(i) != (e["lap"], e["last"]):
                    prev[i] = (e["lap"], e["last"])
                    lastlap[i][e["lap"] - 1] = e["last"] / 1000
        elif pid == 3:
            code = p[29:33].decode("ascii", "replace")
            if code in ("RTMT", "COLL", "PENA", "LGOT", "SSTA", "OVTK", "FLBK"):
                events.append((round(h["time"], 1), code, p[33:40].hex()))
    print("names", {k: v for k, v in name.items() if v})
    print("status at end:")
    for i, e in sorted(final.items(), key=lambda x: x[1]["pos"]):
        print(f"  P{e['pos']:<2} car {i:<2} {name.get(i,'')[:18]:18} lap {e['lap']} dist {e['dist']:.0f} grid {e['grid']} result {e['result']} driverStatus {e['status']} stops {e['stops']}")
    print("laps:")
    for i in sorted(lastlap):
        print(f"  car {i:<2} {name.get(i,'')[:14]:14}", {k: round(v, 3) for k, v in sorted(lastlap[i].items())})
    print("events", [e for e in events if e[1] != "OVTK"][:30])
    return lastlap, name, final


if __name__ == "__main__":
    main(*sys.argv[1:])
