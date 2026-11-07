"""Shared reader for udp_record.py files."""
import struct
import sys


def packets(path):
    with open(path, "rb") as f:
        data = f.read()
    o = 0
    n = len(data)
    while o + 10 <= n:
        t, ln = struct.unpack_from("<dH", data, o)
        o += 10
        p = data[o : o + ln]
        o += ln
        if len(p) < ln:
            break
        yield t, p


def header(p):
    fmt, year, maj, mnr, ver, pid = struct.unpack_from("<HBBBBB", p, 0)
    uid = struct.unpack_from("<Q", p, 7)[0]
    stime, frame, overall = struct.unpack_from("<fII", p, 15)
    player, second = struct.unpack_from("<BB", p, 27)
    return dict(fmt=fmt, year=year, ver=ver, pid=pid, uid=uid, time=stime, frame=frame, player=player)


if __name__ == "__main__":
    import collections

    sizes = collections.Counter()
    uids = collections.Counter()
    ev = collections.Counter()
    first = None
    for t, p in packets(sys.argv[1]):
        h = header(p)
        sizes[(h["pid"], len(p))] += 1
        uids[h["uid"]] += 1
        first = first or h
        if h["pid"] == 3:
            ev[p[29:33].decode("ascii", "replace")] += 1
    print(first)
    print("id,size,count", sorted(sizes.items()))
    print("sessions", uids)
    print("events", ev.most_common())
