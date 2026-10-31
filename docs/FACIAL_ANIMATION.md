# Blend shapes and facial animation

ENG-F25. A face is a mesh whose shape changes: a smile, a blink, a jaw that drops, a mouth for each vowel. Those
are **blend shapes** (glTF calls them morph targets): each is a list of offsets from the rest shape, and at weight
`w` the mesh is `rest + w * offset`, summed over the targets. The engine reads them from glTF, applies them before
skinning (so a face morphs and moves with its bones), and `kke::FaceRig` turns "happy", "say this" and "blink" into weights.

## Blend shapes

A `.glb`/`.gltf` with morph targets loads them with their names (`mesh.extras.targetNames`, which Blender writes) and
default weights (`kke::ModelMesh::morphs`, `morphWeights`; `kke/ModelAsset.h`). They are kept sparse, only the vertices a target moves.

```lua
local head = models.spawn(models.load("my_character"), {pos = {0, 0, 0}})
face.morphs(head)                        -- {"jawOpen", "mouthSmileLeft", "eyeBlinkLeft", ...}: the model's own names
face.morph(head, "jawOpen", 0.6)         -- one blend shape by name; any spelling ("jaw_open", "JawOpen")
```

In C++: `ModelModule::setMorphWeight(instance, "jawOpen", 0.6f)`, `setMorphWeights(instance, {{"jawOpen", 0.6f}})`,
`morphNames(instance)`. One name moves every mesh part that has it (the head, the eyes and the teeth each carry "blink" or "jawOpen").
A weight can go past 1 (to exaggerate) or below 0.

How it is drawn: each skinned part with a weight not zero gets its own bind-pose vertices (the rest shape plus the targets, normals
corrected), uploaded when the weights change, and the skinning pass reads those instead of the model's shared ones. The CPU skinning
path does the same maths on the CPU; the two give the same picture. A face at rest costs nothing.
`tools/morph_test_model.py` writes a small skinned glTF with morph targets to try it on.

## Facial animation: `face.*` and `kke::FaceRig`

```lua
face.expressions(head)                   -- {"happy", "blink", "aa", ...}: what this model can do
face.expression(head, "happy", 0.8)      -- eases to it; true, or false when the model cannot do it
face.expression(head, "neutral")         -- every emotion back to rest
face.say(head, "Welcome to the village.", 3.0)   -- the mouth moves through the vowels of the text for 3 seconds
face.level(head, 0.7)                    -- or follow a voice's loudness, 0..1 (every frame while it talks)
face.blink(head); face.autoBlink(head, false)
face.clear(head)
```

Expressions: `happy angry sad surprised relaxed blink blinkLeft blinkRight`, and the mouth shapes (visemes)
`aa ih ou ee oh sil`. Faces name their shapes in a few different ways, so each expression is a list of recipes tried in
order, and the first one the model has all the shapes for is used:

| Expression | Tried in order |
|---|---|
| `happy` | `happy` (VRM), `joy`, `Fcl_ALL_Joy` (VRoid), `mouthSmileLeft` + `mouthSmileRight` + `cheekSquint` (Apple ARKit), `smile` |
| `blink` | `blink`, `eyeBlinkLeft` + `eyeBlinkRight`, `Fcl_EYE_Close`, `eyesClosed`, `eyeClose` |
| `aa` `ih` `ou` `ee` `oh` | the VRM names, `viseme_aa` ... (Oculus, Ready Player Me), `Fcl_MTH_A` ... (VRoid), or the ARKit mouth shapes (`jawOpen`, `mouthPucker`, `mouthFunnel`...) |
| `angry` `sad` `surprised` `relaxed` | the VRM names, VRoid's `Fcl_ALL_*`, or the ARKit brow, eye and mouth shapes |

Names match ignoring case and `_`, `-`, space. `face.expressions()` says what a model ended up with.

The rig eases every expression (emotions in about a quarter of a second, mouth shapes and blinks faster), blinks by
itself every two to six seconds unless the eyes are held shut, and never lets a morph past 1 when two expressions
share one. `face.say` builds its mouth movement from the vowels of the text spread over the time you give (a, e, i, o, u and
the accented vowels open the mouth, m b p and pauses close it): it reads as speech beside a subtitle (`subtitle.say` with the same text
and `SubtitleQueue::readingSeconds`). For exact sync, give `FaceRig::say` your own keys. Once a model has a face rig, the rig owns its
weights: use `face.morph`, not `models`-level calls.

In C++: `models->face(instance).setExpression("happy", 0.8f)`; `FaceRig` itself (`kke/FaceRig.h`) is pure logic you can test or
drive without a model.

## Not done

- Blend shape weights animated inside a clip (glTF animation channels of type `weights`) are not read yet: drive them
  from `face.*` or Lua.
- VRM files' own expression presets (`blendShapeMaster`) are not read: the VRM loader is in the anime-style work that is not
  merged yet; its presets map onto the recipes above by name.
- Static (unskinned) meshes with morph targets are read but not morphed on screen; give the mesh a one-bone skin.
- Eye look (looking at a target with the eyes) and tongue shapes.
- A mesh the loader had to flat-shade (no normals in the file) drops its morph targets.
