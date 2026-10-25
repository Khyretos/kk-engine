#!/usr/bin/env python3
"""Bakes CharMorph Vitruvian's scanned skin detail onto MakeHuman's body
(docs/HUMANS.md "Skin").

    python -I tools/makehuman/vitruvian_export.py VITRUVIAN/char.blend vit.npz   # with bpy
    python -I tools/makehuman/bake_vitruvian.py MAKEHUMAN_DATA VITRUVIAN vit.npz OUT_DIR [--size 2048]

Vitruvian (CC0, Sean Buckley and Olaf Delgado-Friedrichs) is a far denser
body with 4K scanned-style skin textures over four UDIM tiles. Its mesh is
a different one, so this warps it onto MakeHuman's base body (matching
joints, the eyes and the face's tip points, then pulling it onto the
surface), and for each texel of the MakeHuman body's UV layout reads the
Vitruvian skin at the same spot. The realistic look is then a texture,
not geometry: the body stays MakeHuman's 13,000 vertices.

Writes into OUT_DIR, in the body's UV layout:

- skin_albedo.png: Vitruvian's colour divided by its average skin tone,
  so the player's skin colour still sets the tone: freckles, veins, the
  lips' and nipples' colour, redder knees and knuckles.
- skin_normal.png: Vitruvian's height (wrinkles, lip lines, creases,
  knuckles) plus fine pores, tangent space, OpenGL +Y up.
- skin_roughness.png: Vitruvian's roughness.

Needs numpy, scipy and OpenEXR (pip). The maps it writes are derived from
CC0 data, so they ship as the project's own.
"""

import argparse
import os
import struct
import sys
import zlib

import numpy as np
import OpenEXR
from scipy.interpolate import RBFInterpolator
from scipy.spatial import cKDTree

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bake_skin import region_weights  # noqa: E402
from cook_human import load_json, read_obj  # noqa: E402

SEED = 20261025
SKIN = (4, 5)  # Vitruvian's skin materials: "UDIM.Skin" and the skin under "charmorph_censor"

# Joints on both rigs: MakeHuman bone (head or tail) -> Vitruvian joint
# (CharMorph's mannequin rig, Unreal names). Each pair for both sides.
SIDED = [
    ("upperleg01", "head", "thigh_{}_head"),
    ("lowerleg01", "head", "calf_{}_head"),
    ("foot", "head", "foot_{}_head"),
    ("clavicle", "head", "clavicle_{}_head"),
    ("upperarm01", "head", "upperarm_{}_head"),
    ("lowerarm01", "head", "lowerarm_{}_head"),
    ("wrist", "head", "hand_{}_head"),
    ("finger1-3", "tail", "thumb_03_{}_tail"),
    ("finger2-1", "head", "index_metacarpal_{}_tail"),
    ("finger2-3", "tail", "index_03_{}_tail"),
    ("finger3-1", "head", "middle_metacarpal_{}_tail"),
    ("finger3-3", "tail", "middle_03_{}_tail"),
    ("finger4-3", "tail", "ring_03_{}_tail"),
    ("finger5-1", "head", "pinky_metacarpal_{}_tail"),
    ("finger5-3", "tail", "pinky_03_{}_tail"),
]


def write_png(path, image):
    """image: h x w x channels, uint8."""
    h, w, c = image.shape
    raw = b"".join(b"\x00" + image[y].tobytes() for y in range(h))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    kind = {1: 0, 3: 2, 4: 6}[c]
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, kind, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def srgb(linear):
    linear = np.clip(linear, 0.0, 1.0)
    return np.where(linear <= 0.0031308, linear * 12.92, 1.055 * np.power(linear, 1 / 2.4) - 0.055)


def tri_normals(co, tri):
    n = np.cross(co[tri[:, 1]] - co[tri[:, 0]], co[tri[:, 2]] - co[tri[:, 0]])
    return n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)


def vertex_normals(co, tri):
    n = np.zeros_like(co)
    fn = np.cross(co[tri[:, 1]] - co[tri[:, 0]], co[tri[:, 2]] - co[tri[:, 0]])
    for k in range(3):
        np.add.at(n, tri[:, k], fn)
    return n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)


