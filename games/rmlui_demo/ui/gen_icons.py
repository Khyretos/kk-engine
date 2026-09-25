#!/usr/bin/env python3
"""Generates the showcase's tiny vector-ish UI icons as PNGs (no deps).

Anti-aliased line drawings from distance-to-segment, so they stay crisp
at the sizes RmlUi scales them to. Run from this folder:
    python3 gen_icons.py
"""
import math, struct, zlib, os

def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))

def polyline_png(path, size, points, width, rgb=(255, 255, 255)):
    rows = []
    for y in range(size):
        row = bytearray([0])
        for x in range(size):
            d = min(seg_dist(x + 0.5, y + 0.5, *points[i], *points[i + 1]) for i in range(len(points) - 1))
            a = max(0.0, min(1.0, width / 2 - d + 0.5))
            row += bytes((*rgb, int(a * 255)))
        rows.append(bytes(row))
    raw = b"".join(rows)
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)

os.makedirs("assets", exist_ok=True)
s = 64
polyline_png("assets/check.png", s, [(0.22 * s, 0.52 * s), (0.42 * s, 0.72 * s), (0.78 * s, 0.30 * s)], 0.12 * s)
polyline_png("assets/arrow_down.png", s, [(0.34 * s, 0.42 * s), (0.50 * s, 0.58 * s), (0.66 * s, 0.42 * s)], 0.07 * s, (200, 210, 235))
print("wrote assets/check.png, assets/arrow_down.png")
