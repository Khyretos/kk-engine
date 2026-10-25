#!/usr/bin/env python3
"""Checks cook_human.py on a tiny made-up MakeHuman data folder.

    python3 tools/makehuman/test_cook_human.py

The pack it writes is read back here byte by byte, the way kke::HumanPack
reads it, so the two can't drift apart unnoticed.
"""
import json
import os
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cook_human  # noqa: E402


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def make_data(root):
    """A 2-quad mesh, three targets, one modifier, two bones."""
    write(os.path.join(root, "3dobjs", "base.obj"),
          "# comment\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 2 0 0\nv 2 1 0\n"
          "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
          "g body\nf 1/1 2/2 3/3 4/4\ng helper-hair\nf 2/1 5/2 6/3\n")
    write(os.path.join(root, "targets", "nose", "nose-hump-decr.target"), "# CC0\n0 0.001 0 0\n3 0 0 0\n")
    write(os.path.join(root, "targets", "nose", "nose-hump-incr.target"), "5 0 -1.5 0.25\n1 0.5 0 0\n")
    write(os.path.join(root, "targets", "macrodetails", "universal-female-young-averagemuscle-averageweight.target"),
          "2 0 0.1 0\n")
    write(os.path.join(root, "modifiers", "modeling_modifiers.json"), json.dumps([
        {"group": "nose", "modifiers": [{"target": "nose-hump", "min": "decr", "max": "incr"}]},
        {"group": "macrodetails", "modifiers": [{"macrovar": "Age"}]},
    ]))
    write(os.path.join(root, "modifiers", "modeling_modifiers_desc.json"), json.dumps({"nose/nose-hump-decr|incr": "Hump"}))
    write(os.path.join(root, "rigs", "default.mhskel"), json.dumps({
        "name": "test", "weights_file": "default_weights.mhw",
        "bones": {"spine": {"head": "root____tail", "tail": "spine____tail", "parent": "root"},
                  "root": {"head": "root____head", "tail": "root____tail", "parent": None, "rotation_plane": "root____plane"}},
        "joints": {"root____head": [0, 1], "root____tail": [2], "spine____tail": [3]},
        "planes": {"root____plane": ["root____head", "root____tail", "spine____tail"]},
    }))
    write(os.path.join(root, "rigs", "default_weights.mhw"), json.dumps({"weights": {"root": [[0, 1.0], [1, 0.5]], "spine": [[1, 0.5]]}}))


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def align(self):
        self.pos = (self.pos + 3) & ~3

    def take(self, fmt):
        v = struct.unpack_from("<" + fmt, self.data, self.pos)
        self.pos += struct.calcsize("<" + fmt)
        return v

    def string(self):
        self.align()
        (n,) = self.take("H")
        s = self.data[self.pos:self.pos + n].decode("utf-8")
        self.pos += n
        return s

    def planar(self, fmt, n):
        lo = self.data[self.pos:self.pos + n]
        hi = self.data[self.pos + n:self.pos + 2 * n]
        self.pos += 2 * n
        return list(struct.unpack(f"<{n}{fmt}", bytes(b for pair in zip(lo, hi) for b in pair)))


def chunks(data):
    assert data[:8] == cook_human.MAGIC
    version, count = struct.unpack_from("<II", data, 8)
    assert version == cook_human.VERSION
    pos, out = 16, {}
    for _ in range(count):
        pos = (pos + 3) & ~3
        tag = data[pos:pos + 4].decode()
        (n,) = struct.unpack_from("<I", data, pos + 4)
        out[tag] = data[pos + 8:pos + 8 + n]
        pos += 8 + n
    return out


