#pragma once

#include "kke/AssetCatalog.h"
#include "kke/RigidWorld.h"
#include "kke/SceneFile.h"
#include "kke/modules/ModelModule.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace kke {

// Puts a SceneFile into the world: models through ModelModule (textures
// from the pack's atlas, via AssetCatalog) and static collision in
// RigidWorld ("mesh": the model's own triangles, "box": its bounds).
struct LoadedScene {
    std::vector<ModelModule::InstanceId> instances;
    std::vector<RigidWorld::BodyId> bodies;
    std::vector<std::string> missing;   // assets not found in any installed pack
    size_t collisionTriangles = 0;
    glm::vec3 origin{0.0f};
};

// `origin` shifts the whole scene (several scenes side by side).
LoadedScene loadScene(const SceneFile& scene, const AssetCatalog& catalog, ModelModule& models, RigidWorld* world,
                      const glm::vec3& origin = glm::vec3(0.0f));
void unloadScene(LoadedScene& scene, ModelModule& models, RigidWorld* world);
// Collision only, no GPU (tools, tests, a dedicated server): loads each
// model's geometry straight from the file.
LoadedScene loadSceneCollision(const SceneFile& scene, const AssetCatalog& catalog, RigidWorld& world,
                               const glm::vec3& origin = glm::vec3(0.0f));

} // namespace kke
