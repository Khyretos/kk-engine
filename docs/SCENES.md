# SCENES.md — levels built from asset packs

`scenes/*.scene.json` are levels made of Synty pack assets. The files name
assets and never contain them: the packs are paid, so they stay in the
git-ignored `assets/synty/` (one folder or symlink per pack) and are found
by file name through `AssetCatalog`. Anyone with the same packs gets the
same scene.

- Format and placement rules: `engine/include/kke/SceneFile.h`.
- Loading (render + collision): `engine/include/kke/SceneLoader.h`.
- In `kke_demo`: the **Scenes** panel has a Go button per scene and "Back to
  the course". `KKE_SCENE=town_block ./kke_demo` starts in one.
  `KKE_DEMO_AUTOPILOT=1` sprints from the spawn toward -Z and vaults or
  climbs whatever is in the way.
- Building one: the sandbox (`games/sandbox`, HISTORY.md "Sandbox") saves
  kke.scene files, by default `scenes/sandbox.scene.json`, with spawn,
  sun, ambient, point lights, per-object collision, texture variants and
  breakable materials. `SceneFile::save()` writes the same format back
  (short floats, one line per vector, defaults left out), so saving a
  loaded scene changes nothing (`SceneFile.SavesAndLoadsBackTheSameScene`).
- Tests: `SceneTrails.*` in `tests/test_locomotion.cpp` run both trails
  headless, and the town block again after a save round trip. They skip
  when the packs are missing (CI).

## Getting the packs

`tools/fetch_assets.sh` downloads packs from the asset share and extracts
them into `assets/synty/` (`--list` shows what's there; one zip at a time,
deleted after extracting; `.unitypackage` files are unpacked to their real
paths). Example: `tools/fetch_assets.sh POLYGON_Town ANIMATION_`.
It needs `KKE_SHARE_HASH` (the share's private link hash, never committed).

The startup intro (kke::LogoIntro) uses no packs: its logo is built from
`assets/branding/kreative-kompas-logo.svg`, which is Kreative Kompas's own
and committed.

## Reproducing a scene

1. Put the pack's source folder in `assets/synty/` under its own name (a
   symlink works): `assets/synty/PolygonTown_Source_Files/...`.
2. Run `kke_demo` and pick the scene. Missing assets are logged by name and
   the rest still loads.

## Town block (`town_block.scene.json`)

Pack: **POLYGON Town** (`PolygonTown_Source_Files`). 154 instances of 29
assets. A street with a road and a speed bump, three houses and a shop on
the north side, a house and deck on the south side, a picket fence with a
gate, hedges, a wooden back fence, boxes, barrels, a cart, lamps and trees.
Spawn faces the picket fence, which is a vault.

SM_Bld_House_Deck_01, SM_Bld_House_Deck_Rail_01, SM_Bld_House_Preset_02,
SM_Bld_House_Preset_03, SM_Bld_House_Preset_05, SM_Bld_Shop_01,
SM_Bld_Shop_Concrete_01, SM_Env_Fence_White_Gate_01,
SM_Env_Fence_White_Straight_01 ×8, SM_Env_Fence_Wood_Straight_01 ×20,
SM_Env_Grass_01 ×80, SM_Env_Grass_Patch_04, SM_Env_Hedge_01,
SM_Env_Hedge_03, SM_Env_Path_01 ×10, SM_Env_Road_01 ×10,
SM_Env_Road_SpeedBump_01, SM_Env_Tree_01, SM_Env_Tree_02, SM_Env_Tree_03,
SM_Env_Tree_Pine_01, SM_Env_Tree_Tall_01, SM_Env_Tree_Tall_02,
SM_Prop_Barrel_01 ×2, SM_Prop_CardboardBox_01, SM_Prop_CardboardBox_02,
SM_Prop_CardboardBox_03, SM_Prop_Cart_01, SM_Prop_LampStanding_01 ×2

## Forest trail (`forest_trail.scene.json`)

