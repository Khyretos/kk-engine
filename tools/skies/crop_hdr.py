#!/usr/bin/env python3
"""Crops an equirectangular Radiance .hdr sky below the horizon.

    tools/skies/crop_hdr.py in.hdr out.hdr [--below DEGREES]

Keeps the rows from straight up down to DEGREES below the horizon (10 by
default) and drops the rest: under a game's ground nobody sees them, and a
"pure sky" is half the size without them. kke::SkyImage reads the result
(an image less than half as tall as it is wide covers the top of the
sphere; its last row carries on to the bottom). Scanlines are copied
byte for byte, so the kept pixels are exactly the original's.
"""
import argparse
import math
import sys


def read_scanline(data, pos, width):
    """Returns the end offset of one scanline starting at pos."""
    if width < 8 or width > 0x7FFF or data[pos] != 2 or data[pos + 1] != 2 or data[pos + 2] & 0x80:
        return pos + width * 4  # flat (not run-length encoded)
    if (data[pos + 2] << 8 | data[pos + 3]) != width:
        sys.exit("crop_hdr: scanline width mismatch")
    pos += 4
    for _ in range(4):  # one run-length encoded block per channel
        n = 0
        while n < width:
            count = data[pos]
            pos += 1
            if count > 128:
                count -= 128
                pos += 1
            else:
                pos += count
            n += count
    return pos


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--below", type=float, default=10.0)
    a = ap.parse_args()
    data = open(a.src, "rb").read()
    end = data.index(b"\n\n") + 2
    header = data[:end]
    line_end = data.index(b"\n", end)
    res = data[end:line_end].split()
    if res[0] != b"-Y" or res[2] != b"+X":
        sys.exit("crop_hdr: only standard -Y H +X images")
    height, width = int(res[1]), int(res[3])
    if width != 2 * height:
        sys.exit("crop_hdr: not a full equirectangular image (width must be twice the height)")
    keep = min(height, math.ceil(height * (90.0 + a.below) / 180.0))
    pos = line_end + 1
    for _ in range(keep):
        pos = read_scanline(data, pos, width)
    with open(a.dst, "wb") as f:
        f.write(header)
        f.write(b"-Y %d +X %d\n" % (keep, width))
        f.write(data[line_end + 1:pos])
    print(f"{a.dst}: {width}x{keep} (was {width}x{height})")


if __name__ == "__main__":
    main()