class CookHuman(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        make_data(self.tmp.name)
        self.pack = chunks(cook_human.cook(self.tmp.name, {"commit": "abc"}, log=lambda *_: None))

    def tearDown(self):
        self.tmp.cleanup()

    def test_mesh(self):
        self.assertEqual(len(self.pack["VPOS"]), 6 * 12)
        r = Reader(self.pack["FACE"])
        (n,) = r.take("I")
        self.assertEqual(n, 2)
        verts = r.take("8I")
        self.assertEqual(verts, (0, 1, 2, 3, 1, 4, 5, 5))  # the triangle repeats its last corner
        uvs = r.take("8I")
        self.assertEqual(uvs[:4], (0, 1, 2, 3))
        r.align()
        self.assertEqual(r.take("2H"), (0, 1))

    def test_targets_are_sparse_sorted_and_exact(self):
        (raw_len,) = struct.unpack_from("<I", self.pack["TGTZ"])
        raw = zlib.decompress(self.pack["TGTZ"][4:])
        self.assertEqual(len(raw), raw_len)
        r = Reader(raw)
        (count,) = r.take("I")
        table = []
        for _ in range(count):
            name = r.string()
            r.align()
            table.append((name, r.take("I")[0]))
        r.align()
        total = sum(n for _, n in table)
        steps, xs, ys, zs = r.planar("H", total), r.planar("h", total), r.planar("h", total), r.planar("h", total)
        got, e = {}, 0
        for name, n in table:
            vert, entries = 0, []
            for _ in range(n):
                vert += steps[e]
                entries.append((vert, xs[e], ys[e], zs[e]))
                e += 1
            got[name] = entries
        self.assertEqual(got["nose/nose-hump-decr"], [(0, 1, 0, 0)])  # the all-zero line is left out
        self.assertEqual(got["nose/nose-hump-incr"], [(1, 500, 0, 0), (5, 0, -1500, 250)])  # sorted by vertex
        self.assertIn("macrodetails/universal-female-young-averagemuscle-averageweight", got)

    def test_meta(self):
        meta = json.loads(self.pack["META"])
        self.assertEqual(meta["vertexCount"], 6)
        self.assertEqual(meta["source"], {"commit": "abc"})
        self.assertTrue(meta["licence"].startswith("CC0"))
        self.assertEqual([g["name"] for g in meta["groups"]], ["body", "helper-hair"])
        nose = [m for m in meta["modifiers"] if m["group"] == "nose"][0]
        self.assertEqual(nose["name"], "nose/nose-hump-decr|incr")
        self.assertEqual(nose["targets"], ["nose/nose-hump-decr", "nose/nose-hump-incr"])
        self.assertEqual((nose["min"], nose["max"]), (-1.0, 1.0))
        self.assertIn({"group": "macrodetails", "name": "macrodetails/Age", "macro": "Age"}, meta["modifiers"])
        self.assertEqual([b["name"] for b in meta["skeleton"]["bones"]], ["root", "spine"])  # parents first
        self.assertEqual(meta["descriptions"]["nose/nose-hump-decr|incr"], "Hump")

    def test_weights(self):
        r = Reader(self.pack["WGHT"])
        (bones,) = r.take("I")
        self.assertEqual(bones, 2)
        self.assertEqual(r.string(), "root")
        r.align()
        (n,) = r.take("I")
        r.align()
        self.assertEqual(r.take(f"{n}H"), (0, 1))
        r.align()
        self.assertEqual(r.take(f"{n}f"), (1.0, 0.5))

    def test_missing_target_is_an_error(self):
        os.remove(os.path.join(self.tmp.name, "targets", "nose", "nose-hump-incr.target"))
        with self.assertRaises(ValueError):
            cook_human.cook(self.tmp.name, log=lambda *_: None)

    def test_offset_finer_than_the_step_is_an_error(self):
        write(os.path.join(self.tmp.name, "targets", "nose", "nose-hump-incr.target"), "1 0.0004 0 0\n")
        with self.assertRaises(ValueError):
            cook_human.cook(self.tmp.name, log=lambda *_: None)

    def test_credits_name_the_authors(self):
        c = cook_human.credits({"commit": "abc"})
        self.assertEqual(c["assets"][0]["licence"], "CC0-1.0")
        self.assertIn("MakeHuman", c["assets"][0]["authors"])


if __name__ == "__main__":
    unittest.main()
