#!/usr/bin/env python3
"""Writes tennis_ball.png: the felt and the seam of a tennis ball, mapped
the way games/tennis/Ball.cpp gives the FEMFX ball its UVs (u = longitude
from atan2(x, z), v = latitude from +y down). Standard library only.

    python3 make_ball_texture.py   # next to this script
"""
import math
import os
import random
import struct
import zlib

W, H = 256, 128
FELT = (206, 226, 58)      # optic yellow
SEAM = (246, 246, 236)
A, B = 0.7, 0.3            # the seam: a + b = 1 keeps it on the unit sphere


def seam_points(n=720):
    pts = []
    for i in range(n):
        t = 2.0 * math.pi * i / n
        pts.append((A * math.cos(t) + B * math.cos(3 * t),
                    A * math.sin(t) - B * math.sin(3 * t),
                    2.0 * math.sqrt(A * B) * math.sin(2 * t)))
    return pts


def png(path, rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    def chunk(kind, data):
        c = struct.pack(">I", len(data)) + kind + data
        return c + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def main():
    random.seed(7)
    pts = seam_points()
    rows = []
    for j in range(H):
        lat = math.pi * (j + 0.5) / H
        row = []
        for i in range(W):
            lon = 2.0 * math.pi * ((i + 0.5) / W - 0.5)
            d = (math.sin(lat) * math.sin(lon), math.cos(lat), math.sin(lat) * math.cos(lon))
            best = max(d[0] * p[0] + d[1] * p[1] + d[2] * p[2] for p in pts)
            angle = math.acos(max(-1.0, min(1.0, best)))
            w = max(0.0, min(1.0, (0.06 - angle) / 0.02))    # soft seam edge
            fuzz = 0.9 + 0.1 * random.random()                # felt
            c = [int(FELT[k] * fuzz * (1 - w) + SEAM[k] * w) for k in range(3)]
            row.extend(c)
        rows.append(row)
    png(os.path.join(os.path.dirname(os.path.abspath(__file__)), "tennis_ball.png"), rows)


if __name__ == "__main__":
    main()
