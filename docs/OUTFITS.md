# Outfits: dressing a mannequin in the colours a player picks

`kke::Outfit` (`kke/Outfit.h`) is a humanoid's clothes as four colours: the
skin, a top, the trousers and the shoes. `kke::dressModel` paints them onto
any skinned humanoid (the UAL mannequin, a Synty character) by which bone
moves each part of the mesh, so no textures or extra meshes are needed.
Climb Race and Tennis use it for their start menus.

## How it works

- Each bone belongs to a region (`kke::regionOfBone`): head, neck and hands
  are skin; spine, clavicles and upper arms are the top (forearms too with
  `sleeves`); pelvis and legs are the trousers; feet and toes are the shoes.
  Names go through `canonicalBoneName`, so Mixamo, UE and Synty rigs work.
- `dressModel(model, outfit)` splits every skinned mesh into one mesh per
  region. A triangle goes to the region holding most of its vertices'
  weight, so seams follow the joints. Each region gets a material of its
  own (`Outfit_Skin`, `Outfit_Top`, `Outfit_Bottom`, `Outfit_Shoes`) in the
  outfit's colour; textures are dropped. Static meshes are left alone.
- The dressed copy draws the same triangles as the plain model, so an
  outfit costs nothing per frame.

## Using it in a game

```cpp
#include "kke/Outfit.h"

kke::Outfit o;
o.skin = kke::skinTones()[6].rgb;
o.top = { 0.35f, 0.65f, 1.0f };
o.bottom = kke::clothColours()[3].rgb;

// One dressed copy per outfit, shared by everyone wearing it.
const std::string key = kke::outfitKey(o);
ModelModule::ModelId id = models.add(kke::dressModel(baseModel, o), "mygame/person/" + key);
models.spawn(id, transform); // no tint: the colours are the materials'
```

To change clothes, remove the instance and spawn the new outfit's model with
the same transform; the animator and bone locals carry on.

## Menu rows

`kke::skinTones()` is nine tones from Porcelain to Ebony (lightest first),
so everyone can play as themselves. `kke::clothColours()` is twelve
clothing colours. Climb Race and Tennis add them as start-menu look fields
(Skin, Trousers, Shoes; the existing Colour row is the top):

```cpp
kke::Lobby::LookField skin{ "skin", "Skin", {}, {} };
for (const kke::NamedColour& c : kke::skinTones()) {
    skin.choices.push_back(c.name);
    skin.swatches.push_back(c.rgb);
}
lobby.addLookField(std::move(skin));
```

## Online

A player's look travels as their NetModule character, `look:a.b.c.d.e|...`
([NETWORKING.md](NETWORKING.md) "Names and looks that change"). Every
screen turns it back into an outfit with `Lobby::lookFromText`, so a new
pick in the menu shows on every screen at once. CPU players' clothes come
from their name, so they look the same everywhere too.
