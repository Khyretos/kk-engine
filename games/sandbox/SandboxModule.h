#pragma once

#include "kke/AssetCatalog.h"
#include "kke/Capabilities.h"
#include "kke/Module.h"
#include "kke/Picking.h"
#include "kke/Ragdoll.h"
#include "kke/modules/DebugDrawModule.h"
#include "kke/modules/ModelModule.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace kke_sandbox {

// A mini level editor / toy box over whatever asset packs are on disk:
// browse them, place pieces on a snapping grid, select / move / rotate /
// duplicate / delete them, save and load the layout, and play with them —
// ragdoll characters, turn props into breakable physics objects, throw
// balls at everything.
//
// Deliberately written against engine building blocks only (AssetCatalog,
// ModelModule, DebugDrawModule, Picking, IRagdollPhysics, PhysicsModule),
// so it doubles as the reference for how a game uses them. The physics
// parts are optional: without FEMFX it is still a working level editor.
class SandboxModule : public kke::Module {
public:
    const char* name() const override { return "Sandbox"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;
    void renderUi() override;
    void onEvent(const SDL_Event& event) override;

    // Scan a folder of packs (also used by the "Use this folder" field).
    void openAssetFolder(const std::string& folder);
    bool saveLayout(const std::string& path);
    bool loadLayout(const std::string& path);
    // Engine debug panels (Physics, Camera, Stats...) hidden behind one
    // toggle (F1) so the sandbox's own UI stays readable.
    void setEnginePanels(std::vector<kke::Module*> panels);

private:
    enum class Tool { Select, Place, Shoot };

    struct Object {
        uint32_t id = 0;
        std::string asset;        // catalog name, e.g. "SM_Prop_Crate_01"
        glm::vec3 position{0.0f}; // bottom-center of its bounds
        float yawDegrees = 0.0f;
        kke::ModelModule::ModelId model = 0;
        kke::ModelModule::InstanceId instance = 0;
        bool character = false;
        std::string texture;      // texture variant path, "" = the model's own
        // Ragdoll (characters)
        kke::IRagdollPhysics::RagdollHandle ragdoll = 0;
        kke::RagdollDesc ragdollDesc;
        kke::RagdollSkinBinding binding;
        // Breakable physics proxy (props): while set, the mesh is hidden
        // and FEMFX simulates a box of the prop's size instead.
        uint32_t proxy = 0;
    };

    kke::ModelModule::ModelId loadAsset(const std::string& name);
    glm::mat4 objectTransform(const kke::ModelData& model, const glm::vec3& position, float yawDegrees) const;
    Object* spawnObject(const std::string& asset, const glm::vec3& position, float yawDegrees);
    void removeObject(uint32_t id);
    void clearAll();
    Object* find(uint32_t id);
    void worldBounds(const Object& o, glm::vec3& mn, glm::vec3& mx) const;

    kke::Ray mouseRay() const;
    // Ground-or-stack point under the mouse (ignores `ignoreId`).
    bool placementPoint(glm::vec3& out, uint32_t ignoreId) const;
    uint32_t pickObject() const;

    void beginPlacing(const std::string& asset, float yawDegrees, uint32_t movingId = 0);
    void cancelPlacing();
    void commitPlacement(bool keepPlacing);

    void ragdoll(Object& o, const glm::vec3& push);
    void standUp(Object& o);
    void makeBreakable(Object& o);
    void restoreProp(Object& o);
    void throwBall();

    void applyLook();                   // overlay + variants from the Look settings
    const kke::CatalogPack* packOf(const std::string& asset) const;
    void lookUi();
    void assetBrowserUi();
    void inspectorUi();
    void folderNotFoundUi();

    kke::Application* m_app = nullptr;
    kke::ModelModule* m_models = nullptr;
    kke::DebugDrawModule* m_debug = nullptr;
    kke::IRagdollPhysics* m_ragdolls = nullptr;
    bool m_hasFemfx = false;

    // Assets
    std::string m_assetFolder;
    std::vector<std::string> m_searched;
    kke::AssetCatalog m_catalog;
    std::string m_filterPack, m_filterCategory;
    char m_search[64] = {};
    char m_folderInput[512] = {};
    std::vector<const kke::CatalogAsset*> m_filtered;
    bool m_filterDirty = true;
    std::string m_status;

    // Level
    std::vector<Object> m_objects;
    uint32_t m_nextId = 1;
    uint32_t m_selected = 0;
    uint32_t m_hovered = 0;

    // Placement
    Tool m_tool = Tool::Select;
    std::string m_placeAsset;
    float m_placeYaw = 0.0f;
    uint32_t m_movingId = 0;           // re-placing an existing object
    kke::ModelModule::InstanceId m_ghost = 0;
    kke::ModelModule::ModelId m_ghostModel = 0;
    glm::vec3 m_ghostPos{0.0f};
    bool m_ghostValid = false;
    float m_snap = 0.5f;
    float m_rotateStep = 45.0f;

    // Look (texture variant for new objects, world grid overlay)
    int m_variant = 0;                 // index into the pack's textureVariants
    int m_overlay = 1;                 // 0 = none, else overlayTextures[m_overlay - 1]
    float m_overlayTile = 2.0f;
    float m_overlayStrength = 1.0f;

    // Physics toys
    int m_breakMaterial = 0;           // index into kBreakMaterials (see .cpp)
    float m_ballSpeed = 18.0f;
    std::vector<uint32_t> m_balls;     // oldest first; capped (see throwBall)
    char m_layoutPath[256] = "sandbox_layout.json";
    std::vector<kke::Module*> m_enginePanels;
    bool m_showEnginePanels = false;
};

} // namespace kke_sandbox