class MakeHumanBody:
    def __init__(self, data_dir):
        positions, uvs, faces, groups = read_obj(os.path.join(data_dir, "3dobjs", "base.obj"))
        self.co = np.array(positions) * 0.1  # decimetres -> metres
        uvs = np.array(uvs)
        body = groups.index("body")
        tri, tuv = [], []
        for vs, ts, g in faces:
            if g != body:
                continue
            tri.append((vs[0], vs[1], vs[2]))
            tuv.append((ts[0], ts[1], ts[2]))
            if vs[3] != vs[2]:
                tri.append((vs[0], vs[2], vs[3]))
                tuv.append((ts[0], ts[2], ts[3]))
        self.tri = np.array(tri)
        self.uv = uvs[np.array(tuv)]  # triangles x corners x 2
        self.normals = vertex_normals(self.co, self.tri)
        self.used = np.unique(self.tri)
        skel = load_json(os.path.join(data_dir, "rigs", "default.mhskel"))
        self.joints, self.bones = skel["joints"], skel["bones"]
        weights = load_json(os.path.join(data_dir, "rigs", skel.get("weights_file", "default_weights.mhw")))["weights"]
        self.regions = np.array(region_weights(weights, len(self.co))).T  # lips, lids, cheek, nose, ears

    def joint(self, bone, end):
        return self.co[self.joints[self.bones[bone][end]]].mean(0)


class Vitruvian:
    def __init__(self, folder, npz):
        d = np.load(npz)
        co = d["co"]
        self.co = np.stack([co[:, 0], co[:, 2], -co[:, 1]], 1)  # Z-up facing -Y -> Y-up facing +Z
        skin = np.isin(d["mat"], SKIN)
        self.tri, self.uv = d["tri"][skin], d["uv"][skin]
        self.folder = folder
        self.rigs = {}

    def joint(self, rig, name):
        if rig not in self.rigs:
            d = np.load(os.path.join(self.folder, "joints", rig + ".npz"))
            names = bytes(d["names"]).decode().split("\0")
            starts = np.concatenate([[0], np.cumsum(d["cnt"])]).astype(int)
            self.rigs[rig] = (names, starts, d["idx"], d["weights"])
        names, starts, idx, w = self.rigs[rig]
        i = names.index("joint_" + name)
        s = slice(starts[i], starts[i + 1])
        return (self.co[idx[s]] * w[s, None]).sum(0) / w[s].sum()


def face_points(co, eye_l, eye_r, top):
    """Tip points of a face both bodies have: nose tip, chin, crown, the
    back of the head and the ears' outer edges."""
    mid = (eye_l + eye_r) / 2
    span = np.linalg.norm(eye_l - eye_r)
    head = co[(co[:, 1] > mid[1] - 2.2 * span) & (np.abs(co[:, 0]) < 2.5 * span)]
    centre = head[np.abs(head[:, 0]) < 0.08 * span]
    nose = centre[(centre[:, 1] < mid[1]) & (centre[:, 1] > mid[1] - span)]
    tip = nose[np.argmax(nose[:, 2])]
    chin_zone = centre[(centre[:, 1] < tip[1] - 0.6 * span) & (centre[:, 2] > mid[2] - 0.8 * span)]
    chin = chin_zone[np.argmin(chin_zone[:, 1] - 0.5 * chin_zone[:, 2])]
    crown = centre[np.argmax(centre[:, 1])] if top is None else top
    back = centre[(np.abs(centre[:, 1] - mid[1]) < 0.3 * span)]
    back = back[np.argmin(back[:, 2])]
    ring = head[np.abs(head[:, 1] - (mid[1] - 0.3 * span)) < 0.2 * span]
    ear_l, ear_r = ring[np.argmax(ring[:, 0])], ring[np.argmin(ring[:, 0])]
    return [tip, chin, crown, back, ear_l, ear_r]


