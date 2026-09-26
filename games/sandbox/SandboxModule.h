#pragma once

#include "kke/AssetCatalog.h"
#include "kke/Capabilities.h"
#include "kke/Module.h"
#include "kke/Picking.h"
#include "kke/Ragdoll.h"
#include "kke/SceneFile.h"
#include "kke/VoxelTets.h"
#include "kke/modules/DebugDrawModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/ThumbnailModule.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace kke_sandbox {

// A mini level editor / toy box over whatever asset packs are on disk:
// browse them, place pieces on a snapping grid, select / move / rotate /
// duplicate / delete them (one or many: Shift+click adds to the
// selection), move / rotate / scale them with the gizmo, undo / redo every
// edit, save and load the level as a kke.scene (SceneFile.h: the format
// kke_demo and every game loads), and play with them — ragdoll
// characters, turn props into breakable physics objects, throw balls at
// everything.
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
    // Saves the level as a kke.scene; loads a kke.scene or an older
    // "kke-sandbox-layout" file. False (and the reason in the status line)
    // on failure; nothing in the level changes when a load fails.
    bool saveLayout(const std::string& path);
    bool loadLayout(const std::string& path);
    // The level as a scene / replaced by one (what save and load use).
    kke::SceneFile toScene() const;
    void fromScene(const kke::SceneFile& scene);
    // Engine debug panels (Physics, Camera, Stats...) hidden behind one
    // toggle (F1) so the sandbox's own UI stays readable.
    void setEnginePanels(std::vector<kke::Module*> panels);