Pack: **POLYGON Nature** (`POLYGON_Nature_Source_Files`). 57 instances of
37 assets. A trail along -Z from a camp: a log to step over, a broken
pillar, a stone wall, a stump and a fence to vault, a rock shelf to climb,
then a curved bridge, ruins and rocks too big to get over. Trees and plants
line it; two mountains close the view.

SM_Plant_Bush_01 ×2, SM_Plant_Bush_03 ×2, SM_Plant_Fern_01,
SM_Plant_Fern_02 ×2, SM_Plant_Flowers_01, SM_Plant_Mushrooms_01,
SM_Prop_Bridge_Curved_01, SM_Prop_CampFire_01, SM_Prop_Chest_Wood_01,
SM_Prop_Fence_01 ×3, SM_Prop_Pillar_Arch_Moss_01, SM_Prop_Pillar_Broken_01,
SM_Prop_Pillar_Broken_Moss_02, SM_Prop_Pillar_Moss_01, SM_Prop_StoneWall_01,
SM_Prop_TorchStick_01 ×2, SM_Rock_Boulder_01, SM_Rock_Cluster_Large_02,
SM_Rock_Pile_04, SM_Rock_Rounded_01, SM_Rock_Small_01, SM_Rock_Tile_03,
SM_Rock_Wall_01, SM_Terrain_Ground_Mound_Large_01 ×2,
SM_Terrain_Mountain_01, SM_Terrain_Mountain_02,
SM_Terrain_Rubble_Pebbles_01 ×6, SM_Tree_Birch_01 ×2, SM_Tree_Birch_02 ×2,
SM_Tree_Birch_03 ×2, SM_Tree_Log_01, SM_Tree_Pine_01 ×2, SM_Tree_Pine_02 ×2,
SM_Tree_Pine_Small_01 ×2, SM_Tree_PolyPine_01 ×2, SM_Tree_Round_02 ×2,
SM_Tree_Stump_01

## Showcase course art (`games/showcase/course_art.scene.json`)

Packs: **POLYGON Nature** and **POLYGON Town**. 108 instances of 37 assets,
placed on `kke_demo`'s course itself (not a separate level), around the
stations: a rock heap, dead trees, ash and torches at the lava; reeds, a
bench, deckchairs, a lamp and a tree at the pool; barrels, boxes, a cart
and a shed at the breaking yard; hedges along the north wall; pines behind
the trick wall; pines and mountains beyond the walls, houses, a shop and a
church to the south and east. It loads only when every asset is installed
(otherwise the course stays plain boxes); `KKE_COURSE_ART=0` leaves it out.

POLYGON Nature: SM_Plant_Bush_01 ×2, SM_Plant_Fern_01 ×4,
SM_Plant_Flowers_01 ×6, SM_Plant_Grass_03 ×15, SM_Plant_Reeds_01 ×2,
SM_Plant_Reeds_02, SM_Prop_TorchStick_01 ×2, SM_Rock_Pile_01,
SM_Rock_Pile_Curved_01, SM_Rock_Small_01 ×2, SM_Terrain_DustPile_Long_01,
SM_Terrain_DustPile_Small_01, SM_Terrain_Mountain_01,
SM_Terrain_Mountain_02, SM_Terrain_Mountain_03, SM_Tree_Birch_01 ×2,
SM_Tree_Dead_01, SM_Tree_Dead_02, SM_Tree_Pine_01 ×3,
SM_Tree_Pine_Large_01 ×16, SM_Tree_PolyPine_01 ×18, SM_Tree_Round_01

POLYGON Town: SM_Bld_Church_01, SM_Bld_GardenShed_01,
SM_Bld_House_Preset_01, SM_Bld_House_Preset_04, SM_Bld_Shop_02,
SM_Env_Hedge_01 ×7, SM_Env_Tree_Tall_01 ×2, SM_Prop_Barrel_01 ×2,
SM_Prop_CardboardBox_01, SM_Prop_CardboardBox_03, SM_Prop_Cart_01,
SM_Prop_Deckchair_01 ×2, SM_Prop_ParkBench_01, SM_Prop_Pool_Float_Ring_01,
SM_Prop_Streetlamp_01 ×2

