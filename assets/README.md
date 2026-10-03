# Assets

What every folder here expects, the naming conventions and how packs are
found whatever their folder is called: [docs/ASSETS.md](../docs/ASSETS.md).
Run `kke_assets` to see what the engine found and what each game needs.

| Folder | What | In git? |
|---|---|---|
| `synty/` | Your 3D art packs (models, characters, animations), any vendor | No |
| `sprites/` | Your 2D packs (menu and HUD sprites, icons) | No |
| `animations/` | `UAL1_Standard.fbx`, the mannequin (CC0) | That file only |
| `prompts/xelu/` | Button glyphs (CC0) | Yes |
| `fonts/` | Noto fonts (OFL) | Yes |
| `ambience/` | Looping background sounds for moods (CC0) | Yes |
| `moods/` | Mood files: sky, sun, fog, look | Yes |
| `branding/` | Logo, icon, banner | Yes |
| `textures/` | Test images | Yes |

## Branding — `assets/branding/`

The Kreative Kompas logo and banner (Kreative Kompas's own, committed):

- `kreative-kompas-logo.svg` — the source of truth. The startup intro's 3D
  logo is built from it: run `tools/branding/make_logo_outlines.py` after
  changing it (regenerates `engine/src/LogoOutlines.inc`).
- `logo-128.png` — embedded into the engine as every window's icon
  (`engine/CMakeLists.txt`); `kk-engine.ico` + `kk-engine.rc` give the
  Windows `.exe` files the same icon.
- `logo-512.png`, `logo-1024.png`, `banner.png` — README and store art.

## Button prompts — `assets/prompts/xelu/`

Xelu's Free Controller & Key Prompts (CC0, committed): the glyphs the
engine's button prompts show for keyboard, Xbox, PlayStation, Switch,
Steam Deck and touch. See [prompts/xelu/README.md](prompts/xelu/README.md)
and docs/INPUT.md "Button prompts".

## Licensed art packs (Synty etc.) — `assets/synty/`

Paid asset packs are licensed per user and **must never be committed**
(`assets/synty/` is in `.gitignore`).

**Setup:** extract your pack(s) and put the pack folders inside
`assets/synty/`. Keep the names they came with
(`POLYGON_Street_Racer_SourceFiles_v3` is fine, so is a name of your
own: packs are recognised by their files too). Any layout works — the
engine scans for them (`kke::AssetCatalog`, `kke/KnownPacks.h`):

    assets/synty/POLYGON_Prototype/Characters/SK_Character_Dummy_Male_01.fbx
    assets/synty/POLYGON_Prototype/StaticMeshes/...
    assets/synty/POLYGON_Town/FBX/...            (Town calls it FBX — fine)
    assets/synty/<Pack>/_SourceFiles/...         (older zips — also fine)

Several packs side by side are picked up together. OBJ copies of FBX
files are ignored (FBX carries more data). Each pack's `Textures` folder
and its `*_Texture_01` atlas are found automatically.

**Or keep your packs anywhere** and point the engine at the folder that
contains them: `KKE_ASSETS_DIR=/path/to/my/synty ./synty_demo` (the older
`KKE_SYNTY_DIR` also works). Without either, the demos search
`assets/synty` upward from where you run them and from the executable's
folder, and list every place they looked if nothing is found.
