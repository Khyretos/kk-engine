#!/usr/bin/env python3
"""Bakes the skin detail maps for MakeHuman's body (docs/HUMANS.md "Skin").

    tools/makehuman/bake_skin.py MAKEHUMAN_DATA OUT_DIR [--size 1024]

Writes four PNGs in the body's UV layout into OUT_DIR (the engine
reads them from assets/humans/):

- skin_albedo.png: near white, multiplied by the skin colour the player
  picks, so every tone keeps its colour. It carries what a flat colour
  can't: redder lips, a blush over the cheeks, nose and ears, darker
  eyelids, and a faint blotchiness all over.
- skin_albedo_anime.png: the same for the anime style (docs/HUMANS.md
  "Anime style"): flat, with a blush on the cheeks and only a hint of lips.
- skin_normal.png: pores and fine grain (tangent space, OpenGL +Y up),
  deeper on the face's cheeks and nose than on the body.
- skin_roughness.png: lips and the face's T-zone shinier than the body.

Where each region is comes from MakeHuman's own CC0 skin weights: the
lips are what the oris* (mouth ring) bones move, the eyelids the
orbicularis* bones, the cheeks the risorius* and levator* bones. Every
pattern is generated here from a fixed seed, so the maps are the
project's own (MIT) and the same on every run.

Python standard library only, like cook_human.py: a 1024 bake takes
about a minute.
"""

import argparse
import math
import os
import random
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cook_human import load_json, read_obj  # noqa: E402

SEED = 20261025


def write_png(path, width, height, rows, channels):
    """rows: one bytes object per row, `channels` bytes a pixel (3 RGB, 4 RGBA)."""
    raw = b"".join(b"\x00" + r for r in rows)
    colour = {3: 2, 4: 6}[channels]

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, colour, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def region_weights(weights, nverts):
    """Per vertex: (lips, eyelids, cheeks, nose, ears) in 0..1."""
    def summed(prefixes):
        out = [0.0] * nverts
        for bone, pairs in weights.items():
            if any(bone.startswith(p) for p in prefixes):
                for v, w in pairs:
                    out[v] += w
        return [min(1.0, x) for x in out]

    oris = summed(["oris"])
    lids = summed(["orbicularis"])
    cheek = summed(["risorius", "levator05", "levator06"])
    nose = summed(["levator02", "levator03", "levator04"])
    ears = summed(["temporalis02"])
    # The mouth ring's weights reach out over the skin round the lips: only
    # where they are strong is it the lips themselves.
    lips = [max(0.0, min(1.0, (w - 0.55) / 0.3)) for w in oris]
    return lips, lids, cheek, nose, ears


def rasterise(size, faces, uvs, attrs, group):
    """Interpolates per-vertex attribute tuples over the body's UV triangles.
    Returns a list per pixel (None where no body triangle lands)."""
    grid = [None] * (size * size)
    for vs, ts, g in faces:
        if g != group:
            continue
        tris = [(0, 1, 2)] + ([(0, 2, 3)] if vs[3] != vs[2] else [])
        for a, b, c in tris:
            (ua, va), (ub, vb), (uc, vc) = uvs[ts[a]], uvs[ts[b]], uvs[ts[c]]
            xa, ya = ua * size, (1.0 - va) * size
            xb, yb = ub * size, (1.0 - vb) * size
            xc, yc = uc * size, (1.0 - vc) * size
            det = (yb - yc) * (xa - xc) + (xc - xb) * (ya - yc)
            if abs(det) < 1e-12:
                continue
            x0, x1 = max(0, int(min(xa, xb, xc))), min(size - 1, int(max(xa, xb, xc)) + 1)
            y0, y1 = max(0, int(min(ya, yb, yc))), min(size - 1, int(max(ya, yb, yc)) + 1)
            pa, pb, pc = attrs[vs[a]], attrs[vs[b]], attrs[vs[c]]
            for py in range(y0, y1 + 1):
                cy = py + 0.5
                for px in range(x0, x1 + 1):
                    cx = px + 0.5
                    l1 = ((yb - yc) * (cx - xc) + (xc - xb) * (cy - yc)) / det
                    l2 = ((yc - ya) * (cx - xc) + (xa - xc) * (cy - yc)) / det
                    l3 = 1.0 - l1 - l2
                    if l1 < -1e-4 or l2 < -1e-4 or l3 < -1e-4:
                        continue
                    grid[py * size + px] = tuple(l1 * p + l2 * q + l3 * r for p, q, r in zip(pa, pb, pc))
    return grid


def dilate(grid, size, passes):
    """Grows every UV island outwards, so filtering at a seam never reads the empty background."""
    for _ in range(passes):
        out = grid[:]
        for i, v in enumerate(grid):
            if v is not None:
                continue
            x, y = i % size, i // size
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < size and 0 <= ny < size and grid[ny * size + nx] is not None:
                    out[i] = grid[ny * size + nx]
                    break
        grid = out
    return grid


def value_noise(size, cells, rng):
    """Smooth noise in 0..1: a cells x cells random lattice, bilinear (wrapping)."""
    lattice = [rng.random() for _ in range(cells * cells)]
    out = [0.0] * (size * size)
    scale = cells / size
    for y in range(size):
        fy = y * scale
        iy = int(fy)
        ty = fy - iy
        ty = ty * ty * (3 - 2 * ty)
        r0, r1 = (iy % cells) * cells, ((iy + 1) % cells) * cells
        for x in range(size):
            fx = x * scale
            ix = int(fx)
            tx = fx - ix
            tx = tx * tx * (3 - 2 * tx)
            i0, i1 = ix % cells, (ix + 1) % cells
            top = lattice[r0 + i0] + (lattice[r0 + i1] - lattice[r0 + i0]) * tx
            bot = lattice[r1 + i0] + (lattice[r1 + i1] - lattice[r1 + i0]) * tx
            out[y * size + x] = top + (bot - top) * ty
    return out


