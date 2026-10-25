"""Exports CharMorph's Vitruvian body (CC0) for bake_vitruvian.py.

Run with Blender's Python (the `bpy` wheel from PyPI works):

    python -I tools/makehuman/vitruvian_export.py CharMorph-Vitruvian/char.blend vitruvian.npz

Writes the mesh as it is in the file (Blender's Z-up, facing -Y): vertex
positions, triangles, each triangle's corners in the UDIM UV layout the
skin textures use, and each triangle's material (4 = the skin, 5 = the skin the censor material covers).
"""

import sys

import bpy
import numpy as np


def main():
    blend, out = sys.argv[1], sys.argv[2]
    bpy.ops.wm.open_mainfile(filepath=blend)
    obj = bpy.data.objects["cm_vitruvian"]
    for mod in obj.modifiers:
        mod.show_viewport = False  # the brows and lashes are particle systems
    # The file's own mesh reads back with empty UV layers; the evaluated copy has them.
    mesh = obj.evaluated_get(bpy.context.evaluated_depsgraph_get()).to_mesh()
    mesh.calc_loop_triangles()
    count = len(mesh.loop_triangles)
    co = np.zeros(len(mesh.vertices) * 3, "f4")
    mesh.vertices.foreach_get("co", co)
    tri = np.zeros(count * 3, "i4")
    mesh.loop_triangles.foreach_get("vertices", tri)
    loops = np.zeros(count * 3, "i4")
    mesh.loop_triangles.foreach_get("loops", loops)
    mat = np.zeros(count, "i4")
    mesh.loop_triangles.foreach_get("material_index", mat)
    layer = mesh.uv_layers["VitruvianUV.UDIM"]
    uv = np.zeros(len(mesh.loops) * 2, "f4")
    layer.uv.foreach_get("vector", uv)
    uv = uv.reshape(-1, 2)
    np.savez_compressed(out, co=co.reshape(-1, 3), tri=tri.reshape(-1, 3),
                        uv=uv[loops.reshape(-1, 3)], mat=mat,
                        materials=np.array([m.name if m else "" for m in mesh.materials]))
    print(f"{out}: {len(mesh.vertices)} vertices, {count} triangles")


if __name__ == "__main__":
    main()
