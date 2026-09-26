## Branding — `assets/branding/`

The Kreative Kompas logo and banner (Kreative Kompas's own, committed):

- `kreative-kompas-logo.svg` — the source of truth. The startup intro's 3D
  logo is built from it: run `tools/branding/make_logo_outlines.py` after
  changing it (regenerates `engine/src/LogoOutlines.inc`).
- `logo-128.png` — embedded into the engine as every window's icon
  (`engine/CMakeLists.txt`); `kk-engine.ico` + `kk-engine.rc` give the
  Windows `.exe` files the same icon.
- `logo-512.png`, `logo-1024.png`, `banner.png` — README and store art.

## Licensed art packs (Synty etc.) — `assets/synty/`

Paid asset packs are licensed per user and **must never be committed**
(`assets/synty/` is in `.gitignore`).

**Setup:** extract your pack(s) and put the pack folders inside
`assets/synty/`. Any layout works — the engine scans for them
(`kke::AssetCatalog`):

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
