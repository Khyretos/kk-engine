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
  hairline, since a real face stands out in front of the sphere.

The creator puts the body in a `RigidWorld` as a mesh only cloth and hair
feel, so long hair falls over the shoulders and back without going
through them, and paints the scalp the hair's root colour (a copy of the
scalp's triangles 2 mm over the skin), so no skin shows between the
hairs. Turn spins the figure, not the camera, so the hair swings. A
slider change regrows the hair on the new head once the slider rests.

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

For the game, `dress(model, human, clothes)` adds the clothes to a
skinned bake as meshes on the body's own bones (`garmentMesh` keeps the
suit's skin weights), so a dressed person is one skinned model and costs
one skinned draw per garment; `CharacterCreatorModule::bakeSkinned()`
returns it. Skirts come along as skinned meshes there; a game that wants
them to swing runs them as cloth, as the creator does.

These are plain garments in one colour each: MakeHuman's own `.mhclo`
clothes (with textures) can't be downloaded from here, and are the next
step for richer outfits.

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
names to the engine's canonical ones), skin and eyes, MakeHuman's textured `.mhclo` clothes (each clothing
vertex rides three body vertices, so clothes fit every body),
then facial expressions (ENG-F25).
