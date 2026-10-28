#!/usr/bin/env python3
"""Writes a small skinned glTF binary with morph targets, for checking blend shapes
(docs/FACIAL_ANIMATION.md) without a real character:

    python3 tools/morph_test_model.py /tmp/morph_head.glb
    python3 tools/morph_test_model.py /tmp/dense.glb --rings 64 --segs 96            # a denser sphere, for levels of detail
    python3 tools/morph_test_model.py /tmp/dense_static.glb --rings 64 --segs 96 --static   # not skinned, no morph targets

A sphere on one bone, with the targets "puff" (grows along the normals), "stretch" (wider)
and "squash" (flatter), named the way a face's targets are (glTF extras.targetNames), plus
"jawOpen" and "blink" so kke::FaceRig finds the shapes it uses.
"""
import json
import math
import struct
import sys

args = sys.argv[1:]
def option(name, default):
    return int(args[args.index(name) + 1]) if name in args else default
STATIC = "--static" in args
RINGS, SEGS, R = option("--rings", 12), option("--segs", 20), 0.5
OUT = args[0] if args and not args[0].startswith("--") else "morph_head.glb"

pos, nrm, uv, idx = [], [], [], []
for i in range(RINGS + 1):
    phi = math.pi * i / RINGS
    for j in range(SEGS + 1):
        th = 2 * math.pi * j / SEGS
        n = (math.sin(phi) * math.cos(th), math.cos(phi), math.sin(phi) * math.sin(th))
        pos.append(tuple(R * c for c in n))
        nrm.append(n)
        uv.append((j / SEGS, i / RINGS))
for i in range(RINGS):
    for j in range(SEGS):
        a, b = i * (SEGS + 1) + j, (i + 1) * (SEGS + 1) + j
        idx += [a, a + 1, b, a + 1, b + 1, b]

targets = {
    "puff": [tuple(0.25 * c for c in n) for n in nrm],
    "stretch": [(0.8 * p[0], 0, 0) for p in pos],
    "squash": [(0, -0.5 * p[1], 0) for p in pos],
    # a mouth-ish shape: the lower front drops; a lid-ish shape: the upper front comes down
    "jawOpen": [(0, -0.3 * max(0.0, -p[1]) * max(0.0, p[2]), 0) for p in pos],
    "blink": [(0, -0.25 * max(0.0, p[1]) * max(0.0, p[2]), 0) for p in pos],
}

chunks = []
def add(fmt, rows):
    data = b"".join(struct.pack(fmt, *r) if isinstance(r, tuple) else struct.pack(fmt, r) for r in rows)
    chunks.append(data + b"\0" * (-len(data) % 4))
    return len(chunks) - 1

views, accessors = [], []
def accessor(buf, ctype, count, typ, **extra):
    views.append({"buffer": 0, "byteOffset": sum(len(c) for c in chunks[:buf]), "byteLength": len(chunks[buf])})
    accessors.append({"bufferView": len(views) - 1, "componentType": ctype, "count": count, "type": typ, **extra})
    return len(accessors) - 1

n = len(pos)
a_pos = accessor(add("<3f", pos), 5126, n, "VEC3", min=[-R, -R, -R], max=[R, R, R])
a_nrm = accessor(add("<3f", nrm), 5126, n, "VEC3")
a_uv = accessor(add("<2f", uv), 5126, n, "VEC2")
if not STATIC:
    a_joints = accessor(add("<4B", [(0, 0, 0, 0)] * n), 5121, n, "VEC4")
    a_weights = accessor(add("<4f", [(1, 0, 0, 0)] * n), 5126, n, "VEC4")
a_idx = accessor(add("<I", idx), 5125, len(idx), "SCALAR")
ident = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
if not STATIC:
    a_ibm = accessor(add("<16f", [ident]), 5126, 1, "MAT4")
names = [] if STATIC else list(targets)
a_t = {}
for name in names:
    d = targets[name]
    a_t[name] = accessor(add("<3f", d), 5126, n, "VEC3", min=[min(v[k] for v in d) for k in range(3)], max=[max(v[k] for v in d) for k in range(3)])

blob = b"".join(chunks)
attributes = {"POSITION": a_pos, "NORMAL": a_nrm, "TEXCOORD_0": a_uv}
if not STATIC:
    attributes.update({"JOINTS_0": a_joints, "WEIGHTS_0": a_weights})
primitive = {"attributes": attributes, "indices": a_idx, "material": 0}
mesh = {"name": "head", "primitives": [primitive]}
if not STATIC:
    primitive["targets"] = [{"POSITION": a_t[k]} for k in names]
    mesh["extras"] = {"targetNames": names}
    mesh["weights"] = [0] * len(names)
gltf = {
    "asset": {"version": "2.0", "generator": "kke tools/morph_test_model.py"},
    "scene": 0,
    "scenes": [{"nodes": [0] if STATIC else [0, 1]}],
    "nodes": [{"name": "head", "mesh": 0}] if STATIC else [{"name": "head", "mesh": 0, "skin": 0}, {"name": "root"}],
    "materials": [{"name": "skin", "pbrMetallicRoughness": {"baseColorFactor": [0.9, 0.55, 0.4, 1], "metallicFactor": 0, "roughnessFactor": 0.7}}],
    "meshes": [mesh],
    "buffers": [{"byteLength": len(blob)}],
    "bufferViews": views,
    "accessors": accessors,
}
if not STATIC:
    gltf["skins"] = [{"joints": [1], "inverseBindMatrices": a_ibm, "skeleton": 1}]
js = json.dumps(gltf, separators=(",", ":")).encode()
js += b" " * (-len(js) % 4)
out = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(blob))
out += struct.pack("<II", len(js), 0x4E4F534A) + js + struct.pack("<II", len(blob), 0x004E4942) + blob
open(OUT, "wb").write(out)
print("wrote", OUT, len(out), "bytes,", n, "vertices,", len(idx) // 3, "triangles,", len(names), "morph targets")
