#!/usr/bin/env python3
"""Cooks MakeHuman's CC0 human into one kk-engine pack (docs/HUMANS.md).

    tools/makehuman/cook_human.py MAKEHUMAN_DATA OUT.kkhpack [--credits OUT.json]

MAKEHUMAN_DATA is the `makehuman/data` folder of a MakeHuman checkout at
the commit in tools/makehuman/makehuman.lock. Only CC0 data files are
read (the base mesh, the morph targets, the modifier lists, the skeleton
and its weights); none of MakeHuman's AGPL program code is used or
copied, so the pack carries no AGPL terms.

The pack (format below, read by kke::HumanPack) holds:

- the base mesh: every vertex, the UVs, the quads and which group
  (body, helper-hair, joint-*, ...) each quad belongs to;
- every morph target as sparse, quantised offsets (unchanged vertices
  are left out);
- the modifier lists and the slider layout (which tab, label, camera);
- the skeleton (bones, the vertices whose centre is each joint, the
  roll planes) and the skin weights.

Format, little-endian, every chunk padded to 4 bytes:

    "KKHPACK1"  u32 version  u32 chunkCount
    chunk = tag[4] u32 byteLength payload
    META  UTF-8 JSON (source, licence, groups, modifiers, sliders, skeleton)
    VPOS  f32 x y z per vertex
    VTUV  f32 u v per UV
    FACE  u32 count, u32 vert[4*count], u32 uv[4*count], u16 group[count]
    TGTZ  u32 rawLength, then zlib (RFC 1950) of:
            u32 count, per target: str name, u32 n          (the table)
            u16 vertStep[total], i16 x[total], y[total], z[total]
          where total = the sum of the n's, the targets' entries follow
          one another in table order, vertStep is the vertex number minus
          the previous entry's (0 at each target's start), and x y z are
          the offset in steps of 0.001 MakeHuman units (decimetres, so
          0.1 mm: the precision of the .target files). Each u16/i16 array
          is stored as its low bytes, then its high bytes, which packs
          about 6 times smaller.
    WGHT  u32 bones, per bone: str name, u32 n, u16 vert[n], f32 weight[n]

where str = u16 length + UTF-8 bytes, and every array starts on 4 bytes.
"""
import argparse
import json
import math
import os
import struct
import sys
import zlib

VERSION = 1
MAGIC = b"KKHPACK1"
MODIFIER_FILES = ("modeling", "measurement", "bodyshapes")


def strip_comment(line):
    i = line.find("#")
    return (line if i < 0 else line[:i]).strip()


def read_obj(path):
    """The base mesh: positions, UVs, quads (vertex and UV indices) and groups."""
    positions, uvs, faces, groups = [], [], [], []
    group = -1
    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = strip_comment(raw)
            if not line:
                continue
            parts = line.split()
            kind = parts[0]
            if kind == "v":
                positions.append(tuple(float(x) for x in parts[1:4]))
            elif kind == "vt":
                uvs.append((float(parts[1]), float(parts[2])))
            elif kind == "g":
                groups.append(parts[1] if len(parts) > 1 else "")
                group = len(groups) - 1
            elif kind == "f":
                corners = parts[1:]
                if len(corners) == 3:
                    corners.append(corners[2])  # a triangle as a quad with its last corner twice
                if len(corners) != 4:
                    raise ValueError(f"{path}: face with {len(corners)} corners")
                vs, ts = [], []
                for c in corners:
                    bits = c.split("/")
                    vs.append(int(bits[0]) - 1)
                    ts.append(int(bits[1]) - 1 if len(bits) > 1 and bits[1] else 0)
                if group < 0:
                    groups.append("")
                    group = 0
                faces.append((vs, ts, group))
    return positions, uvs, faces, groups


def read_target(path):
    """[(vertex, dx, dy, dz)] without the vertices a target doesn't move."""
    out = []
    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = strip_comment(raw)
            if not line:
                continue
            parts = line.split()
            d = (float(parts[1]), float(parts[2]), float(parts[3]))
            if d != (0.0, 0.0, 0.0):
                out.append((int(parts[0]), *d))
    out.sort()
    for a, b in zip(out, out[1:]):
        if a[0] == b[0]:
            raise ValueError(f"{path}: vertex {a[0]} listed twice")
    return out


def find_targets(targets_dir):
    """Every .target under targets/, by its name: path without .target, '/' separated."""
    found = {}
    for root, dirs, files in os.walk(targets_dir):
        dirs.sort()
        for name in sorted(files):
            if name.endswith(".target"):
                full = os.path.join(root, name)
                rel = os.path.relpath(full, targets_dir)[: -len(".target")]
                found[rel.replace(os.sep, "/")] = full
    return found


