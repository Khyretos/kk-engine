#!/usr/bin/env python3
"""Makes the Vitruvian body pack (docs/HUMANS.md "The Vitruvian body").

    python -I tools/makehuman/vitruvian_export.py VITRUVIAN/char.blend vit.npz   # with bpy
    python -I tools/makehuman/vitruvian_pack.py assets/humans/makehuman.kkhpack VITRUVIAN vit.npz OUT_DIR [--tile 2048]

CharMorph's Vitruvian body (CC0, Sean Buckley and Olaf Delgado-Friedrichs) is a
37,000 vertex body with a real nose, lips, eyelids and ears, and 4K skin maps
over four UDIM tiles. MakeHuman's pack has the sliders (1,280 morph targets),
the rig and its skin weights, and the helper meshes the clothes and the hair
are cut from. This puts the first on the second's terms, so every part of the
engine that reads a pack (kke::Human, the clothes, the hair, the creator)
works on the better body unchanged:

1. Vitruvian is moved into MakeHuman's pose and size by a thin-plate spline
   through 42 points both bodies have (joints, eyes, nose tip, chin...). Only
   that: its own face and body are kept. A second fit, pulling its surface
   onto MakeHuman's, is used only to find which MakeHuman triangle lies under
   each Vitruvian vertex (median 0.5 mm).
2. Through that correspondence every MakeHuman target, skin weight and joint
   is carried to the Vitruvian vertices (barycentric), with the rest shape
   chosen so the default sliders land on Vitruvian's own shape (MakeHuman's
   default body-type sliders move its base by 30 mm on average; that is
   subtracted).
3. MakeHuman's helper meshes (the body suit and skirt the clothes are cut
   from, eyes, teeth, tongue, lashes, the joint markers) ride a smooth field
   from MakeHuman's default skin to Vitruvian's. The eyeballs are fitted to
   Vitruvian's own (centre, radius): the brows and lashes are placed from them.
4. The skin maps are Vitruvian's own, repacked from the four UDIM tiles into one
   2x2 atlas (UVs remapped): colour over its median tone (the player's skin
   colour still sets the tone), height as a tangent-space normal map with fine
   pores, roughness.

Writes OUT_DIR/vitruvian.kkhpack and vitruvian_skin_{albedo,normal,roughness}.jpg.
Needs numpy, scipy, OpenEXR and Pillow (pip). The outputs are derived from
CC0 data, so they ship as the project's own.
"""

import argparse
import json
import os
import struct
import sys
import time
import zlib

import numpy as np
import scipy.sparse as sp
from PIL import Image
from scipy.interpolate import RBFInterpolator
from scipy.spatial import cKDTree

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bake_vitruvian as bv  # noqa: E402
import cook_human as ch  # noqa: E402

STEP = 0.001  # the pack's offset step, in MakeHuman units (decimetres)
HEIGHT = 0.07  # the height slider's ends: the whole body 7 % smaller or taller
# The body sliders that Vitruvian's own morphs back (MakeHuman's carried onto this finer body made balloon hips and
# crumpled chests): target (without -decr / -incr) -> [(morph, factor)]. A -decr target is the morph at -1 where
# the morph has a negative range (morphs.json), nothing where it has not.
DETAIL = {
    "measure/measure-waist-circ": [("Waist_Width", 1.0)],
    "measure/measure-hips-circ": [("Waist_Hips_Width", 1.0)],
    "buttocks/buttocks-volume": [("Waist_GluteSize", 1.0)],
    "measure/measure-thigh-circ": [("Legs_Thigh_Size", 1.0)],
    "measure/measure-calf-circ": [("Legs_Calves_Size", 1.0)],
    "measure/measure-underbust-circ": [("Torso_RibWidth", 1.0)],
    "measure/measure-bust-circ": [("Chest_Breast_Size", 0.6)],
    "measure/measure-neck-circ": [("Neck_Girth", 1.0)],
    "measure/measure-neck-height": [("Neck_Length", 1.0)],
    "measure/measure-shoulder-dist": [("Shoulders_ShoulderWidth", 1.0)],
    "measure/measure-upperarm-circ": [("Arms_BicepSize", 1.0)],
    "measure/measure-wrist-circ": [("Arms_Forearm_Girth", 0.5)],
    "stomach/stomach-pregnant": [("Stomach_Bloating", 1.0)],
    "neck/neck-double": [("Neck_Girth", 0.6)],
    "torso/torso-scale-horiz": [("Torso_RibWidth", 1.0)],
    "torso/torso-vshape": [("Shoulders_ShoulderWidth", 0.8), ("Shoulders_ShoulderTrapeziusSize", 0.4)],
    "torso/torso-muscle-dorsi": [("Shoulders_ShoulderBladeWidth", 1.0)],
    "hip/hip-scale-horiz": [("Waist_Hips_Width", 1.0)],
}
for _side in ("l", "r"):  # both sides carry the one morph of Vitruvian's: half each
    DETAIL.update({
        f"armslegs/{_side}-upperleg-fat": [("Legs_Thigh_Size", 0.5)],
        f"armslegs/{_side}-upperleg-muscle": [("Legs_Quad_Size", 0.5)],
        f"armslegs/{_side}-lowerleg-fat": [("Legs_Calves_Size", 0.5)],
        f"armslegs/{_side}-lowerleg-muscle": [("Legs_Calves_Size", 0.3)],
        f"armslegs/{_side}-upperarm-fat": [("Arms_TricepSize", 0.5)],
        f"armslegs/{_side}-upperarm-muscle": [("Arms_BicepSize", 0.5)],
        f"armslegs/{_side}-lowerarm-fat": [("Arms_Forearm_Girth", 0.5)],
    })
