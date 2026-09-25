Drop your engine icon here as `icon.png` (and `icon.ico` / `icon.icns` if
you have platform-specific versions for windowing/taskbar icons later).
Nothing in the build currently references this folder automatically — see
the main README's "Branding" section for where to wire it in per platform.

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
