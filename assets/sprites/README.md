# Sprites: `assets/sprites/`

Your 2D packs go here: menu and HUD sprites, icons, cursors (Synty's
`INTERFACE_*` packs, icon packs, your own images). Everything in this
folder except this file is ignored by git, because bought packs are
licensed to you and must never be committed.

Extract packs as they came, folder names and all:

    assets/sprites/INTERFACE_Apocalypse_HUD_Source_Sprites_v3/Source_Sprites/Sprites/...
    assets/sprites/my_icons/heart.png
    assets/sprites/coin.png

Menus show one by its file name: `<img src="sprite:ICON_Map_Fire"/>`.
Or keep them elsewhere and set `KKE_SPRITES_DIR`. `kke_assets` lists what
the engine found. The full guide is [docs/ASSETS.md](../../docs/ASSETS.md).