# Body shapes: one target each, no -decr / -incr.
SHAPES = {
    "full-hourglass": [("Waist_Hips_Width", 0.5), ("Waist_GluteSize", 0.5), ("Waist_Width", -0.4), ("Chest_Breast_Size", 0.3)],
    "neat-hourglass": [("Waist_Hips_Width", 0.25), ("Waist_GluteSize", 0.25), ("Waist_Width", -0.4)],
    "triangle": [("Waist_Hips_Width", 0.5), ("Waist_GluteSize", 0.4), ("Legs_Thigh_Size", 0.3), ("Shoulders_ShoulderWidth", -0.2)],
    "invert-triangle": [("Shoulders_ShoulderWidth", 0.6), ("Shoulders_ShoulderTrapeziusSize", 0.3), ("Waist_Hips_Width", -0.4)],
    "apple": [("Stomach_Bloating", 0.6), ("Waist_Width", 0.5), ("Waist_Hips_Width", -0.3)],
    "lean-column": [("Waist_Hips_Width", -0.4), ("Waist_Width", -0.2), ("Chest_FemaleFlatChested", 0.5)],
    "rectangle": [("Waist_Width", 0.2)],
    "trapezoid": [("Shoulders_ShoulderWidth", 0.6), ("Shoulders_ShoulderTrapeziusSize", 0.6), ("Torso_RibWidth", 0.3)],
}
SKIN = (4, 5)  # Vitruvian's skin materials in the file
EYEBALL = 3  # and its eyeballs

# ---- reading a pack (cook_human.py's format, read back) ---------------------


def read_pack(path):
    b = open(path, "rb").read()
    assert b[:8] == b"KKHPACK1"
    n = struct.unpack_from("<I", b, 12)[0]
    o = 16
    ch_ = {}
    for _ in range(n):
        tag = b[o:o + 4]
        ln = struct.unpack_from("<I", b, o + 4)[0]
        ch_[tag] = b[o + 8:o + 8 + ln]
        o = (o + 8 + ln + 3) & ~3
    meta = json.loads(ch_[b"META"])
    pos = np.frombuffer(ch_[b"VPOS"], "<f4").reshape(-1, 3).copy()
    uv = np.frombuffer(ch_[b"VTUV"], "<f4").reshape(-1, 2).copy()
    f = ch_[b"FACE"]
    cnt = struct.unpack_from("<I", f, 0)[0]
    p = 4
    verts = np.frombuffer(f, "<u4", cnt * 4, p).reshape(-1, 4).copy()
    p += cnt * 16
    uvi = np.frombuffer(f, "<u4", cnt * 4, p).reshape(-1, 4).copy()
    p += cnt * 16
    grp = np.frombuffer(f, "<u2", cnt, p).copy()
    t = ch_[b"TGTZ"]
    raw = zlib.decompress(t[4:])
    tc = struct.unpack_from("<I", raw, 0)[0]
    q = 4
    table = []
    for _ in range(tc):
        q = (q + 3) & ~3
        sl = struct.unpack_from("<H", raw, q)[0]
        q += 2
        name = raw[q:q + sl].decode()
        q += sl
        q = (q + 3) & ~3
        m = struct.unpack_from("<I", raw, q)[0]
        q += 4
        table.append((name, m))
    q = (q + 3) & ~3
    total = sum(m for _, m in table)

    def planar(fmt, off):
        a = raw[off:off + total * 2]
        lo, hi = np.frombuffer(a[:total], "u1"), np.frombuffer(a[total:], "u1")
        return (lo.astype("u2") | (hi.astype("u2") << 8)).view(fmt), off + total * 2

    step, q = planar("<u2", q)
    xs, q = planar("<i2", q)
    ys, q = planar("<i2", q)
    zs, q = planar("<i2", q)
    targets, k = {}, 0
    for name, m in table:
        idx = np.cumsum(step[k:k + m].astype(np.int64)) if m else np.zeros(0, np.int64)
        targets[name] = (idx, np.stack([xs[k:k + m], ys[k:k + m], zs[k:k + m]], 1).astype("f4") * STEP)
        k += m
    w = ch_[b"WGHT"]
    nb = struct.unpack_from("<I", w, 0)[0]
    q = 4
    weights = {}
    for _ in range(nb):
        q = (q + 3) & ~3
        sl = struct.unpack_from("<H", w, q)[0]
        q += 2
        name = w[q:q + sl].decode()
        q += sl
        q = (q + 3) & ~3
        m = struct.unpack_from("<I", w, q)[0]
        q += 4
        q = (q + 3) & ~3
        vi = np.frombuffer(w, "<u2", m, q).copy()
        q += m * 2
        q = (q + 3) & ~3
        wt = np.frombuffer(w, "<f4", m, q).copy()
        q += m * 4
        weights[name] = (vi, wt)
    return dict(meta=meta, pos=pos, uv=uv, quads=verts, quvs=uvi, qgroup=grp, targets=targets, weights=weights)