Two assets looked wrong on the engine's loader and were swapped out:
SM_Plant_FlowerPatch_01 (flat squares of the atlas) and
SM_Tree_Willow_Medium_01 (untextured, grey leaves).

## Farm demo (`scenes/farm.scene.json`)

Used by `games/farm_demo` (the dog on a farm, AI core). The level was
generated by a script and is 60 objects from **POLYGON Farm** on a
170 × 170 m ground, with the spawn at (0, 0, 8) facing -Z.

- Buildings: `SM_Bld_Barn_01`, `SM_Bld_Silo_01`, `SM_Bld_Farmhouse_01`.
- Props: `SM_Prop_Windmill_01`, `SM_Prop_Well_01`, `SM_Veh_Tractor_01`,
  `SM_Prop_Trough_01` ×3, `SM_Prop_Hay_Pile_01`,
  `SM_Prop_Hay_Bale_Round_01` ×4, `SM_Prop_Hay_Bale_Square_01` ×2.
- Fences: `SM_Prop_Fence_Wood_01` ×6 (sheep meadow),
  `SM_Prop_Fence_Wood_Round_01` ×5 (pig pen), `SM_Prop_Fence_Wire_01` ×5
  (cow pasture).
- Trees: `SM_Env_Tree_Apple_Grown_01` ×2, `SM_Env_Tree_Pear_Grown_01`,
  `SM_Env_Tree_Large_01`, `SM_Generic_Tree_01`..`04` (×3 each),
  `SM_Generic_Tree_Patch_01`/`02` (×2 each).
- Horizon: `SM_Generic_Mountains_Grass_01` ×8, scaled (2, 1.5, 2).
- Animals, loaded in code rather than from the scene file:
  - **POLYGON Dogs** `Unity_SK_Animals_Dog_01`, keeping only the
    `GermanShepherd_*` or `Fox_*` material parts. Clips from
    `FBX/Animations`: Locomotion Standing/Walking/Running;
    Actions_Standing Eat/Drink/Sniff/Bark/TailWag; Attack Bite;
    Sleep Idle.
  - **Farm Animals Animated by Quaternius** (CC0): `Sheep` (scale 0.23),
    `Cow` (0.3), `Pig` (0.2) and `Horse` (0.28), with their own clips.
    Sheep and pigs only have Idle and Jump, so they hop.

## Things the packs taught the engine

- **Same names, different packs.** POLYGON City and POLYGON Town both
  have SM_Bld_Shop_01, SM_Env_Road_01, SM_Env_Grass_01 and more, as
  different models. A scene's `"packs"` list now decides which one loads
  (`AssetCatalog::find(name, packs)`); before that, the town was quietly
  built from some City models.
- **Mixed units.** Some City FBX files say centimeters but hold meters and
  load 100x too small. `loadModel` scales an unskinned model from a
  centimeter file up 100x when it comes out under 10 cm on every axis
  (`ModelLoadOptions::fixUnitMismatch`).
- **Textures the FBX doesn't name.** Synty meshes are UV-mapped onto the
  pack atlas even where the material names no texture (Town roofs,
  fences), so those get the atlas, unless the material has a deliberate
  colour (blue glass). A few meshes use their own image instead: the road
  points at an artist's missing .psd but is meant for
  `PolygonTown_Road_01.png`, and Nature trees' "Trunk"/"Leave" materials
  are meant for `Birch_Trunk_Texture.png` and `Leaves_Pine_Texture.png` /
  `Leaves_Generic_Texture.png`. `AssetCatalog::namedTexture` finds these
  by the asset's and the material's names. Leaves are cutout cards, so
  the model shader now discards transparent texels.
- **Open and flipped meshes.** Ray casts hit back faces too, like the
  character does, so the probe can't see through a wall the capsule
  can't pass.
- **Seams.** A fence is a row of panels. The probe casts from both
  shoulders as well as the middle, because a centre ray slips through the
  gap between two panels.
