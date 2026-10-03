#pragma once

#include <string>
#include <vector>

namespace kke {

// 2D images for menus and HUDs (Synty INTERFACE packs, icon packs, your
// own), found in the sprite folder: KKE_SPRITES_DIR, else assets/sprites
// (searched like assets/synty, see findAssetFolder). docs/ASSETS.md.
//
//   assets/sprites/INTERFACE_Apocalypse_HUD_Source_Sprites_v3/Source_Sprites/Sprites/Icons_Map/ICON_Map_Fire.png
//   assets/sprites/my_icons/heart.png
//   assets/sprites/coin.png                       (loose images: pack "sprites")
//
// Any layout works. A sprite's name is its file name without extension;
// its pack is the top folder (known packs by their name, KnownPacks.h);
// its category is the folder it's in ("Icons_Map"). RmlUi documents show
// one with <img src="sprite:ICON_Map_Fire"/> (or "sprite:PACK/NAME" when
// two packs share a name); the UI looks it up with resolveSprite().
struct Sprite {
    std::string name;     // "ICON_Map_Fire"
    std::string path;     // full path to the image
    std::string pack;     // "INTERFACE_Apocalypse_HUD"
    std::string category; // "Icons_Map"
};

struct SpriteCatalog {
    std::vector<std::string> packs; // sorted
    std::vector<Sprite> sprites;    // sorted by pack, category, name

    // Never throws; a missing folder gives an empty catalog.
    static SpriteCatalog scan(const std::string& root);
    // First sprite with exactly this name, from `pack` when given (any
    // spelling of the pack's name, samePack()), else from any pack.
    const Sprite* find(const std::string& name, const std::string& pack = {}) const;
};

// The sprite folder on this machine, or "" (KKE_SPRITES_DIR, then
// assets/sprites under the working and executable folders).
std::string findSpriteFolder(const std::string& executableDir = {});

// "sprite:NAME" or "sprite:PACK/NAME" -> the image's path, from a
// catalog of findSpriteFolder() scanned on first use. Empty when the
// reference isn't a sprite reference or nothing matches.
std::string resolveSprite(const std::string& reference, const std::string& executableDir = {});

// True for "sprite:..." references.
bool isSpriteReference(const std::string& reference);

} // namespace kke