# ---- kke::Human's body-type sliders, to know the default shape ---------------

LEVELS = {
    "female": ("Gender", 0), "male": ("Gender", 1),
    "baby": ("Age", 0), "child": ("Age", 1), "young": ("Age", 2), "old": ("Age", 3),
    "minmuscle": ("Muscle", 0), "averagemuscle": ("Muscle", 1), "maxmuscle": ("Muscle", 2),
    "minweight": ("Weight", 0), "averageweight": ("Weight", 1), "maxweight": ("Weight", 2),
    "minheight": ("Height", 0), "maxheight": ("Height", 2),
    "uncommonproportions": ("Proportions", 0), "idealproportions": ("Proportions", 2),
    "mincup": ("Cup", 0), "averagecup": ("Cup", 1), "maxcup": ("Cup", 2),
    "minfirmness": ("Firmness", 0), "averagefirmness": ("Firmness", 1), "maxfirmness": ("Firmness", 2),
    "african": ("African", 0), "asian": ("Asian", 0), "caucasian": ("Caucasian", 0),
}
ANCESTRY = ("African", "Asian", "Caucasian")
LEVEL_AT = {"Gender": [0.0, 1.0], "Age": [0.0, 0.1875, 0.5, 1.0]}
DEFAULT = {"Gender": 0.5, "Age": 0.5, "Muscle": 0.5, "Weight": 0.5, "Height": 0.5, "Proportions": 0.5,
           "Cup": 0.5, "Firmness": 0.5, "African": 1 / 3, "Asian": 1 / 3, "Caucasian": 1 / 3}


def hat(at, level, x):
    p = at[level]
    if x <= p:
        if level == 0:
            return 1.0
        lo = at[level - 1]
        return 0.0 if x <= lo else (x - lo) / (p - lo)
    if level + 1 >= len(at):
        return 1.0
    hi = at[level + 1]
    return 0.0 if x >= hi else (hi - x) / (hi - p)


def macro_words(name):
    words = [w for w in name.rsplit("/", 1)[-1].split("-") if w != "universal"]
    return words if words and all(w in LEVELS for w in words) else None


def macro_weights(names, value):
    out = {}
    for name in names:
        words = macro_words(name)
        if not words:
            continue
        w = 1.0
        for word in words:
            var, level = LEVELS[word]
            w *= value[var] if var in ANCESTRY else hat(LEVEL_AT.get(var, [0.0, 0.5, 1.0]), level, value[var])
            if w <= 0:
                break
        if w > 1e-5:
            out[name] = w
    return out


def default_shape(P):
    d = np.zeros_like(P["pos"])
    for name, w in macro_weights(P["targets"], DEFAULT).items():
        idx, off = P["targets"][name]
        d[idx] += off * w
    return d