- **Uneven tops.** Rocks have bevelled rims and lumpy tops. The probe
  looks a second time further in when the rim is sloped, and a climb
  stands on the ground under the target spot, lifted a little if the
  ground rises under the capsule's rim.
- **Sheets, not solids.** `SM_Terrain_RiverSide_01` is a surface whose
  near edge hangs at head height with nothing below it, so it can't be
  climbed; the trail uses `SM_Rock_Tile_03` instead.

## Jiggle demo, body scene (`games/jiggle_demo`)

Not a scene file: the demo loads one character by name and reshapes it in
code (`kke::addHumanoidSoftTissue`: bust +6 cm, glutes +5 cm, hips +3 cm,
two breast and two glute bones, belly and thigh skin zones).

- Character: **POLYGON Fantasy Characters**, `SK_Character_Female_Gypsy`
  (falls back to `SK_Character_Female_Peasant_01`, then POLYGON City
  Characters `SK_Character_HipsterGirl`; `KKE_JIGGLE_CHARACTER` picks any).
  Texture: the pack's own atlas via `packLoadOptions`.
- Clips: Quaternius Universal Animation Library,
  `assets/animations/UAL1_Standard.fbx` (Idle_Loop, Walk_Loop,
  Jog_Fwd_Loop, Sprint_Loop, Jump_Start, Jump_Loop, Jump_Land), retargeted.
- The jelly scene uses no assets.

Put the pack in `assets/synty/POLYGON_Fantasy_Characters/` (a symlink to
the shared cache works) or set `KKE_ASSETS_DIR`.

## Duel (`games/duel`)

No Synty packs. Fighters: Quaternius Universal Animation Library (CC0), the
mannequin and clips from `assets/animations/UAL1_Standard.fbx` (Idle_Loop,
Walk_*_Loop, Punch_Cross, Hit_Chest, Hit_Head, Death01, Roll, Dance_Loop)
and, when present, UAL 2's melee clips (`UAL2.fbx`: Melee_Hook,
Melee_Uppercut, Melee_Knee, Hit_Knockback, LayToIdle, Walk_Fwd/Bwd/L/R_Loop)
from `assets/animations/` or `KKE_ASSETS_DIR/Universal Animation Library 2/Unity/`.
The ring, gym and benches are boxes built in code.

## Goblin Horde (`games/goblin_horde`)

Not a scene file: the fort, trees and rubble are boxes built in code.

- Goblins: **SIDEKICK Goblin Fighters**, `GoblinFighter_01` to `_05`
  (`.sk` part lists under `SidekickCharacters/Characters/GoblinFighters/`,
  parts from `Resources/Meshes`, colour maps `T_GoblinFighter_0NColorMap`).
  Tongue, teeth and eyebrow parts are left out. Simplified to 20% at load.
- Goblin clips: **ANIMATION Goblin Locomotion**, Sidekick versions
  (`Animations/Sidekick/Neutral/...`): `A_MOD_GBL_Idle_Standing_Neut`,
  `Walk_F_Neut`, `Run_F_Neut`, `Sprint_F_Neut`, `Idle_Fidget_Swipe_Neut`
  (the attack), `Idle_Fidget_Menacing_Neut` (waiting their turn).
- The king: **POLYGON Fantasy Characters**, `SK_Character_Male_King`
  (falls back to `SK_Character_Male_Rouge_01`, `SK_Character_Male_Peasant_01`,
  then the UAL mannequin) with `SM_Prop_SwordOrnate_01` in his right hand.
- The king's clips: Quaternius UAL 1 (`Sword_Idle`, `Walk_Loop`,
  `Jog_Fwd_Loop`, `Sprint_Loop`, `Roll`, `Hit_Chest`, `Death01`) and UAL 2
  (`Sword_Regular_A`, `Sword_Regular_B`, `Sword_Heavy_A`, `Sword_Block`,
  `Hit_Knockback`, `LayToIdle`), retargeted.

Put the packs in `assets/synty/` (symlinks to the shared cache work) or set
`KKE_ASSETS_DIR`.