STEP = 0.001  # the .target files' precision, in MakeHuman units


def quantise(value, name):
    q = round(value / STEP)
    if not -32768 <= q <= 32767 or abs(q * STEP - value) > 1e-6:
        raise ValueError(f"{name}: offset {value} is not a whole number of {STEP} steps within 16 bits")
    return q


def planar(fmt, values):
    """A 16-bit array as its low bytes, then its high bytes."""
    b = struct.pack(f"<{len(values)}{fmt}", *values)
    return b[0::2] + b[1::2]


class Writer:
    def __init__(self):
        self.buf = bytearray()

    def u16(self, v):
        self.buf += struct.pack("<H", v)

    def u32(self, v):
        self.buf += struct.pack("<I", v)

    def f32(self, v):
        self.buf += struct.pack("<f", v)

    def array(self, fmt, values):
        self.align()
        self.buf += struct.pack(f"<{len(values)}{fmt}", *values)

    def string(self, s):
        b = s.encode("utf-8")
        self.align()
        self.u16(len(b))
        self.buf += b

    def align(self):
        while len(self.buf) % 4:
            self.buf.append(0)


def chunk(tag, payload):
    w = Writer()
    w.buf += tag
    w.u32(len(payload))
    w.buf += payload
    w.align()
    return bytes(w.buf)


