"""Vitruvian (CC0) -> one clean mesh with a single skin atlas, morphs applied, previews + .glb.

Makes assets/bodies/vitruvian_voluptuous.glb (docs/JIGGLE.md). Needs Blender 4+ and a
checkout of https://github.com/Upliner/CharMorph-Vitruvian (git-lfs):

    VITRUVIAN_DIR=~/CharMorph-Vitruvian VITRUVIAN_OUT=out VOL_EXTRA=1.7 \\
        blender -b --python tools/vitruvian/build_body.py -- voluptuous export shots

VOL_EXTRA pushes Breast_Size, GluteSize and Hips_Width past the slider's end (1.7 for the
demo's body); VOL_SWIM is the bandeau and shorts colour. "shots" renders previews
(front, side, three-quarter, face) to VITRUVIAN_OUT; "export" writes the .glb there.
"""
import bpy, bmesh, math, os, sys
import numpy as np
from mathutils import Vector

ROOT = os.environ.get("VITRUVIAN_DIR", "CharMorph-Vitruvian") + "/"
OUT = os.environ.get("VITRUVIAN_OUT", "out") + "/"
os.makedirs(OUT, exist_ok=True)
args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
preset = args[0] if args else "voluptuous"
do_export = "export" in args
do_shots = "shots" in args

PRESETS = {
    "average": {"Gender_Female": 1.0},
    # A full, curvy woman: Vitruvian's own morphs, each inside its slider range unless the factor says more.
    "voluptuous": {"Gender_Female": 1.0, "BodyType_EndoMorph": 1.0, "Chest_Breast_Size": 1.0, "Waist_GluteSize": 1.0,
                   "Waist_Hips_Width": 1.0, "Waist_Width": -0.7, "Legs_Thigh_Size": 0.9, "Legs_Quad_Size": 0.4,
                   "Legs_Hamstring_Size": 0.5, "Legs_Calves_Size": 0.2},
}
EXTRA = float(os.environ.get("VOL_EXTRA", "1.0"))  # >1: push Breast_Size / GluteSize / Hips past the slider's end
weights = dict(PRESETS[preset])
if preset == "voluptuous":
    for k in ("Chest_Breast_Size", "Waist_GluteSize", "Waist_Hips_Width"):
        weights[k] *= EXTRA

# ---- read the mesh (it only exists in the file's Edit Mode data) -------------------------------------------
bpy.ops.wm.open_mainfile(filepath=ROOT + "char.blend")
src = bpy.data.objects["cm_vitruvian"]
bpy.context.view_layer.objects.active = src
bm = bmesh.from_edit_mesh(src.data)
bm.verts.ensure_lookup_table()
uvl = bm.loops.layers.uv["VitruvianUV.UDIM"]
faces, mats, loop_uv = [], [], []
for f in bm.faces:
    if f.material_index in (6, 7):  # eyelash slot (no faces) and the see-through tear line
        continue
    faces.append([v.index for v in f.verts])
    mats.append(f.material_index)
    loop_uv.append([(l[uvl].uv.x, l[uvl].uv.y) for l in f.loops])
base = np.load(ROOT + "morphs/L1/Default.npy").astype("f8")


def morph(name):
    for ext in (".npy", ".npz"):
        p = ROOT + "morphs/L2/" + name + ext
        if os.path.exists(p):
            d = np.load(p)
            if ext == ".npy":
                return d.astype("f8")
            full = np.zeros_like(base)
            full[d["idx"]] += d["delta"]
            return full
    raise FileNotFoundError(name)


pos = base.copy()
for m, w in weights.items():
    pos += w * morph(m)
print("WEIGHTS", weights, flush=True)

# ---- a clean scene -------------------------------------------------------------------------------------------
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.view_settings.view_transform = "Standard"


def srgb(a):
    a = np.clip(a, 0.0, 1.0)
    return np.where(a <= 0.0031308, a * 12.92, 1.055 * np.power(a, 1 / 2.4) - 0.055)


def exr(name, tile, size):
    img = bpy.data.images.load(ROOT + "textures/4K/%s.%d.exr" % (name, tile))
    img.scale(size, size)
    px = np.array(img.pixels[:], dtype="f4").reshape(size, size, 4)
    bpy.data.images.remove(img)
    return px


def byte_image(name, px, size_xy):
    img = bpy.data.images.new(name, size_xy[0], size_xy[1], alpha=False)
    img.colorspace_settings.name = "sRGB"
    rgba = np.ones((size_xy[1], size_xy[0], 4), "f4")
    rgba[..., :3] = srgb(px[..., :3])
    img.pixels.foreach_set(rgba.ravel())
    img.pack()
    return img


