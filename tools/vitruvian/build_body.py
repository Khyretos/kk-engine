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

# ---- Vitruvian's own skeleton and skin weights (Mixamo rig, weights/mixamo.npz, joints/Mixamo.npz) ------------
def packed(path):
    d = np.load(path)
    names = [n for n in bytes(d["names"]).decode().split("\x00") if n]
    off = np.concatenate([[0], np.cumsum(d["cnt"].astype(int))])
    return {n: (d["idx"][off[i]:off[i + 1]].astype(int), d["weights"][off[i]:off[i + 1]].astype("f8")) for i, n in enumerate(names)}


skin_w = packed(ROOT + "weights/Mixamo.npz" if os.path.exists(ROOT + "weights/Mixamo.npz") else ROOT + "weights/mixamo.npz")
joint_w = packed(ROOT + "joints/Mixamo.npz")
PREFIX = "mixamorig:"
PARENT = {"Spine": "Hips", "Spine1": "Spine", "Spine2": "Spine1", "Neck": "Spine2", "Head": "Neck"}
for side in ("Left", "Right"):
    PARENT.update({side + "Shoulder": "Spine2", side + "Arm": side + "Shoulder", side + "ForeArm": side + "Arm", side + "Hand": side + "ForeArm",
                   side + "UpLeg": "Hips", side + "Leg": side + "UpLeg", side + "Foot": side + "Leg", side + "ToeBase": side + "Foot"})
    for f in ("Thumb", "Index", "Middle", "Ring", "Pinky"):
        PARENT[side + "Hand" + f + "1"] = side + "Hand"
        PARENT[side + "Hand" + f + "2"] = side + "Hand" + f + "1"
        PARENT[side + "Hand" + f + "3"] = side + "Hand" + f + "2"


def joint(name):
    if name not in joint_w:
        return None
    i, w = joint_w[name]
    return (pos[i] * w[:, None]).sum(0) / w.sum()


BONES = {}  # short name -> (head, tail)
for full in skin_w:
    short = full[len(PREFIX):]
    head, tail = joint("joint_%s_head" % full), joint("joint_%s_tail" % full)
    if head is None:
        head = BONES[PARENT[short]][1]
    if tail is None:
        print("NOTAIL", short, flush=True)
        up = BONES[PARENT[short]]
        tail = head + (up[1] - up[0]) * 0.4
    BONES[short] = (head, tail)
# every weighted vertex's strongest bone, to cut the swimwear by anatomy
strongest = np.full(len(pos), "", dtype=object)
best = np.zeros(len(pos))
for full, (i, w) in skin_w.items():
    m = w > best[i]
    strongest[i[m]] = full[len(PREFIX):]
    best[i[m]] = w[m]
print("BONES", len(BONES), "weighted vertices", int((best > 0).sum()), "of", len(pos), flush=True)

# ---- the bandeau and the shorts: cloth shells 4 mm over the skin ----------------------------------------------------
# Cut by geometry, not by whole faces (the mesh rows are not level, whole faces make a sawtooth hem): every
# triangle is clipped against level planes (the hems) and against the 0.5 line of a smooth weight field (the
# armpits for the bandeau, the waist and the legs for the shorts). New vertices get interpolated positions
# and skin weights.
BAND_Z = tuple(float(x) for x in os.environ.get("VOL_BAND", "1.17,1.38").split(","))
SHORTS_Z = tuple(float(x) for x in os.environ.get("VOL_SHORTS", "0.76,0.99").split(","))
CLOTH = float(os.environ.get("VOL_CLOTH", "0.004"))
bone_names = list(skin_w)
col = {n[len(PREFIX):]: k for k, n in enumerate(bone_names)}
W = np.zeros((len(pos), len(bone_names)), "f4")
for k, n in enumerate(bone_names):
    i, w = skin_w[n]
    W[i, k] = w
torso_w = W[:, [col[n] for n in ("Spine", "Spine1", "Spine2")]].sum(1)
pelvis_w = W[:, [col[n] for n in ("Hips", "LeftUpLeg", "RightUpLeg")]].sum(1)
mats = [4 if m == 5 else m for m in mats]  # the file's mask is plain skin now
vnorm = np.zeros_like(pos)
for f in faces:
    n = np.cross(pos[f[1]] - pos[f[0]], pos[f[2]] - pos[f[0]])
    for v in f:
        vnorm[v] += n
