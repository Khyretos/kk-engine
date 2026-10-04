# Asset folders and naming

Where art, sound and data go, what the engine expects in each folder,
and how it finds your packs whatever you called their folders.

Short version: put 3D packs (models, characters, animations) in
`assets/synty/`, put 2D packs (menu and HUD sprites, icons) in
`assets/sprites/`, keep the folder names you downloaded them with, and
run `kke_assets` to see what the engine found and what each game still
needs.

## The folders

Everything under `assets/` in the repository, and next to the games in a
download:

| Folder | What goes in it | In git? | Who reads it |
|---|---|---|---|
| `assets/synty/` | Your 3D art packs: models (`.fbx .obj .gltf .glb`), their textures, Synty Sidekick parts (`.sk`), animation packs. Any vendor, not only Synty | **No**, ignored: packs are licensed to you | Every demo with art (`kke::AssetCatalog`) |
| `assets/sprites/` | Your 2D packs: menu and HUD sprites, icons, cursors (`.png .tga .jpg .bmp`) | **No**, ignored, except its README | Menus and HUDs (`<img src="sprite:NAME"/>`, `kke::SpriteCatalog`) |
| `assets/animations/` | `UAL1_Standard.fbx`, the mannequin and its clips (Quaternius, CC0). `UAL2.fbx` may sit next to it | Only `UAL1_Standard.fbx` | Every demo with people in it |
| `assets/prompts/xelu/` | Controller and keyboard button glyphs (Xelu, CC0) | Yes | Button prompts (docs/INPUT.md) |
| `assets/fonts/` | Noto Sans and Noto Color Emoji (OFL) | Yes | All text |
| `assets/ambience/` | Looping background sounds (`.flac`, `.wav`, `.mp3`), named after what they are (`wind_soft.flac`) | Yes (CC0 only) | Moods (docs/MOODS.md) |
| `assets/moods/` | Mood files: sky, sun, fog, look (`<name>.yaml`) | Yes | Every demo (docs/MOODS.md) |
| `assets/branding/` | The Kreative Kompas logo, icon and banner | Yes | Window icon, intro, README |
| `assets/textures/` | Small test images the engine's own tests use | Yes | Tests |
| `bin/assets/skies/` | HDR sky pictures (Poly Haven, CC0), downloaded when you configure the build, straight into the build folder | No | Moods |

Music and sound-effect packs have no folder yet: the engine doesn't load
audio packs on its own. `kke_assets` says so when it finds one.

### Somewhere else on your disk

You don't have to copy packs into the repository. Point the engine at the
folder that holds them:

```bash
KKE_ASSETS_DIR=~/Synty ./racing            # 3D packs (KKE_SYNTY_DIR also works)
KKE_SPRITES_DIR=~/SyntyUI ./rmlui_demo     # 2D packs
KKE_ANIMATIONS_DIR=~/anims ./duel          # UAL1_Standard.fbx and UAL2.fbx
```

Without these the games look for `assets/synty` (and `assets/sprites`,
`assets/animations`) in the folder you start them from, up to four
folders above it, and next to the executable. When nothing is found
they say where they looked.

## Folder names don't matter

Keep packs exactly as they came out of the zip. All of these are found
as the same pack:

```text
assets/synty/POLYGON_Street_Racer/                 the short name
assets/synty/POLYGON_Street_Racer_SourceFiles_v3/  the download's name
assets/synty/PolygonStreetRacer/                   no underscores
assets/synty/Street Racer stuff/                   your own name: found by its files
assets/synty/Synty/POLYGON_Street_Racer_v3/        packs inside a folder of their own
```

How it works (`kke/KnownPacks.h`):

1. **By name.** The engine drops the download's suffix (`_SourceFiles`,
   `_Source_Files`, `_Source_Sprites`, `[Source]`, `[Pro]`, `_Unity_2022…`,
   `_Unreal…`, a ` (1)` from a second download), a version (`_v3`),
   capitals, spaces and punctuation, and compares what is left:
   `POLYGON_Street_Racer_SourceFiles_v3` and `polygon street racer` are
   both `polygonstreetracer`.
2. **By its files.** A folder whose name isn't a pack the games know is
   searched for files only that pack has (the "markers" in
   `engine/src/KnownPacks.cpp`, such as
   `PolygonStreetRacer_Veh_Tex_01_Race_Purple.png`). So a folder you
   renamed to anything still works.
3. **Packs in a folder.** A folder that isn't a pack but holds packs
   (`Synty/POLYGON_Town`, `Synty/POLYGON_Nature`) is looked into, one
   level down.

One rule keeps big libraries fast: a folder named the way Synty names
packs (`POLYGON_…`, `SIDEKICK_…`, `ANIMATION_…`, `INTERFACE_…`) is the
pack its name says and is never searched. `POLYGON_Casino` is Casino; a
game that wants two packs out of seventy reads two.

Inside a pack, any layout works: `FBX/`, `Models/`, `SourceFiles/`,
`Source_Files/`, `_SourceFiles/`, characters in their own folder,
textures anywhere in a folder called `Textures`. OBJ copies of FBX files
are ignored (FBX carries more). Unreal-only packs (`.uasset`) and
Unity packages that weren't unpacked can't be read; `tools/fetch_assets.sh`
unpacks `.unitypackage` files for you.

## Naming conventions

The engine sorts what it finds by file-name prefix, Synty's own
convention. Your own art can use the same prefixes to land in the right
category in the sandbox's asset lists:

