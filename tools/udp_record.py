#!/usr/bin/env python3
"""Records F1 25's own UDP telemetry to a file, untouched, for analysis afterwards.

Game settings: Telemetry Settings, UDP Telemetry On, Broadcast Off, IP 127.0.0.1, Port 20777,
Send Rate 20 Hz, UDP Format 2025.

Run this, race, then press Ctrl+C. Nothing is installed in the game.

    python udp_record.py [--seconds N] [--port 20777] [--out FOLDER]

File layout: repeated records of <double receive time><uint16 length><packet bytes>.
"""
import argparse
import collections
import os
import socket
import struct
import time

# Packet ids kept: 0 motion, 1 session, 2 lap data, 3 event, 4 participants, 6 car telemetry, 7 car status,
# 8 final classification, 10 car damage. Skipped to save space: 5 setups, 9 lobby, 11 history, 12 tyre sets, 13+.
KEEP = {0, 1, 2, 3, 4, 6, 7, 8, 10}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=0, help="stop after this long; 0 runs until Ctrl+C")
    ap.add_argument("--port", type=int, default=20777)
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "telemetry"))
    a = ap.parse_args()

    os.makedirs(a.out, exist_ok=True)
    path = os.path.join(a.out, time.strftime("race-%Y%m%d-%H%M%S.udp"))
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 * 1024 * 1024)
    s.bind(("127.0.0.1", a.port))
    s.settimeout(1.0)
    print(f"Listening on 127.0.0.1:{a.port}, writing {os.path.abspath(path)}")
    print("Waiting for the game. Press Ctrl+C when the race is finished.")

    counts = collections.Counter()
    start = time.time()
    last = start
    size = 0
    fmt = None
    try:
        with open(path, "wb", buffering=1024 * 1024) as f:
            while True:
                now = time.time()
                if a.seconds and now - start >= a.seconds:
                    break
                if now - last >= 10:
                    last = now
                    print(f"{now - start:6.0f} s  {sum(counts.values())} packets  {size / 1e6:.1f} MB  format {fmt}", flush=True)
                try:
                    data = s.recv(4096)
                except socket.timeout:
                    continue
                if len(data) < 29:
                    continue
                pid = data[6]
                counts[pid] += 1
                if fmt is None:
                    fmt = struct.unpack_from("<H", data, 0)[0]
                    print(f"First packet: format {fmt}, packet id {pid}", flush=True)
                if pid in KEEP:
                    f.write(struct.pack("<dH", time.time(), len(data)))
                    f.write(data)
                    size += len(data) + 10
    except KeyboardInterrupt:
        pass
    print(f"Stopped. {sum(counts.values())} packets, {size / 1e6:.1f} MB, by id: {sorted(counts.items())}")
    print(f"File: {os.path.abspath(path)}")
    if not counts:
        print("No packets arrived. Check UDP Telemetry is On, IP 127.0.0.1, port 20777, Broadcast Off.")


if __name__ == "__main__":
    main()
