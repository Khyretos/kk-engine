# Humans: MakeHuman bodies from sliders

`kke::Human` (`kke/Human.h`) makes a realistic person from slider values:
gender, age, muscle, weight, height, ancestry, and about 300 face and body
sliders (nose hump, eye fold, jaw, waist, hand size...). It is the base of
character creator screens: set the sliders, `bake()` once, and the result
is an ordinary skinned model, so a custom character costs the same per
frame as any other model and animates with the engine's GPU skinning.

The human is [MakeHuman](http://www.makehumancommunity.org)'s: its base
mesh, its 1,280 morph targets, its skeleton and skin weights.

## Licence: what can ship

| Part | Licence | In a game, commercial too |
| --- | --- | --- |
| Base mesh, morph targets, modifier lists, skeleton, skin weights | CC0 (MakeHuman's [LICENSE.md](https://github.com/makehumancommunity/makehuman/blob/master/LICENSE.md), section C) | Yes, nothing asked |
| MakeHuman's program code | AGPL-3.0 | Not used: the engine reads only the data files, with its own code |
| MPFB2, MakeHuman's Blender add-on | GPL-3.0 | A tool on your PC; what you export with it is yours |
| Community asset packs (clothes, hair, skins) | CC0 or CC-BY, per pack | Yes; CC-BY packs need their authors in the game's credits |

`assets/humans/credits.json` lists every asset in the pack with its
authors and licence, for a game's credits screen.

## The pack

`assets/humans/makehuman.kkhpack` (about 10 MB) is cooked from MakeHuman's
`makehuman/data` folder at the commit in `tools/makehuman/makehuman.lock`:

```bash
git clone https://github.com/makehumancommunity/makehuman /tmp/makehuman
git -C /tmp/makehuman checkout "$(sed -n 's/^commit //p' tools/makehuman/makehuman.lock)"
python3 tools/makehuman/cook_human.py /tmp/makehuman/makehuman/data \
    assets/humans/makehuman.kkhpack --credits assets/humans/credits.json \
    --commit "$(sed -n 's/^commit //p' tools/makehuman/makehuman.lock)"
```

The cooker keeps every target exact (the `.target` files are in steps of
0.1 mm and so is the pack) but leaves out the vertices a target doesn't
move and packs the rest with zlib: 127 MB of text becomes 10 MB. Its
format is described at the top of `cook_human.py`, and
`tools/makehuman/test_cook_human.py` checks it.

## Using it

```cpp
#include "kke/Human.h"

static const kke::HumanPack pack = kke::HumanPack::load("assets/humans/makehuman.kkhpack");

kke::Human h(pack);
h.set("macrodetails/Gender", 0.2f);            // 0 female, 1 male
h.set("macrodetails/Age", 0.5f);               // 0 = 1 year, 0.1875 = 11, 0.5 = 25, 1 = 90
h.set("macrodetails-universal/Muscle", 0.7f);
h.set("nose/nose-hump-decr|incr", 0.4f);       // two-sided: -1..1

kke::Human::Bake bake = h.bake();               // bake.model is a ModelData
ModelModule::ModelId id = models.add(bake.model, "player/1"); // one model per look

// While a slider is dragged: move the same bake, then re-upload it.
h.set("chin/chin-width-decr|incr", -0.3f);
h.reshape(bake);
```

Slider names are MakeHuman's own, so a save made here and an `.mhm` file
from the MakeHuman app use the same words. `HumanPack::modifiers()` lists
them all with their group (the tab) and range;
`HumanPack::description(name)` has MakeHuman's one-line explanation.

### Body type

The body-type sliders blend MakeHuman's macro targets. Each one is named
after the body it was sculpted for (`universal-female-young-averagemuscle-maxweight`,
`height/male-old-minmuscle-averageweight-maxheight`), and its weight is how
close the sliders are to that body: per word, 1 at that word's slider
value, falling linearly to 0 at the next value either side; the words'
weights multiply. Ancestry (African, Asian, European) is a mix: the three
always add up to 1, and setting one scales the other two.

| Slider | Values the targets were made at |
| --- | --- |
| Gender | female 0, male 1 |
| Age | baby 0 (1 year), child 0.1875 (11), young 0.5 (25), old 1 (90) |
| Muscle, Weight, breast size and firmness | min 0, average 0.5, max 1 |
| Height, proportions | min (or uncommon) 0, max (or ideal) 1; none at 0.5 |

### The bake

- One mesh per group: the skin, plus MakeHuman's helper eyes, teeth and
  tongue, which stand in until eye and teeth assets are added.
- The skeleton is MakeHuman's default rig (163 bones). Each joint sits at
  the centre of its own vertices on the shaped body, so a tall, broad or
  small body gets a skeleton its size. A bone's roll comes from its plane
  of three joints.
- Up to four bone weights per vertex, the strongest, renormalised.
- `Bake::sourceVertex` maps each model vertex back to the pack vertex it
  came from (a vertex on a UV seam becomes several), which `reshape` uses.

## The creator screen

`kke::CharacterCreatorModule` (`kke/modules/CharacterCreatorModule.h`) is
the screen, ready for any game: tabs of sliders (body type, shape, torso,
arms and legs, head and neck, eyes, nose, mouth and jaw, ears, skin,
hair),
mirrored sides, undo and redo, randomise (believable: few sliders, small
moves), camera views, save and load, and the share code. A game adds it
next to a `UiModule` and a `ModelModule`, calls `open()`, and hears
`onDone`:

```cpp
auto& creator = app.addModule<kke::CharacterCreatorModule>("player1"); // saves player1.kkhuman
creator.onDone([&](const kke::HumanLook& look) {
    ModelModule::ModelId id = models.add(creator.bakeSkinned(), "player/me");
    // Send creator.shareCode() to the others with your game's messages;
    // they call fromShareCode, applyLook and bake.
});
```

In CMake, `kke_use_character_creator(my_game)` copies the screen
(`assets/ui/character_creator/`) and the human pack next to the game.
`games/character_creator` is the demo, and its README has the controls.

While a slider moves only vertices move (`Human::reshape` and
`ModelModule::setDeformedVertices`), so dragging stays smooth on a phone
GPU too.

## Hair

`kke/HumanHair.h` grows the engine's strand hair ([HAIR.md](HAIR.md)) on
a MakeHuman head, whatever shape the sliders gave it:

- `findScalp(pack, shaped)` finds where hair grows: the skin the head
  bone carries, above the brows and the ear tops and behind the
  hairline (heights measured from the eyes, as fractions of the head, so
  they scale with it), and the sphere that fits it best.
- `hairOnHuman(desc, style, pack, shaped, guides)` lays any hairstyle
  (`hairstyleOnHead` names: afro, box braids, cornrows, locs...; or a
  type, `short`, `long`, `4c`...) on that sphere, then sets every root on
  the skin straight under it (a ray against the scalp's faces). Roots with
  no scalp under them are dropped. Loose styles start back from the
  hairline, since a real face stands out in front of the sphere. Rows and
  braids (cornrows) lie on the sphere fitted to the scalp, a row starting
  in front of the hairline starts further back, and their ends sit on the
  skin.

The creator puts the body in a `RigidWorld` as a mesh only cloth and hair
feel, so long hair falls over the shoulders and back without going
through them, and shades the scalp a little darker than the skin (a copy
of the scalp's triangles 2 mm over the skin, as under real hair; painted
the hair's colour it showed as a patch with a hard edge). Turn spins the figure, not the camera, so the hair swings. A
slider change regrows the hair on the new head once the slider rests.

## Eyes

MakeHuman's helper eyes are plain white balls (the iris is a texture the
pack doesn't carry). `kke/HumanEyes.h` `addIrises(model, colour)` adds a
pupil, the iris and its darker rim on each eyeball's front, as thin domes
just over the ball with 32 sides, so they stay round on the coarse
eyeball, skinned like the eye. The creator tints them the eye colour
chosen on the Skin & eyes tab; `bakeSkinned()` includes them.

## Brows and lashes

MakeHuman's eyebrow and eyelash meshes live in its asset library, not in
the base the pack is cooked from, so `addBrows(model, skin, colour)` (also
in `kke/HumanEyes.h`) grows them from the face: about 260 short hairs per
brow lying flat on the skin over the brow ridge (thick and upright at the
inner end, thin and swept out at the outer), and 60 curled lashes along
each upper lid's edge, where the skin meets the eyeball's top half. Each
hair is a thin tapered strip with the skin weights of the skin under its
root, so brows follow the face and lashes the lids. They come from a
fixed seed: the same face gives the same brows, with the same number of
hairs after any slider, which `update` relies on. The creator tints them
the hair's root colour (lashes darker); `bakeSkinned()` includes them.

## Skin

A flat colour made every face look like a doll's. Three maps in the
body's UV layout (2048 square), `assets/humans/skin_*.png`, give it what
a single tint can't, and the creator puts them on the body's material:

- `skin_albedo.png` is near white and multiplies the chosen skin tone, so
  every tone keeps its colour: scanned blotchiness, the knees', elbows'
  and knuckles' colour, redder lips, a blush over the cheeks, nose and
  ears.
- `skin_normal.png`: creases, wrinkles and lip lines from the scan, plus
  fine pores (stronger on the face than on the body).
- `skin_roughness.png`: the scan's gloss pattern; glossy lips.

They come from CharMorph's Vitruvian body (CC0, Sean Buckley and Olaf
Delgado-Friedrichs): a 37,000 vertex body with 4K skin textures over
four UDIM tiles. `tools/makehuman/vitruvian_export.py` (run with
Blender's `bpy` from PyPI) writes its mesh to an `.npz`;
`tools/makehuman/bake_vitruvian.py MAKEHUMAN_DATA VITRUVIAN vit.npz
assets/humans` (numpy, scipy, OpenEXR; about 2 minutes) warps it onto
MakeHuman's base body and reads, for every texel of MakeHuman's UV
layout, the Vitruvian skin at that spot:

1. A thin-plate spline through 42 points both bodies have (the joints of
   both rigs, the eyes, the nose tip, chin, crown, back of the head, ears,
   heels and toes) brings Vitruvian into MakeHuman's place.
2. Six refits pull its surface onto MakeHuman's, each adding the nearest
   points that face the same way as pairs (mean gap 1.6 mm at the end).
3. Each texel takes the nearest spot on a Vitruvian skin triangle that
   faces its way (the lips and fingers lie close to other surfaces),
   0.7 mm away at the median.

The colour is divided by Vitruvian's own skin tone, so the player's
colour still sets the tone; half its colour shifts are kept (all of them
read as bruises over a warm skin) and two thirds of its brightness swings.
The lips' red and the blush come from MakeHuman's CC0 skin weights (the
lips are what the `oris*` mouth ring bones move, the lids
`orbicularis*`, the cheeks `risorius*` and `levator05-06`, the nose
`levator02-04`, the ears `temporalis02`). The height loses its slow swell
(that is the mesh's job) before it becomes the normal map.

So the realistic body keeps MakeHuman's 13,000 vertices and every slider;
the detail is three texture reads in the normal model shader, no extra
pass. The baked maps are the project's own (MIT).

`tools/makehuman/bake_skin.py` still makes the anime style's
`skin_albedo_anime.png`, and the procedural set when Vitruvian isn't at
hand.

## Clothes

`kke/HumanClothes.h` cuts clothes from two shapes MakeHuman's base mesh
carries and every morph target moves along with the skin: a body suit a
few millimetres over the skin (`helper-tights`) and an ankle-length skirt
(`helper-skirt`). So a garment is already the size of whatever body the
sliders made, with no fitting step.

- Fitted garments (T-shirt, tank top, crop top, long sleeve, shorts,
  trousers, socks, shoes) are the suit's vertices picked by the bone that
  moves each one and by height: tops end at the waist (the skirt's top),
  tucked in 3 cm; bottoms start there; socks and shoes by height over the
  sole. Each is drawn a little further out than the one under it (socks
  1 mm, bottoms 2, tops 3, shoes 6), so layers never fight.
- Skirts (tennis skirt, skirt, long skirt: the skirt's top 30%, 55% or
  all of it) are the engine's cloth (docs/CLOTH.md), pinned at the
  waistband to the figure and falling against the body collider the hair
  uses, drawn with the fabric shader.

`garmentShape(pack, shaped, id)` gives a garment's vertices; the creator
builds a model of the suit's triangles from them (moved with the body on
every slider drag, like the scalp cap) or a `ClothDesc` from the skirt's
faces. A slider change re-cuts the clothes once it rests, since socks and
skirts are cut by height.

The suit follows the body's targets but lags the strongest muscle and
bust ones by a few millimetres, and the skin showed through a shirt on a
Strong body. So every garment vertex also remembers the skin vertex
nearest it (`skinUnder`) and is pushed out along that vertex's normal
until it is at least its layer's offset over the skin (`keepOutside`),
on every drag and in `dress()`.

For the game, `dress(model, human, clothes)` adds the clothes to a
skinned bake as meshes on the body's own bones (`garmentMesh` keeps the
suit's skin weights), so a dressed person is one skinned model and costs
one skinned draw per garment; `CharacterCreatorModule::bakeSkinned()`
returns it. Skirts come along as skinned meshes there; a game that wants
them to swing runs them as cloth, as the creator does.

These are plain garments in one colour each: MakeHuman's own `.mhclo`
clothes (with textures) can't be downloaded from here, and are the next
step for richer outfits.

## Anime style

One person, two looks: the Realistic and Anime buttons switch how the
same meshes, bones and materials are drawn, so a look, an outfit and every
animation work in either (the "Character pipeline: one base, two styles"
plan). `ModelModule::setToon(instance, true, outline)` draws an instance
with `shaders/model_toon.frag` in place of model.frag, after the
manner of VRM's MToon material:

- Two tones, not a gradient: lit, or a cooler darker shade of the same
  colour, split at a narrow soft edge a little past the side; cast
  shadows only half way, so a nose or fringe shadow doesn't cut a face.
- The sky's light flattened, a rim light on the silhouette, and one small
  hard highlight where a material is really glossy (lips, eyes).
- No normal map: pores would be noise in this style.
- An outline: the mesh drawn again pushed out along its normals with only
  its back faces (`model_outline.vert`/`.frag`), in a dark, more saturated
  shade of its own colour, a little wider far away. The creator gives the
  body 1.2 mm and clothes 1.5 mm, and none to eyes, brows and teeth.

Anime hair is the same simulated guides drawn as locks
(`kke/HumanHair.h` `hairLocks`): each guide a tapered, flattened
four-sided spike about 2.6 times the root spacing wide (`lockWidth`), so
locks overlap, rebuilt from the guides every frame and drawn toon with an
outline. The physics is the strand hair's, with the style's hold raised
to 0.35 so locks keep their shape and still bounce; the scalp under them
takes the hair colour. Braids and twists keep a braid's width.

Toon instances are drawn one at a time (never instanced) and cost one
more draw per mesh for the outline. The pipelines are made by the first
`setToon`, so a game without it needn't ship the shaders. Not yet toon:
cloth skirts (their own renderer).

## Multiplayer

A character travels as its look (`kke/HumanLook.h`), never as a mesh:
slider values, colours, the hairstyle and the clothes (each a name and a
colour). `shareCode(look, pack)` packs it into a
URL-safe string, `kh1.` and base64 of the colours, one bit per slider
(off its default or not) and a byte per moved slider: about 90 letters for
a few changes, 250 for a random person. `fromShareCode` reads it back. The
code carries a key of the pack's slider list, so a machine with another
pack refuses it instead of building someone else.

Every machine loads the same pack and bakes the same body from the same
values. The player's own body is baked from `quantised(look, pack)`, the
look rounded exactly as the code rounds it, so the vertices match
everywhere. A look read back and coded again gives the same code (a CI
self-test checks this on a random person every run).

Saves are JSON (`lookToJson`, `lookFromJson`: format `kk-human`, version

1) with MakeHuman's slider names, and `lookFromMhm` reads MakeHuman's own
`.mhm` saves.

## What comes next

The plan for the whole creator is in the project's "Character creator
plan" document. In order: animation through retargeting (MakeHuman bone
names to the engine's canonical ones), MakeHuman's textured `.mhclo` clothes (each clothing
vertex rides three body vertices, so clothes fit every body),
then facial expressions (ENG-F25).
