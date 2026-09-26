#pragma once

#include "kke/ModelAsset.h"

#include <string>
#include <vector>

namespace kke {

// Synty SIDEKICK characters: modular people and creatures (the Goblin
// Fighters, the Sidekick starter packs). A character is a list of parts,
// each its own skinned FBX on one shared skeleton (head, torso, arms,
// hands, legs, feet, ears, armour attachments...), plus one colour map.
// Synty's Unity tool writes that list as a small YAML file, `<Name>.sk`:
//
//   Name: GoblinFighter_01
//   Parts:
//   - Name: SK_GOBL_BASE_01_01HEAD_GO01
//     PartType: Head
//   ...
//
// The parts live under the pack's `Resources/Meshes/` (any depth) and the
// colour map is `<Name>/Textures/T_<Name>ColorMap.png` next to the .sk
// file. loadSidekickCharacter() reads the .sk file, loads every part and
// merges them into one skinned model, so a goblin is one ModelModule
// instance: one pose, one skinning pass, one ragdoll.
//
// Pure CPU (no GPU types); tests/test_sidekick.cpp.

struct SidekickPart {
    std::string name;      // SK_GOBL_FIGT_01_10TORS_GO01
    std::string type;      // Torso, Head, AttachmentBack, ...
    std::string file;      // resolved FBX path ("" = not found)
};

struct SidekickCharacter {
    std::string name;
    std::vector<SidekickPart> parts;
    std::string colorMap;  // resolved path, or "" when missing
};

// Reads a .sk file (YAML) and finds each part's FBX under `meshRoots`
// (searched recursively, by file name). No `meshRoots` = the pack's own
// `Resources/Meshes`, found by walking up from the .sk file. Throws
// std::runtime_error when the file can't be read or names no parts.
SidekickCharacter readSidekickCharacter(const std::string& skFile, const std::vector<std::string>& meshRoots = {});

struct SidekickLoadOptions {
    // Part types left out (small or hidden details): e.g. { "Tongue",
    // "Teeth", "EyebrowLeft", "EyebrowRight" } for a crowd seen from afar.
    std::vector<std::string> skipTypes;
};

// Loads and merges a character's parts into one skinned model: bones
// unified by name (the first part's skeleton, extended by any bone a
// later part adds), meshes appended with their joint indices remapped,
// every material given the colour map. Parts whose FBX is missing are
// skipped (listed in *missing). Throws when no part loads.
ModelData loadSidekickCharacter(const SidekickCharacter& character, const SidekickLoadOptions& options = {},
                                std::vector<std::string>* missing = nullptr);

// Merges skinned models that share a skeleton by bone name (what
// loadSidekickCharacter does with the parts). Meshes whose materials
// match by name and texture are kept as separate mesh parts but share
// one material. Rest pose and inverse binds come from the first model
// that has each bone.
ModelData mergeSkinnedModels(const std::vector<ModelData>& models);

// Joins mesh parts that share a material into one (fewer draws and one
// skinning loop per material). Vertices keep their order within a part.
void joinMeshesByMaterial(ModelData& model);

} // namespace kke