def landmarks(mh, vit):
    src, dst = [], []
    for bone, end, joint in SIDED:
        for mh_side, vit_side in (("L", "l"), ("R", "r")):
            dst.append(mh.joint(f"{bone}.{mh_side}", end))
            src.append(vit.joint("mannequin", joint.format(vit_side)))
    eyes_mh = [mh.joint("eye.L", "head"), mh.joint("eye.R", "head")]
    eyes_vit = [vit.joint("rigify", "eye.L_head"), vit.joint("rigify", "eye.R_head")]
    dst += eyes_mh
    src += eyes_vit
    dst += face_points(mh.co[mh.used], *eyes_mh, None)
    src += face_points(vit.co[np.unique(vit.tri)], *eyes_vit, None)
    # Feet: heel and toe tips.
    for co, out in ((mh.co[mh.used], dst), (vit.co[np.unique(vit.tri)], src)):
        for side in (1, -1):
            foot = co[(co[:, 0] * side > 0) & (co[:, 1] < co[:, 1].min() + 0.05)]
            out.append(foot[np.argmax(foot[:, 2])])
            out.append(foot[np.argmin(foot[:, 2])])
    return np.array(src), np.array(dst)


def warp(vit, mh, log):
    """Vitruvian's vertices moved onto MakeHuman's body."""
    src, dst = landmarks(vit=vit, mh=mh)
    log(f"{len(src)} landmarks, mean gap {np.linalg.norm(src - dst, axis=1).mean() * 100:.1f} cm")
    moved = RBFInterpolator(src, dst - src, kernel="thin_plate_spline", smoothing=0.0)(vit.co) + vit.co
    # Then pull the surface onto MakeHuman's: nearest points that face the
    # same way become more pairs, the warp refitted with them, a few times.
    used = np.unique(vit.tri)
    rng = np.random.default_rng(SEED)
    sample = rng.choice(used, 5000, replace=False)
    target = mh.co[mh.used]
    target_n = mh.normals[mh.used]
    tree = cKDTree(target)
    for step, (smooth, reach) in enumerate(((1e-3, 0.10), (1e-3, 0.08), (3e-4, 0.05), (3e-4, 0.03), (1e-4, 0.02), (1e-4, 0.02))):
        n = vertex_normals(moved, vit.tri)
        dist, j = tree.query(moved[sample])
        keep = (dist < reach) & ((n[sample] * target_n[j]).sum(1) > 0.5)
        pairs_src = np.concatenate([src, vit.co[sample[keep]]])
        pairs_dst = np.concatenate([dst, target[j[keep]]])
        moved = RBFInterpolator(pairs_src, pairs_dst - pairs_src, kernel="thin_plate_spline",
                                smoothing=smooth)(vit.co) + vit.co
        log(f"fit {step + 1}: {keep.sum()} surface pairs, mean gap {dist[keep].mean() * 1000:.1f} mm")
    return moved


def closest_on_triangles(p, a, b, c):
    """Closest points to p on triangles abc (all n x 3): their barycentrics."""
    ab, ac, ap = b - a, c - a, p - a
    d1, d2 = (ab * ap).sum(1), (ac * ap).sum(1)
    bp = p - b
    d3, d4 = (ab * bp).sum(1), (ac * bp).sum(1)
    cp = p - c
    d5, d6 = (ab * cp).sum(1), (ac * cp).sum(1)
    va = d3 * d6 - d5 * d4
    vb = d5 * d2 - d1 * d6
    vc = d1 * d4 - d3 * d2
    denom = np.where(np.abs(va + vb + vc) < 1e-20, 1e-20, va + vb + vc)
    v, w = vb / denom, vc / denom
    # Outside the face: clamp onto the nearest edge (each edge's segment).
    out = (va < 0) | (vb < 0) | (vc < 0)
    if out.any():
        best = np.full(len(p), np.inf)
        bv, bw = v.copy(), w.copy()
        for x, ex, wx, wy in ((a, b - a, (0, 1), (0, 0)), (a, c - a, (0, 0), (0, 1)), (b, c - b, (1, 0), (0, 1))):
            t = np.clip(((p - x) * ex).sum(1) / np.maximum((ex * ex).sum(1), 1e-20), 0, 1)
            q = x + ex * t[:, None]
            d = ((p - q) ** 2).sum(1)
            # q's barycentrics (v, w): from the edge's start to its end
            ev = wx[0] + (wx[1] - wx[0]) * t
            ew = wy[0] + (wy[1] - wy[0]) * t
            take = out & (d < best)
            best[take], bv[take], bw[take] = d[take], ev[take], ew[take]
        v, w = np.where(out, bv, v), np.where(out, bw, w)
    return 1 - v - w, v, w


