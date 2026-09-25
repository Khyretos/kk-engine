Drop your engine icon here as `icon.png` (and `icon.ico` / `icon.icns` if
you have platform-specific versions for windowing/taskbar icons later).
Nothing in the build currently references this folder automatically — see
the main README's "Branding" section for where to wire it in per platform.

## Licensed art packs (Synty etc.) — `assets/synty/`

Paid asset packs are licensed per user and **must never be committed**
(`assets/synty/` is in `.gitignore`). Unzip a pack so its `_SourceFiles/`
folder sits at `assets/synty/<PackName>/_SourceFiles/`, e.g.

    assets/synty/POLYGON_Prototype/_SourceFiles/{Characters,StaticMeshes,Textures}

`games/synty_demo` finds it there (searching upward from the working
directory), or wherever the `KKE_SYNTY_DIR` environment variable points
(the folder that contains `_SourceFiles/`). FBX files are loaded directly
with `kke::loadModel` / `kke::ModelModule`; nothing needs converting.
