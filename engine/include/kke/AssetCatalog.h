#pragma once

#include <string>
#include <vector>

namespace kke {

// Finds model files in a folder of asset packs, whatever their layout —
// so "unzip your packs somewhere and point the engine at it" just works.
//
// Handles, verified against real Synty layouts:
//   root/POLYGON_Town/{Characters,FBX,OBJ,Textures}/...        (several packs side by side)
//   root/POLYGON_Prototype/_SourceFiles/{Characters,StaticMeshes,...}
//   root = a single pack folder itself
// Rules:
//   - a *pack* is a direct subfolder of root containing model files
//     (any depth); if root's subfolders are only asset-type folders
//     (Characters, Textures, FBX...) root itself is the pack
//   - .fbx wins over .obj with the same name (Synty ships both)
//   - category comes from the file-name prefix (Synty convention:
//     SK_ characters, SM_Bld_ buildings, SM_Prop_ props, SM_Env_ /
//     SM_Generic_ environment, SM_Veh_ vehicles, SM_Wep_ weapons...)
//   - textures: any folder named "Textures" (any case) in the pack; the
//     pack's default texture is its "*_Texture_01*" image if present.
//
// Pure file-system logic, no GPU — unit-tested in tests/test_asset_catalog.cpp.
struct CatalogAsset {
    std::string name;      // file name without extension, e.g. "SM_Prop_Crate_01"
    std::string path;      // full path to the file to load
    std::string pack;      // e.g. "POLYGON_Town"
    std::string category;  // e.g. "Props"
    bool skinned = false;  // name says it's a skinned character (SK_)
};

struct CatalogPack {
    std::string name;
    std::string root;
    std::vector<std::string> textureDirs;
    std::string defaultTexture; // may be empty
    size_t assetCount = 0;
};

struct AssetCatalog {
    std::vector<CatalogPack> packs;
    std::vector<CatalogAsset> assets;   // sorted by pack, category, name

    // Never throws; a missing/empty folder gives an empty catalog.
    static AssetCatalog scan(const std::string& root);

    const CatalogPack* pack(const std::string& name) const;
    // Every category present, in a stable, friendly order.
    std::vector<std::string> categories() const;
    // Assets matching all non-empty filters; `search` is a case-
    // insensitive substring of the name.
    std::vector<const CatalogAsset*> filter(const std::string& pack, const std::string& category, const std::string& search) const;
    // First asset with exactly this name (without extension), any pack.
    const CatalogAsset* find(const std::string& name) const;
};

// Where the asset folder is on this machine. Checked in order:
//   1. each environment variable in `envVars` that is set (first wins)
//   2. `relative` (e.g. "assets/synty") under the working directory and
//      up to 4 parent folders
//   3. the same under the executable's folder and its parents
// Returns "" if none exists. `searched` (optional) lists every place
// tried, so a demo can tell the user exactly where it looked.
std::string findAssetFolder(const std::string& relative, const std::vector<std::string>& envVars,
                            const std::string& executableDir = {}, std::vector<std::string>* searched = nullptr);

// Category for a file name, by the prefix rules above ("Other" if none).
std::string categoryForAssetName(const std::string& name);

} // namespace kke