class PackBody:
    """What bake_vitruvian's warp wants of a MakeHuman body, from a pack."""

    def __init__(self, P, shaped_dm):
        meta = P["meta"]
        body = [g["name"] for g in meta["groups"]].index("body")
        self.co = shaped_dm * 0.1
        tri = []
        for q, g in zip(P["quads"], P["qgroup"]):
            if g != body:
                continue
            tri.append((q[0], q[1], q[2]))
            if q[3] != q[2]:
                tri.append((q[0], q[2], q[3]))
        self.tri = np.array(tri)
        self.normals = bv.vertex_normals(self.co, self.tri)
        self.used = np.unique(self.tri)
        self.joints = meta["skeleton"]["joints"]
        self.bones = {b["name"]: {"head": b["head"], "tail": b["tail"]} for b in meta["skeleton"]["bones"]}

    def joint(self, bone, end):
        return self.co[self.joints[self.bones[bone][end]]].mean(0)


def atlas_uv(uv):
    """UDIM (tile = floor(u), four in a row) to one 2x2 atlas, v up."""
    tile = np.clip(np.floor(uv[..., 0] + 1e-6), 0, 3)
    u = uv[..., 0] - tile
    col, row = tile % 2, tile // 2
    return np.stack([(col + u) / 2, (uv[..., 1] + 1 - row) / 2], -1).astype("f4")


# ---- the geometry -------------------------------------------------------------


