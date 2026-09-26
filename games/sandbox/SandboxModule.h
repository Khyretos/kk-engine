#pragma once

#include "kke/AssetCatalog.h"
#include "kke/Capabilities.h"
#include "kke/Module.h"
#include "kke/Picking.h"
#include "kke/PlayBlocks.h"
#include "kke/Ragdoll.h"
#include "kke/RigidWorld.h"
#include "kke/SceneFile.h"
#include "kke/TouchGestures.h"
#include "kke/VoxelTets.h"
#include "kke/modules/DebugDrawModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/ThumbnailModule.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <memory>
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
// It opens in Play mode (docs/PLAY_TO_MAKE.md "Simple"): no panels, just a
// row of big pictures to drag people and things out of, a bat to bonk
// them with (they ragdoll) and a "Build" button that opens the full editor
// described above. F2 switches between the two; KKE_SANDBOX_MODE=build
// starts in the editor.
//
// Play mode works the same with a finger (one finger is the mouse; two
// turn and pinch-zoom the view, and drop whatever the first was dragging)
// and with a gamepad: left stick moves a cursor, A presses (hold and move
// to drag), B cancels, LB/RB or the D-pad jump along the palette, Y stands
// everyone up, right stick turns the view, triggers zoom.
// "Look" (the magnifier in Play mode) opens the node graph inside a thing,
// a palette block (its recipe: the bat is a graph) or, on the ground, the
// level's own graph: docs/PLAY_TO_MAKE.md "Intermediate", PlayScripting.cpp.
//
// KKE_SANDBOX_REPLAY=file plays timed touches and gamepad input into it
// (tests/sandbox_replays/; format in loadReplay()).
//
// Deliberately written against engine building blocks only (AssetCatalog,
// ModelModule, DebugDrawModule, Picking, IRagdollPhysics, PhysicsModule),
// so it doubles as the reference for how a game uses them. The physics
// parts are optional: without FEMFX it is still a working level editor.
class SandboxModule : public kke::Module {
public:
    SandboxModule();
    ~SandboxModule() override;
    const char* name() const override { return "Sandbox"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;
    void renderUi() override;
    void onEvent(const SDL_Event& event) override;
    void shutdown() override;

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

    // Play: the Simple-mode palette only. Build: the full editor.
    enum class Mode { Play, Build };
    void setMode(Mode mode);
    Mode mode() const { return m_mode; }

private:
    enum class Tool { Select, Place, Shoot, Bat, Look };
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
        // Jolt collision (a static box of its bounds) so ragdolls land on it.
        kke::RigidWorld::BodyId collider = kke::RigidWorld::kNoBody;
        // Breakable (props): a FEMFX tet volume voxelized from the prop's
        // own mesh; the prop's vertices are glued to it (embedding) and
        // drawn deformed every frame the physics is awake.
        uint32_t proxy = 0;
        kke::TetEmbedding embedding;          // all mesh parts' vertices, concatenated
        std::vector<glm::vec3> restNormals;
        std::vector<size_t> partOffsets;      // where each mesh part starts in the arrays above
        bool settled = false;                 // last frame's vertices already match a sleeping object
        // Node graphs (PlayScripting.cpp)
        kke::NodeGraph graph;                 // what this one thing does
        std::string owner;                    // the graph that brought it out ("" = placed by hand, saved)
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
        kke::NodeGraph graph;
    };
    using Snapshot = std::vector<ObjectState>;

    // An asset by name, from `pack` if given, else the loaded scene's
    // packs first, else any pack.
    const kke::CatalogAsset* resolve(const std::string& name, const std::string& pack = {}) const;
    kke::ModelModule::ModelId loadAsset(const std::string& name, const std::string& pack = {});
    glm::mat4 objectTransform(const kke::ModelData& model, const glm::vec3& position, float yawDegrees, float scale = 1.0f) const;
    glm::mat4 objectTransform(const Object& o) const;
    void applyTransform(Object& o);
    // Jolt, when present: the floor, and a static box per placed piece
    // (people excluded: they're what gets knocked over).
    kke::RigidWorld* rigidWorld() const;
    void syncCollider(Object& o);
    void dropCollider(Object& o);
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

    // Play mode (Simple): the palette, dragging blocks into the world,
    // picking placed things up again, and the bat.
    void playPaletteUi();
    void modeSwitchUi();
    bool mouseOverUi() const;      // over any ImGui window, even mid-drag
    void placeBlock(size_t block); // start placing (the ghost follows the mouse)
    void swingBat();
    void updateBat(float dt);
    void standEveryoneUp();