SK = 1024  # per skin tile: 2 x 2 tiles = a 2048 atlas
atlas = np.zeros((2 * SK, 2 * SK, 4), "f4")
for k in range(4):
    atlas[(k // 2) * SK:(k // 2 + 1) * SK, (k % 2) * SK:(k % 2 + 1) * SK] = exr("Light_Skin_Color", 1001 + k, SK)
skin_img = byte_image("skin_albedo", atlas, (2 * SK, 2 * SK))
iris_img = byte_image("iris", exr("Iris_Color", 1007, 512), (512, 512))
sclera_img = byte_image("sclera", exr("Sclera_Color", 1005, 512), (512, 512))
mouth_img = byte_image("mouth", exr("Mouth_Color", 1008, 512), (512, 512))


def material(name, image=None, colour=(1, 1, 1, 1), rough=0.5):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Roughness"].default_value = rough
    if image:
        t = m.node_tree.nodes.new("ShaderNodeTexImage")
        t.image = image
        m.node_tree.links.new(t.outputs[0], b.inputs["Base Color"])
    else:
        b.inputs["Base Color"].default_value = colour
    return m


SWIM = tuple(float(x) for x in os.environ.get("VOL_SWIM", "0.05,0.30,0.38").split(",")) + (1.0,)
slots = [  # the file's own material order
    material("Iris", iris_img, rough=0.25), material("Mouth", mouth_img, rough=0.4), material("Pupil", colour=(0.01, 0.01, 0.01, 1), rough=0.1),
    material("Sclera", sclera_img, rough=0.2), material("Skin", skin_img, rough=0.5), material("Swimwear", colour=SWIM, rough=0.55),
]

# ---- the mesh ---------------------------------------------------------------------------------------------------
mesh = bpy.data.meshes.new("vitruvian")
mesh.from_pydata([tuple(p) for p in pos], [], faces)
mesh.polygons.foreach_set("use_smooth", [True] * len(mesh.polygons))
for m in slots:
    mesh.materials.append(m)
mesh.polygons.foreach_set("material_index", mats)
layer = mesh.uv_layers.new(name="UVMap")
uv = []
for fi, f in enumerate(faces):
    tile = int(math.floor(sum(c[0] for c in loop_uv[fi]) / len(loop_uv[fi])))
    for (u, v) in loop_uv[fi]:
        lu = min(max(u - tile, 0.0), 1.0)
        if mats[fi] in (4, 5):  # skin tiles 0..3 -> the 2 x 2 atlas
            lu, v = ((tile % 2) + lu) / 2.0, ((tile // 2) + v) / 2.0
        uv.append((lu, v))
layer.data.foreach_set("uv", np.array(uv, "f4").ravel())
mesh.update()
obj = bpy.data.objects.new("body", mesh)
scene.collection.objects.link(obj)
print("BUILT", len(mesh.vertices), "verts", len(mesh.polygons), "faces, height %.3f" % (pos[:, 2].max() - pos[:, 2].min()), flush=True)

# ---- previews -------------------------------------------------------------------------------------------------
if do_shots:
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 24
    scene.cycles.use_denoising = True
    cd = bpy.data.cameras.new("cam")
    cam = bpy.data.objects.new("cam", cd)
    scene.collection.objects.link(cam)
    scene.camera = cam
    for name, loc, energy, size in (("key", (1.8, -2.5, 1.8), 220, 1.5), ("fill", (-2.5, -2.0, 0.9), 70, 2.5), ("rim", (0, 2.5, 1.8), 120, 1.5)):
        ld = bpy.data.lights.new(name, "AREA")
        ld.energy, ld.size = energy, size
        lo = bpy.data.objects.new(name, ld)
        lo.location = loc
        lo.rotation_euler = (math.pi / 2 - 0.3, 0, 0) if name != "rim" else (-math.pi / 2 + 0.3, 0, 0)
        scene.collection.objects.link(lo)
    w = bpy.data.worlds.new("w")
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = (0.12, 0.10, 0.16, 1)
    w.node_tree.nodes["Background"].inputs[1].default_value = 0.25
    scene.world = w

    def shot(name, yaw_deg, centre_z, scale, size=(630, 1140), ortho=True):
        cd.type = "ORTHO"
        cd.ortho_scale = scale
        scene.render.resolution_x, scene.render.resolution_y = size
        yaw = math.radians(yaw_deg)
        cam.location = Vector((math.sin(yaw) * 4.0, -math.cos(yaw) * 4.0, centre_z))
        cam.rotation_euler = (math.pi / 2, 0, yaw)
        scene.render.filepath = OUT + "%s-%s.png" % (preset, name)
        bpy.ops.render.render(write_still=True)
        print("SHOT", scene.render.filepath, flush=True)

    shot("front", 0, 0.87, 2.0)
    shot("side", 90, 0.87, 2.0)
    shot("three-quarter", 35, 0.87, 2.0)
    shot("face", 0, 1.60, 0.42, (700, 700))

# ---- the .glb the engine loads ------------------------------------------------------------------------------------
if do_export:
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    path = OUT + "vitruvian_%s.glb" % preset
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_image_format="JPEG",
                              export_jpeg_quality=90, export_yup=True, export_apply=False)
    print("EXPORTED", path, os.path.getsize(path), flush=True)
print("ALLDONE", flush=True)