private:
    enum class Tool { Select, Place, Shoot };
    enum class Gizmo { Move, Rotate, Scale };
    enum class Handle { None, X, Y, Z, Ring, Scale };

    struct Object {
        uint32_t id = 0;
        std::string asset;        // catalog name, e.g. "SM_Prop_Crate_01"
        std::string pack;         // the pack it came from (names repeat across packs)
        glm::vec3 position{0.0f}; // bottom-center of its bounds
        float yawDegrees = 0.0f;
        float scale = 1.0f;       // uniform, around the bottom-center
        kke::SceneObject::Collision collision = kke::SceneObject::Collision::Mesh; // in the games that load the level
        kke::ModelModule::ModelId model = 0;
        kke::ModelModule::InstanceId instance = 0;
        bool character = false;
        std::string texture;      // texture variant path, "" = the model's own
        uint32_t fractureSeed = 0; // this object's own seed, mixed with the world's (kke::fractureSeed); 0 = its id
        int breakMaterial = -1;    // kBreakMaterials index it was made breakable with (saved in layouts)
        // Ragdoll (characters)
        kke::IRagdollPhysics::RagdollHandle ragdoll = 0;
        kke::RagdollDesc ragdollDesc;
        kke::RagdollSkinBinding binding;
        // Breakable (props): a FEMFX tet volume voxelized from the prop's
        // own mesh; the prop's vertices are glued to it (embedding) and
        // drawn deformed every frame the physics is awake.
        uint32_t proxy = 0;
        kke::TetEmbedding embedding;          // all mesh parts' vertices, concatenated
        std::vector<glm::vec3> restNormals;
        std::vector<size_t> partOffsets;      // where each mesh part starts in the arrays above
        bool settled = false;                 // last frame's vertices already match a sleeping object
    };

    // What undo/redo restores: the saved part of each object.
    struct ObjectState {
        uint32_t id;
        std::string asset, pack, texture;
        glm::vec3 position;
        float yawDegrees, scale;
        kke::SceneObject::Collision collision;
        uint32_t fractureSeed;
        int breakMaterial;                    // -1: not breakable right now
    };
    using Snapshot = std::vector<ObjectState>;

    // An asset by name, from `pack` if given, else the loaded scene's
    // packs first, else any pack.
    const kke::CatalogAsset* resolve(const std::string& name, const std::string& pack = {}) const;
    kke::ModelModule::ModelId loadAsset(const std::string& name, const std::string& pack = {});
    glm::mat4 objectTransform(const kke::ModelData& model, const glm::vec3& position, float yawDegrees, float scale = 1.0f) const;
    glm::mat4 objectTransform(const Object& o) const;
    void applyTransform(Object& o);
    Object* spawnObject(const std::string& asset, const glm::vec3& position, float yawDegrees, uint32_t id = 0, const std::string& pack = {});
    void removeObject(uint32_t id);
    void clearAll();
    Object* find(uint32_t id);
    void worldBounds(const Object& o, glm::vec3& mn, glm::vec3& mx) const;

    // Selection: m_selected is the primary (inspector, gizmo pivot for
    // one object); m_selection holds every selected id, primary included.
    bool isSelected(uint32_t id) const;
    void select(uint32_t id, bool additive);
    void clearSelection();
    std::vector<Object*> selectedObjects(); // editable ones (not ragdolls or live breakables)

    // Undo / redo: a snapshot of the level before each edit.
    Snapshot snapshot() const;
    void pushUndo();
    void restore(const Snapshot& s);
    void undo();
    void redo();

    // Gizmo (move along X/Y/Z, rotate around Y, uniform scale).
    bool gizmoPivot(glm::vec3& pivot, float& size) const;
    Handle hoverHandle() const;
    void beginDrag(Handle h);
    void updateDrag();
    void drawGizmo();
    void rotateSelection(float degrees);
    void deleteSelection();
    void duplicateSelection();

    void lightsUi();

    kke::Ray mouseRay() const;
    // Ground-or-stack point under the mouse (ignores `ignoreId`).
    bool placementPoint(glm::vec3& out, uint32_t ignoreId) const;
    uint32_t pickObject() const;

    void beginPlacing(const std::string& asset, float yawDegrees, uint32_t movingId = 0, const std::string& pack = {});
    void cancelPlacing();
    void commitPlacement(bool keepPlacing);

    void ragdoll(Object& o, const glm::vec3& push);
    void standUp(Object& o);
    void makeBreakable(Object& o);
    void restoreProp(Object& o);
    void updateBreakables();
    void throwBall();

    void applyLook();                   // overlay + variants from the Look settings
    const kke::CatalogPack* packOf(const std::string& asset, const std::string& pack = {}) const;
    void lookUi();
    void assetBrowserUi();
    void assetGridUi(float uiScale);
    void inspectorUi();
    void folderNotFoundUi();

    kke::Application* m_app = nullptr;
    kke::ModelModule* m_models = nullptr;
    kke::ThumbnailModule* m_thumbs = nullptr; // optional: the Assets panel shows a list without it
    bool m_gridView = true;
    float m_thumbSize = 88.0f;
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
    std::vector<uint32_t> m_selection;
    uint32_t m_hovered = 0;
    std::vector<Snapshot> m_undo, m_redo;
    static constexpr size_t kMaxUndo = 100;

    // Gizmo drag state
    Gizmo m_gizmo = Gizmo::Move;
    Handle m_hoverHandle = Handle::None, m_drag = Handle::None;
    glm::vec3 m_dragPivot{0.0f};
    float m_dragSize = 1.0f, m_dragStartParam = 0.0f, m_dragStartMouseY = 0.0f;
    struct DragStart { uint32_t id; glm::vec3 position; float yaw, scale; };
    std::vector<DragStart> m_dragStart;
    bool m_dragMoved = false;

    // Moving several objects with G: the others follow the primary.
    std::vector<DragStart> m_groupMove;

    // Level settings saved with the scene
    glm::vec3 m_spawn{0.0f, 0.0f, 6.0f};
    float m_spawnYaw = 180.0f;
    std::vector<kke::SceneLight> m_pointLights; // drawn as markers, lit in slots 2-3
    int m_selectedLight = -1;
    char m_levelName[128] = "Sandbox level";
    std::string m_levelDescription = "Built in the sandbox";

    // Placement
    Tool m_tool = Tool::Select;
    std::string m_placeAsset, m_placePack;
    std::vector<std::string> m_scenePacks; // the loaded scene's pack preference
    float m_placeYaw = 0.0f;
    float m_placeScale = 1.0f;
    bool m_duplicating = false;        // placing fresh copies (Esc takes them away again)
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
    int m_patternOverride = 0;         // 0 = the material's own pattern, else FracturePattern + 1
    float m_chunkScale = 1.0f;         // multiplies the material's chunk size
    int m_detailCells = 160;           // voxel budget per prop (6 tets per cell)
    float m_toughness = 1.0f;          // multiplies the material's fracture threshold
    uint32_t m_worldSeed = 1;          // the world's fracture seed (see kke::fractureSeed)
    std::string m_lastBreakStats;
    float m_ballSpeed = 18.0f;
    std::vector<uint32_t> m_balls;     // oldest first; capped (see throwBall)
    char m_layoutPath[512] = "sandbox.scene.json";
    std::vector<kke::Module*> m_enginePanels;
    bool m_showEnginePanels = false;
};

} // namespace kke_sandbox
