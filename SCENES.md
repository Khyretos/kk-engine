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
- Tests: `SceneTrails.*` in `tests/test_locomotion.cpp` run both trails
  headless. They skip when the packs are missing (CI).

## Getting the packs

`tools/fetch_assets.sh` downloads packs from the asset share and extracts
them into `assets/synty/` (`--list` shows what's there; one zip at a time,
deleted after extracting; `.unitypackage` files are unpacked to their real
paths). Example: `tools/fetch_assets.sh POLYGON_Town ANIMATION_`.

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