| Prefix | Means | Example |
|---|---|---|
| `SK_` | A skinned character (has bones) | `SK_Character_Father_01.fbx` |
| `SM_Chr_` | A character part: hair, hat, attachment | `SM_Chr_Attach_Male_Hair_01.fbx` |
| `SM_Bld_` | A building or building piece | `SM_Bld_Shop_01.fbx` |
| `SM_Prop_` | A prop | `SM_Prop_Crate_01.fbx` |
| `SM_Env_`, `SM_Generic_` | Ground, trees, rocks, roads | `SM_Env_Tree_01.fbx` |
| `SM_Veh_` | A vehicle or vehicle part | `SM_Veh_Car_Muscle_01.fbx` |
| `SM_Wep_` | A weapon | `SM_Wep_Sword_01.fbx` |
| `FX_` | An effect mesh | `FX_Bullet_Trail_01.fbx` |
| `A_` | An animation clip | `A_Run_F_Masc.fbx` |
| `<Pack>_Texture_01` | The pack's colour atlas; `_02`…`_10` are recoloured versions | `PolygonTown_Texture_01_A.png` |
| `ICON_` | An icon (sprite packs) | `ICON_Map_Fire.png` |
| `SPR_` | A sprite: bar, frame, background (sprite packs) | `SPR_Background_Bar_Horiz.png` |

A sprite's name is its file name without the extension. Its category is
the folder it's in (`Icons_Map`), and its pack is the top folder in
`assets/sprites` (loose images there are pack `sprites`).

## Sprites in menus and HUDs

Any RmlUi document (menus, HUDs, the settings panel) shows a sprite by
name:

```html
<img src="sprite:ICON_Map_Fire"/>
<img src="sprite:INTERFACE_Apocalypse_HUD/SPR_Background_Bar_Horiz"/>   <!-- when two packs share a name -->
```

The name is looked up in the sprite folder (`kke::resolveSprite`), so a
document doesn't need to know where the pack sits. An unknown name logs a
warning and draws nothing. In a baked download the sprites are cooked
like the rest of the art and load the same way.

## What each game needs

Every game runs without its packs: blocks and plain colours stand in, and
the game says which pack it missed. With the packs:

| Game | Packs |
|---|---|
| racing | POLYGON_Street_Racer, POLYGON_Nature |
| flying_demo | POLYGON_StuntPlane, POLYGON_Town |
| party | POLYGON_City_Characters |
| tennis | POLYGON_Shops (racket and ball), Universal Animation Library 2 |
| duel | Universal Animation Library 2, Universal Animation Library (optional: the kick, dodges and body hits) |
| goblin_horde | POLYGON_Goblin_War_Camp, POLYGON_Dungeon_Pack, POLYGON_Fantasy_Rivals, POLYGON_Fantasy_Characters, Universal Animation Library 2, Universal Animation Library (optional: kicks and spells) |
| farm_demo | POLYGON_Farm, POLYGON_Dogs, Farm Animals Animated by Quaternius |
| pet_companion | Farm Animals Animated by Quaternius, POLYGON_Town |
| platoon | POLYGON_Prototype |
| jiggle_demo | POLYGON_Prototype, POLYGON_City_Characters, POLYGON_Fantasy_Characters |
| kke_demo | POLYGON_Town, POLYGON_Nature, Universal Animation Library 2 |
| sandbox | POLYGON_Town |
| synty_demo | POLYGON_Town, POLYGON_Nature, POLYGON_Prototype, Farm Animals Animated by Quaternius |

`kke_assets needs` prints this list from the engine itself, so it is
never out of date. Games not listed use only what ships in the
repository (the mannequin, the fonts, the button glyphs).

## kke_assets: what did the engine find?

```bash
./kke_assets                      # your asset and sprite folders (as the games find them)
./kke_assets ~/Synty ~/SyntyUI    # these folders
./kke_assets needs                # what every game needs
./kke_assets needs racing         # what one game needs
```

It lists every folder with the pack it is (and whether it was recognised
by name or by its files), what kind of pack it looks like when it's not
one the games use (characters, animations, models, sprites, audio, an
Unreal project), where it belongs if it is in the wrong folder, and a
line per game with `ok` or `MISSING` for each pack. For example:

```text
Asset folder: /home/kees/Synty
  POLYGON_Street_Racer_SourceFiles_v3          POLYGON_Street_Racer; used by racing
  my planes                                    POLYGON_StuntPlane (by its files); used by flying_demo
  POLYGON_Casino_SourceFiles_v5                not a pack the games use (Models, 3104 files)
  INTERFACE_Apocalypse_HUD_Source_Sprites_v3   INTERFACE_Apocalypse_HUD; no game uses it yet; belongs in assets/sprites
  10 Ambient RPG Tracks                        not a pack the games use (Audio, 24 files); the engine doesn't load this kind of pack

What the games need:
  racing         ok POLYGON_Nature, ok POLYGON_Street_Racer
  tennis         MISSING POLYGON_Shops, ok Universal Animation Library 2
```

Run it before a bake (the bake script runs it for you): what is missing
here is missing in the download.

## Getting packs from the asset share

`tools/fetch_assets.sh` downloads packs from Kees's share into the right
folder (3D packs to `assets/synty`, `INTERFACE_*` sprite packs to
`assets/sprites`), one zip at a time. The share's link is private and
never in the repository: set `KKE_SHARE_HASH` first.

```bash
tools/fetch_assets.sh --list
tools/fetch_assets.sh POLYGON_Town INTERFACE_Apocalypse
```

## Adding a pack the games know about

When a game starts using a new pack, add a row to the table in
`engine/src/KnownPacks.cpp`: its name, kind, folder, one or two marker
files only it ships (ideally files the game loads), the games that use
it and a few words on what for. `kke_assets` and the bake's check come
from that row; add the pack to this page's table too. Then ask for the pack by that name
(`AssetCatalog::find(name, { "POLYGON_X" })`, `onlyPacks`,
`findPackFile`), never by building a path from a folder name.
