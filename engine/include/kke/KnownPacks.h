#pragma once

#include <string>
#include <vector>

namespace kke {

// Which art pack a folder holds, whatever the folder is called.
//
// People keep packs under the names they downloaded them with
// ("POLYGON_Street_Racer_SourceFiles_v3"), shorten them ("Street Racer"),
// or nest them ("Synty/POLYGON_Town"). Games ask for a pack by one name
// ("POLYGON_Street_Racer"), so the engine recognises a pack two ways:
//
//   1. by name: packKey() drops the download suffix, a version, case,
//      spaces and punctuation, so every spelling above gives the same key
//   2. by contents: a folder whose name isn't a known pack is searched
//      for files only that pack ships (its "markers", from the table in
//      KnownPacks.cpp), so "my racing stuff" holding
//      PolygonStreetRacer_Veh_Tex_01_Race_Purple.png is the Street Racer
//
// Folders that are neither keep their own name (packBaseName()).
// docs/ASSETS.md is the guide; tools/assets (kke_assets) prints what a
// folder holds and what each demo needs.
struct KnownPack {
    std::string name;                 // what games ask for, e.g. "POLYGON_Street_Racer"
    std::string kind;                 // Characters, Environment, Vehicles, Props, Animations, Effects, Sprites
    std::string folder;               // where it goes: "assets/synty" or "assets/sprites"
    std::vector<std::string> markers; // file (or folder) names only this pack ships
    std::vector<std::string> usedBy;  // the games that load it (executable names)
    std::string usedFor;              // one line for people: what the games use it for
};

// Every pack the engine's games know about, in a stable order.
const std::vector<KnownPack>& knownPacks();

// The known pack a name or folder name means, or nullptr.
const KnownPack* knownPack(const std::string& nameOrFolder);

// "POLYGON_Street_Racer_SourceFiles_v3", "PolygonStreetRacer",
// "polygon street racer v2" -> "polygonstreetracer". Two names are the
// same pack when their keys are equal.
std::string packKey(const std::string& nameOrFolder);
bool samePack(const std::string& a, const std::string& b);

struct PackIdentity {
    enum class How { Name, Contents, Unknown };
    std::string name; // the known pack's name, or packBaseName(folder) when Unknown
    How how = How::Unknown;
};

// What pack `folder` holds: by its name, else by the marker files in it
// (searched at any depth; the answer is remembered for the process, so
// a folder is searched once), else Unknown with its own name.
PackIdentity identifyPackFolder(const std::string& folder);

// The folder directly under `root` that holds `pack` (by name, then by
// contents), also one level down when packs sit in a folder of their own
// ("root/Synty/POLYGON_Town"). Empty if there is none.
std::string findPackFolder(const std::string& root, const std::string& pack);

// The first file called `fileName` (any depth, sorted) inside `pack`
// under `root`, e.g. findFileInPack(assets, "Universal Animation Library 2",
// "UAL2.fbx"). Empty if the pack or the file isn't there.
std::string findFileInPack(const std::string& root, const std::string& pack, const std::string& fileName);

// The same, in the asset folder on this machine (KKE_ASSETS_DIR,
// KKE_SYNTY_DIR, then assets/synty: findAssetFolder()).
std::string findPackFile(const std::string& pack, const std::string& fileName, const std::string& executableDir = {});

// What a folder of files looks like, for packs the table doesn't know
// (kke_assets prints this). Counts files by kind, at any depth.
struct PackContents {
    size_t models = 0;     // .fbx .obj .gltf .glb
    size_t skinned = 0;    // models named SK_*
    size_t animations = 0; // models named A_* or in an "Animations" folder
    size_t images = 0;     // .png .tga .jpg .jpeg .psd
    size_t audio = 0;      // .wav .ogg .mp3 .flac
    size_t fonts = 0;      // .ttf .otf
    size_t sidekick = 0;   // .sk (Synty Sidekick parts)
    size_t engineOnly = 0; // .uasset .umap .unitypackage .prefab: files only Unreal/Unity read
    size_t files = 0;
    // "Characters", "Animations", "Models", "Sprites", "Audio", "Fonts",
    // "Unreal/Unity project" or "Empty" (the largest share wins).
    std::string kind() const;
    // Where a pack like this goes: "assets/synty", "assets/sprites", or
    // "" when the engine doesn't load this kind of pack yet.
    std::string folder() const;
};
PackContents analyzePackFolder(const std::string& folder);

} // namespace kke