def load_json(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def modifier_list(data_dir):
    """Every modifier as {group, name, targets, min, max, default} or {macro}.

    name is MakeHuman's own ("nose/nose-hump-decr|incr"), so saves and .mhm
    imports match. A two-sided modifier runs -1..1: below 0 its first
    target, above 0 its second. A one-sided one runs 0..1.
    """
    out = []
    for kind in MODIFIER_FILES:
        path = os.path.join(data_dir, "modifiers", f"{kind}_modifiers.json")
        if not os.path.exists(path):
            continue
        for group in load_json(path):
            gname = group["group"]
            for m in group["modifiers"]:
                if "macrovar" in m:
                    out.append({"group": gname, "name": f"{gname}/{m['macrovar']}", "macro": m["macrovar"]})
                elif "min" in m and "max" in m:
                    base = m["target"]
                    out.append({
                        "group": gname,
                        "name": f"{gname}/{base}-{m['min']}|{m['max']}",
                        "targets": [f"{gname}/{base}-{m['min']}", f"{gname}/{base}-{m['max']}"],
                        "min": -1.0, "max": 1.0, "default": 0.0,
                    })
                else:
                    base = m["target"]
                    out.append({
                        "group": gname, "name": f"{gname}/{base}",
                        "targets": [f"{gname}/{base}"],
                        "min": 0.0, "max": 1.0, "default": 0.0,
                    })
    return out


def parents_first(bones):
    """The bones in file order, except that every parent comes before its children."""
    out, placed = [], set()

    def place(name, path=()):
        if name in placed:
            return
        if name in path:
            raise ValueError(f"bone {name} is its own ancestor")
        parent = bones[name]["parent"]
        if parent is not None:
            if parent not in bones:
                raise ValueError(f"bone {name}'s parent {parent} is not in the skeleton")
            place(parent, path + (name,))
        placed.add(name)
        out.append((name, bones[name]))

    for name in bones:
        place(name)
    return out


def descriptions(data_dir):
    out = {}
    for kind in MODIFIER_FILES:
        path = os.path.join(data_dir, "modifiers", f"{kind}_modifiers_desc.json")
        if os.path.exists(path):
            out.update(load_json(path))
    return out


def sliders(data_dir):
    out = {}
    for kind in MODIFIER_FILES:
        path = os.path.join(data_dir, "modifiers", f"{kind}_sliders.json")
        if os.path.exists(path):
            out[kind] = load_json(path)
    return out


def cook(data_dir, source=None, log=print):
    positions, uvs, faces, groups = read_obj(os.path.join(data_dir, "3dobjs", "base.obj"))
    nverts = len(positions)
    if nverts > 0xFFFF:
        raise ValueError(f"{nverts} vertices: the pack stores vertex numbers in 16 bits")

    targets = find_targets(os.path.join(data_dir, "targets"))
    modifiers = modifier_list(data_dir)
    missing = sorted({t for m in modifiers for t in m.get("targets", []) if t not in targets})
    if missing:
        raise ValueError(f"{len(missing)} modifier targets have no .target file, first: {missing[0]}")

    skel = load_json(os.path.join(data_dir, "rigs", "default.mhskel"))
    weights = load_json(os.path.join(data_dir, "rigs", skel.get("weights_file", "default_weights.mhw")))["weights"]

    group_faces = {}
    for i, (_, _, g) in enumerate(faces):
        first, count = group_faces.get(g, (i, 0))
        group_faces[g] = (first, count + 1)
    meta = {
        "format": "kk-human-pack",
        "version": VERSION,
        "source": source or {},
        "licence": "CC0-1.0 (MakeHuman assets, released September 2020)",
        "vertexCount": nverts,
        "groups": [{"name": n, "firstFace": group_faces.get(i, (0, 0))[0], "faceCount": group_faces.get(i, (0, 0))[1]}
                   for i, n in enumerate(groups)],
        "modifiers": modifiers,
        "descriptions": descriptions(data_dir),
        "sliders": sliders(data_dir),
        "skeleton": {
            "name": skel.get("name", ""),
            "bones": [{"name": name, "parent": b["parent"], "head": b["head"], "tail": b["tail"],
                       "plane": b.get("rotation_plane"), "reference": b.get("reference")}
                      for name, b in parents_first(skel["bones"])],
            "joints": skel["joints"],
            "planes": skel.get("planes", {}),
        },
        "targetCount": len(targets),
    }

    w = Writer()
    w.array("f", [c for p in positions for c in p])
    vpos = bytes(w.buf)

    w = Writer()
    w.array("f", [c for t in uvs for c in t])
    vtuv = bytes(w.buf)

    w = Writer()
    w.u32(len(faces))
    w.array("I", [v for f in faces for v in f[0]])
    w.array("I", [t for f in faces for t in f[1]])
    w.array("H", [f[2] for f in faces])
    face = bytes(w.buf)

    w = Writer()
    w.u32(len(targets))
    steps, xs, ys, zs = [], [], [], []
    for name, path in targets.items():
        entries = read_target(path)
        prev = 0
        for e in entries:
            if e[0] >= nverts:
                raise ValueError(f"{name}: vertex {e[0]} past the mesh's {nverts}")
            steps.append(e[0] - prev)
            prev = e[0]
            xs.append(quantise(e[1], name))
            ys.append(quantise(e[2], name))
            zs.append(quantise(e[3], name))
        w.string(name)
        w.align()
        w.u32(len(entries))
    w.align()
    raw = bytes(w.buf) + planar("H", steps) + planar("h", xs) + planar("h", ys) + planar("h", zs)
    entries_total = len(steps)
    tgtz = struct.pack("<I", len(raw)) + zlib.compress(raw, 9)

    w = Writer()
    w.u32(len(weights))
    for bone, pairs in weights.items():
        w.string(bone)
        w.align()
        w.u32(len(pairs))
        w.array("H", [p[0] for p in pairs])
        w.array("f", [p[1] for p in pairs])
    wght = bytes(w.buf)

    meta_bytes = json.dumps(meta, separators=(",", ":"), sort_keys=True).encode("utf-8")
    chunks = [chunk(b"META", meta_bytes), chunk(b"VPOS", vpos), chunk(b"VTUV", vtuv), chunk(b"FACE", face),
              chunk(b"TGTZ", tgtz), chunk(b"WGHT", wght)]
    out = bytearray(MAGIC)
    out += struct.pack("<II", VERSION, len(chunks))
    for c in chunks:
        out += c
    log(f"cook_human: {nverts} vertices, {len(faces)} quads, {len(targets)} targets "
        f"({entries_total} offsets), {len(modifiers)} modifiers, {len(meta['skeleton']['bones'])} bones, "
        f"{len(out) / 1e6:.1f} MB")
    return bytes(out)


def credits(source):
    """The credits list a game shows: every asset with its author and licence."""
    return {
        "format": "kk-human-credits",
        "version": 1,
        "assets": [{
            "what": "MakeHuman base mesh, morph targets, skeleton and skin weights",
            "authors": "MakeHuman Team (Data Collection AB, Joel Palmius, Jonas Hauquier; original targets by Manuel Bastioni)",
            "licence": "CC0-1.0",
            "url": "http://www.makehumancommunity.org",
            "source": source or {},
        }],
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("data", help="makehuman/data folder of a MakeHuman checkout")
    ap.add_argument("out", help="the .kkhpack to write")
    ap.add_argument("--credits", help="also write the credits list (JSON) here")
    ap.add_argument("--commit", default="", help="the MakeHuman commit, recorded in the pack")
    args = ap.parse_args()
    source = {"repo": "https://github.com/makehumancommunity/makehuman", "commit": args.commit}
    try:
        data = cook(args.data, source)
    except (OSError, ValueError, KeyError) as e:
        sys.exit(f"cook_human: {e}")
    with open(args.out, "wb") as f:
        f.write(data)
    if args.credits:
        with open(args.credits, "w", encoding="utf-8") as f:
            json.dump(credits(source), f, indent=2)
            f.write("\n")


if __name__ == "__main__":
    main()