def pores(size, rng):
    """A height field of small round dips (pores) over fine grain, 0..1."""
    h = [0.5 + (rng.random() - 0.5) * 0.25 for _ in range(size * size)]
    for _ in range(size * size // 9):
        cx, cy = rng.randrange(size), rng.randrange(size)
        depth = 0.25 + rng.random() * 0.35
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                fall = 1.0 if dx == 0 and dy == 0 else 0.35
                i = ((cy + dy) % size) * size + (cx + dx) % size
                h[i] -= depth * fall
    # Soft blur passes: grain, not salt.
    out = [0.0] * (size * size)
    for y in range(size):
        up, dn = ((y - 1) % size) * size, ((y + 1) % size) * size
        row = y * size
        for x in range(size):
            l, r = (x - 1) % size, (x + 1) % size
            out[row + x] = (4 * h[row + x] + h[row + l] + h[row + r] + h[up + x] + h[dn + x]) / 8.0
    return out


def bake(data_dir, out_dir, size, log=print):
    positions, uvs, faces, groups = read_obj(os.path.join(data_dir, "3dobjs", "base.obj"))
    body = groups.index("body")
    skel = load_json(os.path.join(data_dir, "rigs", "default.mhskel"))
    weights = load_json(os.path.join(data_dir, "rigs", skel.get("weights_file", "default_weights.mhw")))["weights"]
    lips, lids, cheek, nose, ears = region_weights(weights, len(positions))
    attrs = list(zip(lips, lids, cheek, nose, ears))

    log(f"rasterising {size}x{size}")
    grid = dilate(rasterise(size, faces, uvs, attrs, body), size, 6)
    rng = random.Random(SEED)
    log("noise")
    mottle = value_noise(size, 24, rng)
    fine = value_noise(size, 160, rng)
    height = pores(size, rng)

    log("writing maps")
    albedo, anime, normal, rough = [], [], [], []
    strength = 0.25  # pore depth to slope: the face island is small, so keep it faint
    for y in range(size):
        a_row, t_row, n_row, r_row = bytearray(), bytearray(), bytearray(), bytearray()
        for x in range(size):
            i = y * size + x
            lp, ld, ck, ns, er = grid[i] if grid[i] is not None else (0.0, 0.0, 0.0, 0.0, 0.0)
            face = min(1.0, ck + ns)
            # Albedo, a multiplier on the chosen skin colour.
            m = 1.0 + (mottle[i] - 0.5) * 0.07 + (fine[i] - 0.5) * 0.02
            r, g, b = m, m, m
            blush = 0.10 * ck + 0.08 * ns + 0.10 * er
            g -= blush * 0.9
            b -= blush * 0.8
            r, g, b = r - 0.04 * ld, g - 0.09 * ld, b - 0.06 * ld  # thinner, darker skin on the lids
            r, g, b = r * (1 - 0.06 * lp), g * (1 - 0.36 * lp), b * (1 - 0.30 * lp)  # the lips' red
            a_row += bytes(max(0, min(255, int(c * 248 + 0.5))) for c in (r, g, b))
            # The anime face's: flat skin, an anime blush on the cheeks, the
            # lips only a hint (a strong mouth reads as lipstick in that style).
            ab = 0.16 * ck
            t_row += bytes(max(0, min(255, int(c * 248 + 0.5)))
                           for c in (1.0 - 0.02 * lp, 1.0 - ab * 0.75 - 0.14 * lp, 1.0 - ab * 0.55 - 0.10 * lp))
            # Normal from the pore height's slope, deeper on the face.
            k = strength * (0.3 + 0.9 * face) * (1.0 - 0.7 * lp)  # lips are smooth
            hx = height[y * size + (x + 1) % size] - height[y * size + (x - 1) % size]
            hy = height[((y + 1) % size) * size + x] - height[((y - 1) % size) * size + x]
            nx, ny, nz = -hx * k, hy * k, 1.0  # image rows run down, +Y up
            inv = 1.0 / math.sqrt(nx * nx + ny * ny + nz * nz)
            n_row += bytes(int((c * inv * 0.5 + 0.5) * 255 + 0.5) for c in (nx, ny, nz)) + b"\xff"
            # Roughness: lips glossy, the T-zone a little shinier, pores rougher.
            rv = 0.62 - 0.30 * lp - 0.08 * ns + (0.5 - height[i]) * 0.10
            r_row += bytes((max(0, min(255, int(rv * 255 + 0.5))),) * 3)
        albedo.append(bytes(a_row))
        anime.append(bytes(t_row))
        normal.append(bytes(n_row))
        rough.append(bytes(r_row))
    os.makedirs(out_dir, exist_ok=True)
    write_png(os.path.join(out_dir, "skin_albedo.png"), size, size, albedo, 3)
    write_png(os.path.join(out_dir, "skin_albedo_anime.png"), size, size, anime, 3)
    write_png(os.path.join(out_dir, "skin_normal.png"), size, size, normal, 4)
    write_png(os.path.join(out_dir, "skin_roughness.png"), size, size, rough, 3)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("data_dir", help="the makehuman/data folder (tools/makehuman/makehuman.lock)")
    ap.add_argument("out_dir", help="where the PNGs go (assets/humans)")
    ap.add_argument("--size", type=int, default=1024)
    args = ap.parse_args()
    bake(args.data_dir, args.out_dir, args.size)


if __name__ == "__main__":
    main()
