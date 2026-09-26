#pragma once

#include <glm/glm.hpp>

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

    // Throws std::runtime_error naming the file and the problem.
    static SceneFile load(const std::string& path);
    static SceneFile parse(const std::string& json, const std::string& sourceName = "scene");

    // Model transform of one instance (grid cell `cell`), given the
    // asset's bounds in model space.
    static glm::mat4 placement(const SceneObject& o, const glm::vec3& boundsMin, const glm::vec3& boundsMax, glm::ivec2 cell = {0, 0});
    // How many times each asset is placed (grids counted), by name.
    std::map<std::string, int> assetsUsed() const;
    size_t instanceCount() const;
};

} // namespace kke