vnorm /= np.maximum(np.linalg.norm(vnorm, axis=1), 1e-9)[:, None]


def clip(poly, k):
    """Sutherland-Hodgman against field k >= 0; a vertex is (position, normal, weights, fields)."""
    out = []
    for a, b in zip(poly, poly[1:] + poly[:1]):
        fa, fb = a[3][k], b[3][k]
        if fa >= 0:
            out.append(a)
        if (fa >= 0) != (fb >= 0):
            t = fa / (fa - fb)
            out.append(tuple(x + (y - x) * t for x, y in zip(a, b)))
    return out


shell_pos, shell_nrm, shell_w, shell_tri, shell_key = [], [], [], [], {}


def shell_vertex(v):
    key = tuple(np.round(v[0] * 1e6).astype(np.int64))
    if key not in shell_key:
        shell_key[key] = len(shell_pos)
        shell_pos.append(v[0])
        shell_nrm.append(v[1] / max(np.linalg.norm(v[1]), 1e-9))
        shell_w.append(v[2])
    return shell_key[key]


for zr, field in ((BAND_Z, torso_w), (SHORTS_Z, pelvis_w)):
    fields = np.stack([pos[:, 2] - zr[0], zr[1] - pos[:, 2], field - 0.5], axis=1)
    for fi, f in enumerate(faces):
        if mats[fi] != 4 or fields[f][:, :2].min() < -0.05 and False:
            continue
        if (fields[f][:, 0] < 0).all() or (fields[f][:, 1] < 0).all() or (fields[f][:, 2] < 0).all():
            continue  # wholly outside one hem or the weight line
        for tri in (f[:3], [f[0], f[2], f[3]] if len(f) == 4 else None):
            if tri is None:
                continue
            poly = [(pos[v].copy(), vnorm[v].copy(), W[v].copy(), fields[v].copy()) for v in tri]
            for k in range(3):
                poly = clip(poly, k)
                if len(poly) < 3:
                    break
            if len(poly) < 3:
                continue
            idx = [shell_vertex(v) for v in poly]
            for j in range(1, len(idx) - 1):
                if len({idx[0], idx[j], idx[j + 1]}) == 3:
                    shell_tri.append([idx[0], idx[j], idx[j + 1]])
base_count = len(pos)
pos = np.vstack([pos, np.array(shell_pos) + np.array(shell_nrm) * CLOTH])
shell_w = np.array(shell_w)
for t in shell_tri:
    faces.append([base_count + i for i in t])
    mats.append(5)
    loop_uv.append([(0.0, 0.0)] * 3)
print("SWIMWEAR", len(shell_tri), "triangles,", len(shell_pos), "new vertices", flush=True)

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
# ---- armature + vertex groups -------------------------------------------------------------------------------------
rig_data = bpy.data.armatures.new("rig")
rig = bpy.data.objects.new("rig", rig_data)
scene.collection.objects.link(rig)
bpy.context.view_layer.objects.active = rig
bpy.ops.object.mode_set(mode="EDIT")
edit = {}
for short, (head, tail) in BONES.items():
    b = rig_data.edit_bones.new(PREFIX + short)
    b.head = tuple(head)
    b.tail = tuple(tail) if np.linalg.norm(tail - head) > 1e-4 else tuple(head + np.array([0, 0, 0.01]))
    edit[short] = b
for short, b in edit.items():
    if short in PARENT:
        b.parent = edit[PARENT[short]]
bpy.ops.object.mode_set(mode="OBJECT")
obj.parent = rig
mod = obj.modifiers.new("Armature", "ARMATURE")
mod.object = rig
for k, full in enumerate(bone_names):
    vg = obj.vertex_groups.new(name=full)
    idx, w = skin_w[full]
    for v, wt in zip(idx.tolist(), w.tolist()):
        vg.add([v], wt, "REPLACE")
    for i in np.nonzero(shell_w[:, k] > 0)[0].tolist():
        row = shell_w[i]
        top = np.argsort(row)[-4:]  # the strongest four, normalised
        if k in top:
            vg.add([base_count + i], float(row[k] / row[top].sum()), "REPLACE")
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
    rig.select_set(True)
    path = OUT + "vitruvian_%s.glb" % preset
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_image_format="JPEG",
                              export_jpeg_quality=90, export_yup=True, export_apply=False)
    print("EXPORTED", path, os.path.getsize(path), flush=True)
print("ALLDONE", flush=True)