    // Node graphs (PlayScripting.cpp): the play blocks in Lua, recipes
    // (the bat's graph), each thing's own graph and the level's, the
    // editor, and the events that drive them.
    struct PlayGraphs;
    void initGraphs();
    void updateGraphs(float dt);
    void shutdownGraphs();
    void graphUi();                            // the score and what graphs say
    void openThingGraph(uint32_t thing);       // 0 = the level's graph
    void openRecipe(const std::string& block);
    bool graphEditorOpen() const;
    void closeGraphEditor();                   // keeps what was made
    bool graphEditorKey(SDL_Keycode key);      // Esc, Delete: true if the editor took it
    bool graphEditorPadButton(uint8_t button, bool down);
    bool graphEditorPadSticks(float dt, const glm::vec2& rightStick, float zoom); // true: the editor took them
    void graphsToScene(kke::SceneFile& scene) const;
    void graphsFromScene(const kke::SceneFile& scene);
    bool graphEditorContains(const glm::vec2& point) const;
    bool lookAvailable() const;                // Lua is built in and the editor has an RmlUi context
    std::string blockOf(const std::string& asset) const;
    std::vector<std::string> playBlockIds() const;
    // Events for graphs and scripts, fired at the next update.
    void queueHit(uint32_t target, const glm::vec3& point, const glm::vec3& push);
    void queueClicked(uint32_t thing, const glm::vec3& point);
    void queuePlaced(uint32_t thing, const glm::vec3& point);
    void queueFellOver(uint32_t thing);
    void queueStoodUp(uint32_t thing);
    // Swings the bat so its sweet spot passes through `target` (the foot of
    // whoever is there, or a spot on the ground).
    bool swingBatAt(const glm::vec3& target);

    // Touch and gamepads (Play mode).
    void openGamepad(SDL_JoystickID id);
    void updatePad(float dt);
    void padButton(uint8_t button, bool down);
    void pointerButton(bool down);        // a left mouse press/release at the cursor
    void warpPointer(const glm::vec2& p); // moves the real mouse (SDL fakes it where it can't)
    void dropFingerDrag();                // a second finger landed: let go of what the first was dragging
    bool loadReplay(const std::string& path);
    void updateReplay(float dt);

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

    // Play mode
    Mode m_mode = Mode::Play;
    std::vector<kke::PlayBlock> m_blocks;
    std::vector<std::vector<std::string>> m_blockAssets; // per block: its assets that are on disk
    uint32_t m_lookPick = 0;         // turns through the character looks
    bool m_dropOnRelease = false;    // placing by dragging: letting go of the mouse drops it
    kke::BatSwing m_swing;
    kke::ModelModule::ModelId m_batModel = 0;
    kke::ModelModule::InstanceId m_bat = 0;
    kke::LongAxis m_batAxis;
    std::vector<uint32_t> m_swingHits; // characters this swing already knocked over

    // Touch and gamepads
    kke::TouchGestures m_touches;        // only to know when a second finger lands
    std::vector<SDL_Gamepad*> m_pads;
    bool m_gamepadSubsystem = false;
    glm::vec2 m_padCursor{-1.0f};        // where the gamepad's cursor is (window points); x < 0: not placed yet
    double m_padLastUsed = -1e9;         // seconds; the cursor is drawn while a pad is in use
    bool m_padPressing = false;
    std::vector<glm::vec2> m_paletteCells; // centres, left to right, from the last palette drawn
    struct ReplayStep {
        float time = 0.0f;
        int line = 0;
        std::string kind, what;          // "finger" down/move/up, "pad" attach/axis/button
        std::string name;                // pad axis or button name
        uint64_t finger = 0;
        float x = 0.0f, y = 0.0f, value = 0.0f;
    };
    std::vector<ReplayStep> m_replay;
    size_t m_replayNext = 0;
    float m_replayTime = 0.0f;
    SDL_Joystick* m_replayPad = nullptr; // a virtual gamepad the replay drives
    SDL_JoystickID m_replayPadId = 0;

    std::unique_ptr<PlayGraphs> m_graphs;
    bool m_graphsDirty = true;          // things came or went: graphs to (un)load
    bool m_graphTouch = false;          // a two-finger gesture on the graph editor
    glm::vec2 m_tapStart{0.0f};         // a press with the hand: a tap is a click
    uint32_t m_tapThing = 0;
};

} // namespace kke_sandbox