def build_pack(P, vit_npz, vit_dir, log):
    D = default_shape(P)
    shaped = P["pos"] + D
    log(f"MakeHuman's default sliders move its base {np.linalg.norm(D, axis=1).mean() * 100:.1f} mm on average")
    mh = PackBody(P, shaped)
    d = np.load(vit_npz)
    tmp = dict((k, d[k]) for k in d.files)
    vit = bv.Vitruvian.__new__(bv.Vitruvian)
    co = tmp["co"]
    vit.co = np.stack([co[:, 0], co[:, 2], -co[:, 1]], 1)  # Z-up facing -Y -> Y-up facing +Z
    skin = np.isin(tmp["mat"], SKIN)
    vit.tri, vit.uv = tmp["tri"][skin], tmp["uv"][skin]
    vit.folder, vit.rigs = vit_dir, {}
    vused = np.unique(vit.tri)

    # Shape: landmarks only. Correspondence: the refits.
    src, dst = bv.landmarks(vit=vit, mh=mh)
    warp = RBFInterpolator(src, dst - src, kernel="thin_plate_spline", smoothing=0.0)
    S = warp(vit.co) + vit.co
    M = bv.warp(vit, mh, log)
    tri, centres = mh.tri, mh.co[mh.tri].mean(1)
    facing = bv.tri_normals(mh.co, tri)
    tree = cKDTree(centres)
    vn = bv.vertex_normals(M, vit.tri)
    pts = M[vused]
    _, near = tree.query(pts, k=12)
    best = np.full(len(pts), np.inf)
    owner = np.zeros(len(pts), int)
    bary = np.zeros((len(pts), 3))
    for k in range(12):
        t = near[:, k]
        a, b, c = mh.co[tri[t, 0]], mh.co[tri[t, 1]], mh.co[tri[t, 2]]
        u, v, w = bv.closest_on_triangles(pts, a, b, c)
        q = a * u[:, None] + b * v[:, None] + c * w[:, None]
        dist = np.sqrt(((pts - q) ** 2).sum(1))
        dist = np.where((facing[t] * vn[vused]).sum(1) > 0.3, dist, dist + 1.0)
        take = dist < best
        best[take], owner[take] = dist[take], t[take]
        bary[take] = np.stack([u, v, w], 1)[take]
    gap = np.where(best >= 1.0, best - 1.0, best)
    log(f"correspondence: median {np.median(gap) * 1000:.1f} mm, 99% {np.percentile(gap, 99) * 1000:.1f} mm")

    # Vertices along a Z-order curve: neighbours get similar offsets, which the pack's zlib likes.
    qz = np.clip(((S[vused] - S[vused].min(0)) / (np.ptp(S[vused], axis=0) + 1e-9) * 1023).astype(np.int64), 0, 1023)
    code = np.zeros(len(vused), np.int64)
    for bit in range(10):
        for ax in range(3):
            code |= ((qz[:, ax] >> bit) & 1) << (3 * bit + ax)
    order = np.argsort(code, kind="stable")
    vused, owner, bary = vused[order], owner[order], bary[order]

    meta = P["meta"]
    names = [g["name"] for g in meta["groups"]]
    body_g = names.index("body")
    nmh = len(P["pos"])
    corner = mh.tri[owner]
    body_verts = set(np.unique(mh.tri).tolist())
    others = np.array([i for i in range(nmh) if i not in body_verts])
    nv, no = len(vused), len(others)
    log(f"{nv} Vitruvian skin vertices + {no} MakeHuman helper vertices")
    assert nv + no < 65536, "the pack stores vertex numbers in 16 bits"
    new_of_mh = {int(o): nv + k for k, o in enumerate(others)}
    vit_new = np.full(len(vit.co), -1, int)
    vit_new[vused] = np.arange(nv)

    # T: new vertex <- MakeHuman vertices (barycentric for skin, identity for helpers).
    rows = np.concatenate([np.repeat(np.arange(nv), 3), nv + np.arange(no)])
    cols = np.concatenate([corner.reshape(-1), others])
    vals = np.concatenate([bary.reshape(-1), np.ones(no)])
    T = sp.csr_matrix((vals, (rows, cols)), shape=(nv + no, nmh))

    # Rest shape (dm): the default sliders land on Vitruvian's own shape.
    want = np.zeros((nv + no, 3))
    want[:nv] = S[vused] * 10
    rng = np.random.default_rng(bv.SEED)
    pick = rng.choice(nv, 3000, replace=False)
    # Smooth and low frequency: the suit and skirt are coarse cloth, and a field that follows every bump of
    # the finer skin makes lumps and ragged hems.
    field = RBFInterpolator(M[vused][pick] * 10, (S[vused][pick] - M[vused][pick]) * 10, kernel="thin_plate_spline", smoothing=20.0)
    want[nv:] = shaped[others] + field(shaped[others])
    eye_pts = S[np.unique(tmp["tri"][tmp["mat"] == EYEBALL])] * 10
    for name in ("helper-l-eye", "helper-r-eye"):
        g = meta["groups"][names.index(name)]
        mhv = np.unique(P["quads"][g["firstFace"]:g["firstFace"] + g["faceCount"]])
        pos = shaped[mhv]
        c = pos.mean(0)
        r = np.linalg.norm(pos - c, axis=1).mean()
        mine = eye_pts[np.sign(eye_pts[:, 0]) == np.sign(c[0])]
        cv = mine.mean(0)
        rv = np.linalg.norm(mine - cv, axis=1).mean()
        for m in mhv:
            want[new_of_mh[int(m)]] = cv + (shaped[m] - c) * (rv / r)
        log(f"{name}: radius {r * 100:.1f} -> {rv * 100:.1f} mm")

    # The body-type (macro) targets are Vitruvian's own morphs (gender, age, body type, ancestry, breast size,
    # morphs/L2), moved into the pack's frame and put under MakeHuman's target names, so the slider maths in
    # kke::Human is unchanged. MakeHuman's own carried onto this finer body made crumpled chests and balloon
    # hips; its detail targets (nose, ears, ...) are still carried.
    skin_dm = S[vused] * 10
    helper_dm = want[nv:]
    htree = cKDTree(skin_dm)
    hd, hi = htree.query(helper_dm, k=12)
    hw = np.exp(-(hd / 0.3) ** 2 / 2) + 1e-9  # 3 cm Gaussian over the nearest skin: the suit, eyes and joints follow it
    hw /= hw.sum(1, keepdims=True)
    HM = sp.csr_matrix((hw.ravel(), (np.repeat(np.arange(no), 12), hi.ravel())), shape=(no, nv))
    l2 = os.path.join(vit_dir, "morphs", "L2")
    cache = {}

    def own(name):
        """A Vitruvian morph as offsets (dm) of the pack's vertices."""
        if name not in cache:
            npy = os.path.join(l2, name + ".npy")
            if os.path.exists(npy):
                d = np.load(npy).astype("f8")
                rows = np.nonzero(np.abs(d).sum(1) > 0)[0]
                delta = d[rows]
            else:
                z = np.load(os.path.join(l2, name + ".npz"))
                rows, delta = z["idx"].astype(int), z["delta"].astype("f8")
            delta = np.stack([delta[:, 0], delta[:, 2], -delta[:, 1]], 1)  # Z-up facing -Y -> Y-up facing +Z
            moved = vit.co[rows] + delta
            full = np.zeros_like(vit.co)
            full[rows] = warp(moved) + moved - S[rows]  # the same landmark warp the body went through
            sk = full[vused] * 10
            cache[name] = np.vstack([sk, HM @ sk])
        return cache[name]

    def scaled(k):
        return np.vstack([skin_dm, helper_dm]) * k

    def level(var, lv):
        if var == "Gender":
            return own("Gender_Female" if lv == 0 else "Gender_Male")
        if var == "Age":
            return [own("Age_Baby"), 0.45 * own("Age_Baby"), None, own("Age_Old")][lv]
        if var == "Muscle":
            return {0: 0.5 * own("BodyType_Lean"), 2: own("BodyType_Muscular")}.get(lv)
        if var == "Weight":
            return {0: own("BodyType_Lean") + 0.4 * own("BodyType_Emaciated"), 2: own("BodyType_Fat")}.get(lv)
        if var == "Height":
            return scaled(-HEIGHT if lv == 0 else HEIGHT)
        if var == "Proportions":
            return {0: -0.3 * own("BodyType_LongProportions"), 2: 0.8 * own("BodyType_LongProportions")}.get(lv)
        if var == "Cup":
            return {0: own("Chest_FemaleFlatChested"), 2: 1.4 * own("Chest_Breast_Size")}.get(lv)
        if var in ANCESTRY:
            return own({"African": "Race_African", "Asian": "Race_EastAsian", "Caucasian": "Race_White"}[var])
        return None  # firmness: no morph of Vitruvian's

    def own_target(name):
        """Each family adds its own variable's levels (the weights of the others sum to 1, so each variable
        counts once): ancestry the race, height, proportions, breast the cup, the rest the body."""
        levels = [LEVELS[w] for w in macro_words(name)]
        have = {v for v, _ in levels}
        for family in (ANCESTRY, ("Height",), ("Proportions",), ("Cup", "Firmness")):
            if have & set(family):
                use = [(v, lv) for v, lv in levels if v in family]
                break
        else:
            use = [(v, lv) for v, lv in levels if v in ("Gender", "Age", "Muscle", "Weight")]
        out = np.zeros((nv + no, 3))
        for v, lv in use:
            part = level(v, lv)
            if part is not None:
                out += part
        return out

    ranges = {m["morph"]: (m.get("min", 0), m.get("max", 1)) for m in json.load(open(os.path.join(l2, "morphs.json")))}

    def detail(name):
        """A body slider's target from Vitruvian's own morphs, or None where MakeHuman's stays."""
        if name.startswith("bodyshapes/bodyshapes-elvs-"):
            terms = SHAPES.get(name.split("-", 3)[-1], [])  # "fem-full-hourglass" -> "full-hourglass"
            sign = 1.0
        else:
            sign = 1.0 if name.endswith("-incr") else -1.0 if name.endswith("-decr") else 0.0
            terms = DETAIL.get(name[:-5] if sign else name)
            if terms is None:
                return None
        out = np.zeros((nv + no, 3))
        for morph, k in terms:
            if sign >= 0 or ranges[morph][0] < 0:
                out += (sign or 1.0) * k * own(morph)
        return out

    Dnew = np.zeros((nv + no, 3))
    for name, w in macro_weights(P["targets"], DEFAULT).items():
        Dnew += w * own_target(name)
    log(f"Vitruvian's own morphs at the default sliders move it {np.linalg.norm(Dnew[:nv], axis=1).mean() * 100:.1f} mm on average")
    rest = want - Dnew

    # Faces: the skin first (Vitruvian's triangles as quads, atlas UVs), then the helpers as they were.
    skin_tri = tmp["tri"][skin]
    skin_uv = atlas_uv(tmp["uv"][skin])
    ntri = len(skin_tri)
    uv_all = np.concatenate([skin_uv.reshape(-1, 2), P["uv"]])
    uv_off = ntri * 3
    faces, groups_meta = [], []
    for gi, g in enumerate(meta["groups"]):
        first = len(faces)
        if gi == body_g:
            for t in range(ntri):
                a, b, c = vit_new[skin_tri[t]]
                faces.append(((a, b, c, c), (3 * t, 3 * t + 1, 3 * t + 2, 3 * t + 2), gi))
        else:
            for f in range(g["firstFace"], g["firstFace"] + g["faceCount"]):
                faces.append(([new_of_mh[int(x)] for x in P["quads"][f]], [uv_off + int(x) for x in P["quvs"][f]], gi))
        groups_meta.append({"name": g["name"], "firstFace": first, "faceCount": len(faces) - first})

    # Joints: helpers in MakeHuman's mesh, except a few made of skin vertices (the neck ring, the face
    # muscles): the Vitruvian skin vertex beside each.
    near_tree = cKDTree(M[vused] * 10)
    joints = {}
    for name, ids in meta["skeleton"]["joints"].items():
        joints[name] = [new_of_mh[int(i)] if int(i) in new_of_mh else int(near_tree.query(shaped[int(i)])[1]) for i in ids]

    # Skin weights: barycentric over the same triangles, the weakest dropped.
    bones = list(P["weights"])
    r_, c_, v_ = [], [], []
    for bi, b in enumerate(bones):
        vi, wt = P["weights"][b]
        r_.append(vi.astype(int))
        c_.append(np.full(len(vi), bi))
        v_.append(wt)
    W = sp.csr_matrix((np.concatenate(v_), (np.concatenate(r_), np.concatenate(c_))), shape=(nmh, len(bones)))
    Wn = (T @ W).tocsr()
    wr, wc, wv = [], [], []
    for i in range(Wn.shape[0]):
        lo, hi = Wn.indptr[i], Wn.indptr[i + 1]
        c, w = Wn.indices[lo:hi], Wn.data[lo:hi]
        keep = w >= 0.02
        if keep.any():
            c, w = c[keep], w[keep]
            w = w / w.sum()
            wr += [i] * len(c)
            wc += c.tolist()
            wv += w.tolist()
    wr, wc, wv = np.array(wr), np.array(wc), np.array(wv, "f4")

    # Targets: each carried through T. The body-type (macro) ones are smooth and big: 0.4 mm steps compress
    # far better and show nothing; the detail ones keep 0.1 mm.
    table, steps, xs, ys, zs = [], [], [], [], []
    dense = np.zeros((nmh, 3))
    for name, (idx, off) in P["targets"].items():
        dense[:] = 0
        dense[idx] = off
        rnd = 4 if macro_words(name) else 1
        own_detail = None if rnd > 1 else detail(name)
        if own_detail is not None:
            rnd, carried = 2, own_detail  # a body slider Vitruvian has a morph for
        else:
            carried = own_target(name) if rnd > 1 else T @ dense  # a body-type target: Vitruvian's own
        q = np.clip(np.rint(carried / (STEP * rnd)).astype(np.int64) * rnd, -32768, 32767)
        nz = np.nonzero((q != 0).any(1))[0]
        prev = 0
        for i in nz:
            steps.append(int(i) - prev)
            prev = int(i)
        xs += q[nz, 0].tolist()
        ys += q[nz, 1].tolist()
        zs += q[nz, 2].tolist()
        table.append((name, len(nz)))
    assert max(steps) < 65536
    log(f"{len(table)} targets, {len(steps)} offsets")

    meta = dict(meta)
    meta["groups"] = groups_meta
    meta["vertexCount"] = nv + no
    meta["skeleton"] = dict(meta["skeleton"], joints=joints)
    meta["licence"] = ("CC0-1.0 (MakeHuman assets, released September 2020; CharMorph Vitruvian by Sean Buckley and "
                       "Olaf Delgado-Friedrichs)")
    meta["source"] = dict(meta.get("source", {}), body="CharMorph-Vitruvian (CC0), skin mesh; targets, weights and skeleton carried over from MakeHuman")
    meta["targetCount"] = len(table)

    def blob(fmt, values):
        w = ch.Writer()
        w.array(fmt, values)
        return bytes(w.buf)

    vpos = blob("f", [float(x) for x in rest.reshape(-1)])
    vtuv = blob("f", [float(x) for x in uv_all.reshape(-1)])
    w = ch.Writer()
    w.u32(len(faces))
    w.array("I", [int(x) for f in faces for x in f[0]])
    w.array("I", [int(x) for f in faces for x in f[1]])
    w.array("H", [int(f[2]) for f in faces])
    face = bytes(w.buf)
    w = ch.Writer()
    w.u32(len(table))
    for name, n in table:
        w.string(name)
        w.align()
        w.u32(n)
    w.align()
    raw = bytes(w.buf) + ch.planar("H", steps) + ch.planar("h", xs) + ch.planar("h", ys) + ch.planar("h", zs)
    tgtz = struct.pack("<I", len(raw)) + zlib.compress(raw, 9)
    w = ch.Writer()
    w.u32(len(bones))
    order = np.lexsort((wr, wc))
    for bi, b in enumerate(bones):
        sel = order[wc[order] == bi]
        w.string(b)
        w.align()
        w.u32(len(sel))
        w.array("H", [int(x) for x in wr[sel]])
        w.array("f", [float(x) for x in wv[sel]])
    meta_bytes = json.dumps(meta, separators=(",", ":"), sort_keys=True).encode("utf-8")
    chunks = [ch.chunk(b"META", meta_bytes), ch.chunk(b"VPOS", vpos), ch.chunk(b"VTUV", vtuv), ch.chunk(b"FACE", face),
              ch.chunk(b"TGTZ", tgtz), ch.chunk(b"WGHT", bytes(w.buf))]
    out = bytearray(ch.MAGIC)
    out += struct.pack("<II", ch.VERSION, len(chunks))
    for c in chunks:
        out += c
    return bytes(out), skin_uv.reshape(-1, 3, 2)


