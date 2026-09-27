#pragma once

#include "kke/AssetCatalog.h"
#include "kke/Mesh.h"
#include "kke/Module.h"
#include "kke/RigidWorld.h"
#include "kke/modules/ModelModule.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace kke {
class Application;
class DynamicMeshRenderer;
class RigidBodyModule;
} // namespace kke

namespace command_kit {

// One box as 24 flat-shaded vertices.
void appendBox(const glm::vec3& center, const glm::vec3& half, const glm::vec3& color, std::vector<kke::Vertex>& v,
               std::vector<uint32_t>& idx);

// The ground, and art from the asset packs when they are on this machine
// (assets/synty or KKE_ASSETS_DIR; never committed). Every model a game
// asked for is remembered, so it can list what it used (docs/SCENES.md).
class Scenery {
public:
    Scenery(kke::Application& app, kke::ModelModule& models, kke::RigidBodyModule& rigid);
    ~Scenery();

    // A flat field `half` metres each way from the origin, with a Jolt floor.
    void ground(float half, const glm::vec3& color);
    bool hasPacks() const { return !m_catalog.assets.empty(); }
    const kke::AssetCatalog& catalog() const { return m_catalog; }

    // Loads a pack model by name (any pack; `packs` first). 0 if missing.
    kke::ModelModule::ModelId model(const std::string& name, const std::vector<std::string>& packs = {}, bool animations = false);
    // Places one; `collide` adds a static box from its bounds. 0 if missing.
    kke::ModelModule::InstanceId place(const std::string& name, const glm::vec3& pos, float yawDegrees, float scale = 1.0f,
                                       bool collide = true, const std::vector<std::string>& packs = {});
    // A coloured block with a static collider (the fallback for missing art, or level geometry).
    void block(const glm::vec3& center, const glm::vec3& half, const glm::vec3& color, bool collide = true);

    const std::vector<std::string>& used() const { return m_used; } // asset names, in first-use order
    void logUsed(const char* who) const;

    void render(const kke::RenderContext& ctx);
    void renderShadow(const kke::ShadowRenderContext& ctx);

private:
    kke::Application& m_app;
    kke::ModelModule& m_models;
    kke::RigidBodyModule& m_rigid;
    kke::AssetCatalog m_catalog;
    std::vector<std::string> m_used;
    std::vector<kke::Vertex> m_blockVerts;
    std::vector<uint32_t> m_blockIdx;
    bool m_blocksDirty = false;
    std::unique_ptr<kke::DynamicMeshRenderer> m_ground, m_blocks;
    std::vector<kke::RigidWorld::BodyId> m_bodies;
};

} // namespace command_kit