def project(pos, nrm, co, tri, uv, candidates=12, chunk=200000):
    """For each point: the UV of the nearest spot on the triangles that faces
    its way (lips and fingers sit close to another surface), and the gap."""
    centres = co[tri].mean(1)
    facing = tri_normals(co, tri)
    tree = cKDTree(centres)
    out_uv = np.zeros((len(pos), 2), np.float32)
    out_gap = np.zeros(len(pos), np.float32)
    for s in range(0, len(pos), chunk):
        p, n = pos[s:s + chunk], nrm[s:s + chunk]
        _, near = tree.query(p, k=candidates)
        best = np.full(len(p), np.inf)
        for k in range(candidates):
            t = near[:, k]
            a, b, c = co[tri[t, 0]], co[tri[t, 1]], co[tri[t, 2]]
            u, v, w = closest_on_triangles(p, a, b, c)
            q = a * u[:, None] + b * v[:, None] + c * w[:, None]
            d = np.sqrt(((p - q) ** 2).sum(1))
            d = np.where((facing[t] * n).sum(1) > 0.3, d, d + 1.0)  # facing away: only as a last resort
            take = d < best
            best[take] = d[take]
            out_uv[s:s + chunk][take] = (uv[t, 0] * u[:, None] + uv[t, 1] * v[:, None] + uv[t, 2] * w[:, None])[take]
        out_gap[s:s + chunk] = np.where(best >= 1.0, best - 1.0, best)
    return out_uv, out_gap


def rasterise(size, uv_tris):
    """Per texel: the triangle it lies in (-1 for none) and its barycentrics."""
    owner = np.full((size, size), -1, np.int32)
    bary = np.zeros((size, size, 3), np.float32)
    px = uv_tris[:, :, 0] * size
    py = (1.0 - uv_tris[:, :, 1]) * size
    for t in range(len(uv_tris)):
        xa, xb, xc = px[t]
        ya, yb, yc = py[t]
        det = (yb - yc) * (xa - xc) + (xc - xb) * (ya - yc)
        if abs(det) < 1e-12:
            continue
        x0, x1 = max(0, int(min(xa, xb, xc))), min(size - 1, int(max(xa, xb, xc)) + 1)
        y0, y1 = max(0, int(min(ya, yb, yc))), min(size - 1, int(max(ya, yb, yc)) + 1)
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        l1 = ((yb - yc) * (gx - xc) + (xc - xb) * (gy - yc)) / det
        l2 = ((yc - ya) * (gx - xc) + (xa - xc) * (gy - yc)) / det
        l3 = 1.0 - l1 - l2
        inside = (l1 >= -1e-4) & (l2 >= -1e-4) & (l3 >= -1e-4)
        ys, xs = gy[inside].astype(int), gx[inside].astype(int)
        owner[ys, xs] = t
        bary[ys, xs] = np.stack([l1[inside], l2[inside], l3[inside]], 1)
    return owner, bary


def dilate(image, filled, passes):
    """Grows every UV island outwards, so filtering at a seam never reads the background."""
    image, filled = image.copy(), filled.copy()
    for _ in range(passes):
        grown = image.copy()
        got = filled.copy()
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            shifted = np.roll(np.roll(image, dy, 0), dx, 1)
            sf = np.roll(np.roll(filled, dy, 0), dx, 1)
            take = ~got & sf
            grown[take] = shifted[take]
            got |= take
        image, filled = grown, got
    return image


def read_tiles(folder, name, size):
    """A UDIM set (1001-1004) as one array per tile, box-filtered to size."""
    tiles = []
    for tile in range(4):
        with OpenEXR.File(os.path.join(folder, "textures", "4K", f"{name}.{1001 + tile}.exr")) as f:
            pixels = next(iter(f.channels().values())).pixels.astype(np.float32)
        k = pixels.shape[0] // size
        if k > 1:
            pixels = pixels.reshape(size, k, size, k, *pixels.shape[2:]).mean((1, 3))
        tiles.append(pixels)
    return tiles