# ---- the skin maps ------------------------------------------------------------


def build_maps(vit_dir, uv_tris, tile, out_dir, log):
    size = tile * 2

    def atlas(tiles):
        tiles = [t if t.ndim == 3 else t[..., None] for t in tiles]
        out = np.zeros((size, size, tiles[0].shape[2]), np.float32)
        for k, t in enumerate(tiles):
            r, c = k // 2, k % 2
            out[r * tile:(r + 1) * tile, c * tile:(c + 1) * tile] = t
        return out

    owner, _ = bv.rasterise(size, uv_tris)
    filled = owner >= 0
    del owner
    colour = atlas(bv.read_tiles(vit_dir, "Light_Skin_Color", tile))[..., :3]
    tone = np.median(colour[filled], 0)
    ratio = colour / tone * 0.9
    ratio = np.where(ratio < 0.8, ratio, 0.8 + 0.2 * (1 - np.exp(-(ratio - 0.8) / 0.2)))
    lum = ratio.mean(2, keepdims=True)
    ratio = lum + (ratio - lum) * 0.6  # its colour shifts read as bruises over a warm tint
    ratio = ratio - (lum - 0.9) / 3
    albedo = bv.dilate(np.clip(ratio, 0, 1).astype(np.float32), filled, 12)
    Image.fromarray((bv.srgb(albedo) * 255 + 0.5).astype(np.uint8)).save(
        os.path.join(out_dir, "vitruvian_skin_albedo.jpg"), quality=95, subsampling=2)
    log(f"albedo, skin tone {np.round(tone, 3)}")
    del albedo, ratio, colour

    height = atlas(bv.read_tiles(vit_dir, "Skin_Height", tile))[..., 0]
    h = bv.dilate(np.where(filled, height, 0).astype(np.float32)[..., None], filled, 12)[..., 0]
    detail = h - bv.blur(h, max(2, tile // 128))
    pore = bv.pores(size, np.random.default_rng(bv.SEED)) - 0.5
    total = detail * 6.0 * (tile / 1024.0) * 0.75 + pore * 0.19
    hx = np.roll(total, -1, 1) - np.roll(total, 1, 1)
    hy = np.roll(total, -1, 0) - np.roll(total, 1, 0)
    n = np.stack([-hx, hy, np.ones_like(hx)], 2)
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    Image.fromarray(((n * 0.5 + 0.5) * 255 + 0.5).astype(np.uint8)).save(
        os.path.join(out_dir, "vitruvian_skin_normal.jpg"), quality=92, subsampling=0)
    log("normal")
    del n, total, height, h

    rough = atlas(bv.read_tiles(vit_dir, "Skin_Roughness", tile))[..., 0]
    r = np.clip(0.6 + (rough - np.median(rough[filled])) * 0.8, 0.2, 1.0)
    r = bv.dilate(np.where(filled, r, 0.6).astype(np.float32)[..., None], filled, 12)[..., 0]
    # Roughness changes slowly: half the size is enough.
    Image.fromarray((r * 255 + 0.5).astype(np.uint8), "L").resize((tile, tile), Image.LANCZOS).save(
        os.path.join(out_dir, "vitruvian_skin_roughness.jpg"), quality=90)
    log("roughness")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("makehuman_pack", help="assets/humans/makehuman.kkhpack")
    ap.add_argument("vitruvian_dir", help="CharMorph-Vitruvian (joints/ and textures/4K/)")
    ap.add_argument("vitruvian_npz", help="vitruvian_export.py's output")
    ap.add_argument("out_dir")
    ap.add_argument("--no-maps", action="store_true", help="only the pack (the skin maps take a minute)")
    ap.add_argument("--tile", type=int, default=2048, help="texels per UDIM tile in the atlas (the atlas is twice that)")
    args = ap.parse_args()
    os.makedirs(args.out_dir, exist_ok=True)
    t0 = time.time()

    def log(*a):
        print(f"[{time.time() - t0:4.0f}s]", *a, flush=True)

    pack, uv_tris = build_pack(read_pack(args.makehuman_pack), args.vitruvian_npz, args.vitruvian_dir, log)
    with open(os.path.join(args.out_dir, "vitruvian.kkhpack"), "wb") as f:
        f.write(pack)
    log(f"vitruvian.kkhpack {len(pack) / 1e6:.1f} MB")
    if not args.no_maps:
        build_maps(args.vitruvian_dir, uv_tris, args.tile, args.out_dir, log)


if __name__ == "__main__":
    main()
