#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace kke {

// A level made of pack assets, as JSON (scenes/*.scene.json): what to put
// where, and how it collides. It names assets, never ships them: the
// Synty packs stay in the git-ignored assets/synty/, found by name through
// AssetCatalog, so anyone with the same packs gets the same scene.
// `assetsUsed()` lists them for reproducing a scene.
//
//   {
//     "format": "kke.scene", "version": 1,
//     "name": "Town block", "description": "...",
//     "packs": ["PolygonTown_Source_Files"],
//     "spawn": { "position": [0, 0, 8], "yaw": 180 },
//     "objects": [
//       { "asset": "SM_Env_Fence_White_Straight_01", "position": [2, 0, 1], "yaw": 90,
//         "collision": "box" },
//       { "asset": "SM_Env_Grass_01", "grid": { "count": [4, 4], "step": [5, 5] },
//         "position": [-10, 0, -10], "collision": "none" }
//     ]
//   }
//
// Placement is by bounds (Synty pivots vary: buildings sit on a corner,
// props on their centre): centred on `position` in X/Z with the bottom at
// its Y, unless "pivot": true keeps the asset's own origin. "grid"
// repeats an object on an X/Z grid (floors, fences, rows of trees).
//
// Optional, written by the sandbox editor (games/sandbox) and read by
// every game: per object "pack" (only where the scene's "packs" order
// would pick another pack's model of the same name), "texture" (a texture variant's file name from
// the asset's pack), "breakable" (a material the game can turn it into a
// FEMFX breakable with: "wood", "stone", "glass", "ceramic", "metal") and
// "fractureSeed"; per scene "worldSeed", "sun", "ambient" and "lights"
// (point lights, position relative to the scene):
//
//     "sun": { "direction": [-0.4, -1, -0.3], "color": [1, 0.98, 0.92], "intensity": 1 },
//     "ambient": [0.25, 0.25, 0.25],
//     "lights": [ { "position": [3, 2.5, 0], "color": [1, 0.7, 0.4], "intensity": 2 } ]
//
// save()/toJson() write the same format back (defaults left out), so a
// scene round-trips: load -> edit -> save -> load gives the same scene.
struct SceneObject {
    enum class Collision { Mesh, Box, None };
    std::string asset;              // file stem, e.g. "SM_Bld_Shop_01"
    glm::vec3 position{0.0f};
    float yaw = 0.0f;               // degrees around +Y
    glm::vec3 scale{1.0f};
    Collision collision = Collision::Mesh;
    bool pivot = false;             // true: place by the asset's own origin
    glm::ivec2 gridCount{1, 1};
    glm::vec2 gridStep{0.0f};
    std::string pack;               // "" = the scene's "packs" order decides; else this pack first
    std::string texture;            // texture variant file name, "" = the model's own
    std::string breakable;          // "" = static, else a break material name (see above)
    uint32_t fractureSeed = 0;      // 0 = let the game pick
};

struct SceneLight {
    glm::vec3 position{0.0f};       // point light, relative to the scene origin
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
};

struct SceneFile {
    std::string name, description;
    std::vector<std::string> packs;
    glm::vec3 spawn{0.0f};
    float spawnYaw = 0.0f;
    // Optional flat ground under everything ("ground": {"size": [w, d],
    // "color": [r, g, b]}): a guaranteed floor below art that has gaps.
    glm::vec2 groundSize{0.0f};
    glm::vec3 groundColor{0.35f, 0.42f, 0.3f};
    std::vector<SceneObject> objects;
    uint32_t worldSeed = 0;         // 0 = not set
    // Sun and ambient are optional: has* false = the game keeps its own.
    bool hasSun = false;
    glm::vec3 sunDirection{-0.4f, -1.0f, -0.3f}; // towards the ground, normalized on load
    glm::vec3 sunColor{1.0f, 0.98f, 0.92f};
    float sunIntensity = 1.0f;
    bool hasAmbient = false;
    glm::vec3 ambient{0.15f};
    std::vector<SceneLight> lights;

    // Throws std::runtime_error naming the file and the problem.
    static SceneFile load(const std::string& path);
    static SceneFile parse(const std::string& json, const std::string& sourceName = "scene");
    // The scene as kke.scene v1 JSON text. save() throws std::runtime_error
    // naming the file if it can't be written; it writes to a temporary
    // file first and renames, so a crash mid-save never leaves half a level.
    std::string toJson() const;
    void save(const std::string& path) const;

    // Model transform of one instance (grid cell `cell`), given the
    // asset's bounds in model space.
    static glm::mat4 placement(const SceneObject& o, const glm::vec3& boundsMin, const glm::vec3& boundsMax, glm::ivec2 cell = {0, 0});
    // How many times each asset is placed (grids counted), by name.
    std::map<std::string, int> assetsUsed() const;
    size_t instanceCount() const;
};

} // namespace kke