def sample(tiles, uv):
    """Bilinear reads at UDIM coordinates (u's integer part is the tile)."""
    size = tiles[0].shape[0]
    tile = np.clip(np.floor(uv[:, 0]).astype(int), 0, len(tiles) - 1)
    x = (uv[:, 0] - tile) * size - 0.5
    y = (1.0 - uv[:, 1]) * size - 0.5
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = x - x0, y - y0
    stack = np.stack(tiles)
    out = 0
    for dy, wy in ((0, 1 - fy), (1, fy)):
        for dx, wx in ((0, 1 - fx), (1, fx)):
            px = np.clip(x0 + dx, 0, size - 1)
            py = np.clip(y0 + dy, 0, size - 1)
            v = stack[tile, py, px]
            w = wx * wy
            out = out + (v * (w[:, None] if v.ndim > 1 else w))
    return out


def blur(image, radius):
    """A box blur, three passes (near Gaussian), wrapping."""
    for _ in range(3):
        for axis in (0, 1):
            acc = np.zeros_like(image)
            for d in range(-radius, radius + 1):
                acc += np.roll(image, d, axis)
            image = acc / (2 * radius + 1)
    return image


def pores(size, rng):
    """Small round dips over fine grain, 0..1, tiling."""
    h = 0.5 + (rng.random((size, size)) - 0.5) * 0.25
    count = size * size // 9
    ys, xs = rng.integers(0, size, count), rng.integers(0, size, count)
    depth = 0.25 + rng.random(count) * 0.35
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            fall = 1.0 if dx == 0 and dy == 0 else 0.35
            np.subtract.at(h, ((ys + dy) % size, (xs + dx) % size), depth * fall)
    return (4 * h + np.roll(h, 1, 0) + np.roll(h, -1, 0) + np.roll(h, 1, 1) + np.roll(h, -1, 1)) / 8.0


def bake(data_dir, vit_dir, vit_npz, out_dir, size, log=print):
    mh = MakeHumanBody(data_dir)
    vit = Vitruvian(vit_dir, vit_npz)
    moved = warp(vit, mh, log)

    log(f"rasterising {size}x{size}")
    owner, bary = rasterise(size, mh.uv)
    filled = owner >= 0
    t = owner[filled]
    b = bary[filled]
    corners = mh.tri[t]
    pos = (mh.co[corners] * b[:, :, None]).sum(1)
    nrm = (mh.normals[corners] * b[:, :, None]).sum(1)
    regions = (mh.regions[corners] * b[:, :, None]).sum(1)

    log("finding the Vitruvian skin under each texel")
    src_uv, gap = project(pos, nrm, moved, vit.tri, vit.uv)
    log(f"median gap {np.median(gap) * 1000:.1f} mm, 99% under {np.percentile(gap, 99) * 1000:.1f} mm")
    if os.environ.get("KKE_BAKE_DEBUG"):
        dbg = np.zeros((size, size), np.float32)
        dbg[filled] = np.clip(gap / 0.02, 0, 1)
        write_png(os.path.join(out_dir, "debug_gap.png"), (dbg[..., None] * 255).astype(np.uint8))
        np.save(os.path.join(out_dir, "debug_uv.npy"), src_uv)

    tex = 1024 if size <= 1024 else 2048
    log("reading the Vitruvian textures")
    colour = sample(read_tiles(vit_dir, "Light_Skin_Color", tex), src_uv)
    height = sample(read_tiles(vit_dir, "Skin_Height", tex), src_uv)
    rough = sample(read_tiles(vit_dir, "Skin_Roughness", tex), src_uv)

    log("writing maps")
    # Albedo: the colour over the body's average tone, so the player's
    # colour sets the tone and this only its variation.
    tone = np.median(colour, 0)
    ratio = colour / tone
    # The average at 0.9, the lighter places (palms, the chest's centre)
    # rolled off softly above 0.8 instead of clipped.
    ratio = ratio * 0.9
    ratio = np.where(ratio < 0.8, ratio, 0.8 + 0.2 * (1 - np.exp(-(ratio - 0.8) / 0.2)))
    # Then the lips' red and a blush over the cheeks, nose and ears, from
    # MakeHuman's own regions (as bake_skin.py): Vitruvian's own are faint.
    lp, ld, ck, ns, er = regions.T
    # Vitruvian's lips and veins are bluer than its tone, which over a warm
    # skin reads as bruises: half its colour shifts, none on the lips.
    # Its brightness swings (dark knees and elbows) at two thirds.
    lum = ratio.mean(1, keepdims=True)
    ratio = lum + (ratio - lum) * (0.5 * (1 - lp))[:, None]
    ratio = ratio - (lum - 0.9) / 3
    blush = 0.08 * ck + 0.06 * ns + 0.08 * er
    tint = np.stack([1 - 0.03 * ld - 0.04 * lp,
                     1 - 0.9 * blush - 0.07 * ld - 0.30 * lp,
                     1 - 0.8 * blush - 0.05 * ld - 0.24 * lp], 1)
    ratio = ratio * tint
    log(f"skin tone {np.round(tone, 3)}")
    albedo = np.ones((size, size, 3), np.float32)
    albedo[filled] = np.clip(ratio, 0, 1)
    albedo = dilate(albedo, filled, 8)
    write_png(os.path.join(out_dir, "skin_albedo.png"), (srgb(albedo) * 255 + 0.5).astype(np.uint8))

    # Height: Vitruvian's (creases, lip lines, wrinkles), minus its slow
    # swell (that is the shape's job), plus pores.
    hmap = np.zeros((size, size), np.float32)
    hmap[filled] = height
    hmap = dilate(hmap[..., None], filled, 8)[..., 0]
    detail = hmap - blur(hmap, max(2, size // 128))
    lips = np.zeros((size, size), np.float32)
    lips[filled] = regions[:, 0]
    face = np.zeros((size, size), np.float32)
    face[filled] = np.minimum(1.0, regions[:, 2] + regions[:, 3])
    pore = pores(size, np.random.default_rng(SEED)) - 0.5
    scale = size / 1024.0
    k_detail = 6.0 * scale
    k_pore = 0.25 * (0.3 + 0.9 * face) * (1.0 - 0.7 * lips)
    total = detail * k_detail * (0.5 + 0.5 * face) + pore * k_pore
    hx = np.roll(total, -1, 1) - np.roll(total, 1, 1)
    hy = np.roll(total, -1, 0) - np.roll(total, 1, 0)
    n = np.stack([-hx, hy, np.ones_like(hx)], 2)  # image rows run down, +Y up
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    normal = np.concatenate([(n * 0.5 + 0.5) * 255 + 0.5, np.full((size, size, 1), 255.0)], 2)
    write_png(os.path.join(out_dir, "skin_normal.png"), normal.astype(np.uint8))

    rmap = np.full((size, size), 0.6, np.float32)
    # Vitruvian's roughness is for Blender's skin shader, which reads
    # it shinier than ours: keep its pattern round our base of 0.6.
    rmap[filled] = np.clip(0.6 + (rough - np.median(rough)) * 0.8 - 0.25 * regions[:, 0], 0.2, 1.0)
    rmap = dilate(rmap[..., None], filled, 8)
    write_png(os.path.join(out_dir, "skin_roughness.png"),
              np.repeat((rmap * 255 + 0.5).astype(np.uint8), 3, 2))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("data_dir", help="the makehuman/data folder (tools/makehuman/makehuman.lock)")
    ap.add_argument("vitruvian_dir", help="CharMorph-Vitruvian (joints/ and textures/4K/)")
    ap.add_argument("vitruvian_npz", help="vitruvian_export.py's output")
    ap.add_argument("out_dir")
    ap.add_argument("--size", type=int, default=2048)
    args = ap.parse_args()
    os.makedirs(args.out_dir, exist_ok=True)
    bake(args.data_dir, args.vitruvian_dir, args.vitruvian_npz, args.out_dir, args.size)


if __name__ == "__main__":
    main()
