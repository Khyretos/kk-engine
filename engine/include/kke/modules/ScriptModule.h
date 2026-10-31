#pragma once

#include "kke/AssetCatalog.h"
#include "kke/AudioMixer.h"
#include "kke/Module.h"
#include "kke/WorldStream.h"
#include "kke/SceneLoader.h"
#include "kke/ScriptCalls.h"
#include "kke/CameraDirector.h"
#include "kke/ScriptFx.h"
#include "kke/ScriptLights.h"
#include "kke/ScriptStore.h"
#include "kke/ScriptTables.h"
#include "kke/ScriptVM.h"
#include "kke/ai/AiWorld.h"
#include "kke/ai/NavMesh.h"
#include "kke/modules/ModelModule.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Rml {
class ElementDocument;
class EventListener;
}

namespace kke {

class DynamicMeshRenderer;
class ScriptClock;
class ScriptTerrain;
class ParticleEffects;
class ParticleLibrary;

// Lua gameplay scripts in a game, Garry's Mod style (docs/SCRIPTING.md).
//
// Loads every *.lua in a folder (default "scripts/", or KKE_SCRIPTS_DIR),
// in name order, and reloads a file the moment it changes on disk: edit,
// save, see it in the running game. Each script's hooks, timers and
// everything it made (bodies, models, breakables, UI documents, scenes)
// are replaced on reload, not duplicated. Each script has its own globals.
//
// Realms, by file name: sv_*.lua runs only where the game's physics is the
// truth (offline, or hosting); every other script runs on every player's
// machine. Joining or leaving a game loads/unloads sv_ scripts. What sv_
// scripts spawn (physics bodies, breakables, balls) appears on every
// player's machine too, moving and breaking with the host's copy
// (NetModule spawns; docs/SCRIPTING.md "Multiplayer").
//
// Hooks the engine runs: "Init" (once, after the first load), "Think"
// (dt, every frame), "Tick" (dt, tick; fixed rate, before physics results
// are read), "Contact" (a table per physics contact, capped per frame),
// "Break" (id: a script breakable came apart), "CameraDone" (a camera.path
// reached its end), "NetMessage" (name, data,
// fromPlayer), "Shutdown".
//
// Bindings, each present only when its module is in the game:
//   kke.*        log, time, dt
//   physics.*    box/sphere spawn (drawn by this module), remove, position,
//                velocity, setVelocity, impulse, raycast, joint, motor,
//                jointValue, removeJoint                   (RigidBodyModule)
//   models.*     load, spawn, move, remove, tint, visible, play, event, clips,
//                bounds                                    (ModelModule)
//   breakable.*  box, ball, remove, broken, pieces, count
//                                                          (PhysicsModule, FEMFX)
//   ui.*         open, load, text, rml, class, property, show, close,
//                onClick                                   (UiModule, RmlUi)
//   scene.*      list, load, unload, spawnPoint            (ModelModule + scenes/)
//   light.*      point, move, set, remove, count: lamps     (kke/ScriptLights.h)
//   fx.*         play, start, move, stop, count, names, define, wind: particle
//                effects by name (kke/ParticleLibrary.h, kke/ScriptFx.h)
//   net.*        role, isServer, connected, playerId, players, send
//                                                          (NetModule)
//   audio.*      impact(pos, material, intensity), materials   (AudioModule)
//   input.*      define(id, label, key), pressed, held, value  (InputModule)
//   camera.*     position, forward, target, fov (what the player sees);
//                path, hold, release, directing, shake: cutscenes
//                (kke/CameraDirector.h; hook "CameraDone" when a path ends)
//   terrain.*    create, height, normal, slope, ground, raise, lower, smooth,
//                flatten, paint, scatter, unscatter, layer, info, remove:
//                heightmap ground with trees and rocks
//                (kke/ScriptTerrain.h; collides through RigidBodyModule)
//   clock.*      start, set, pause, resume, stop, hour, time, isDay, sun,
//                dayLength: time of day; Dawn/Dusk/Hour hooks (kke/ScriptClock.h)
//   store.*      save, load, add, remove, keys: what outlives the session
//                (kke/ScriptStore.h; a file by default, see storeUrl)
//
// The panel (F1 panels in kke_demo) lists scripts with their CPU time and
// errors, has Reload, and a one-line Lua console that runs in the console's
// own environment or in any script's.
class ScriptModule : public Module {
public:
    explicit ScriptModule(std::string scriptsDir = "scripts");
    ~ScriptModule() override;

    const char* name() const override { return "Scripts"; }
    std::vector<ModuleDependency> dependencies() const override;
    void init(Application& app) override;
    void fixedUpdate(const FixedUpdateContext& ctx) override;
    void update(const UpdateContext& ctx) override;
    void render(const RenderContext& ctx) override;
    void renderTranslucent(const RenderContext& ctx) override;
    void renderShadow(const ShadowRenderContext& ctx) override;
    void renderUi() override;
    void shutdown() override;

    ScriptVM& vm() { return *m_vm; }
    const std::string& scriptsDir() const { return m_dir; }
    // The game's own folder: the one holding scriptsDir() (games/<game>
    // while you develop, games/<game> next to the executable when shipped).
    // Its models/ folder joins the asset catalog and its scenes/ folder is
    // searched first by scene.list and scene.load.
    std::string gameFolder() const;
    // Where scene.load looks, in order: <game folder>/scenes, then the shared
    // scenes/ folder (KKE_SCENES_DIR). Only folders that exist.
    std::vector<std::string> sceneFolders() const;
    void reloadAll();

    int maxContactsPerFrame = 32;
    size_t maxBodiesPerScript = 2000; // budget: a spawn loop can't take down the game
    size_t maxModelsPerScript = 2000;
    size_t maxJointsPerScript = 2000;
    size_t maxBreakablesPerScript = 64; // FEMFX bodies are expensive (docs/OPTIMIZATION.md)
    size_t maxDocumentsPerScript = 16;
    size_t maxScenesPerScript = 4;
    size_t maxSoundsPerScript = 64; // playing at once (the mixer has 32 voices; the quietest give way)
    size_t maxNavObstaclesPerScript = 256;

    // Where store.* keeps what scripts save (docs/STORAGE.md urls), opened
    // on first use; KKE_SCRIPT_STORE overrides it. Set before init.
    std::string storeUrl = "sqlite:save/scripts.db";
    // Whose collection it is ("lua.<game>"); "" = the NetModule's game id,
    // or "game" without one.
    std::string storeGame;
    // Or: a store someone else owns (a server's), used instead of storeUrl.
    void useStore(storage::Store* shared) { m_sharedStore = shared; }
    ScriptStore* scriptStore() { return m_store.get(); }
    // terrain.*: the terrain a script made (kke/ScriptTerrain.h, docs/TERRAIN.md).
    ScriptTerrain* terrain() { return m_terrain.get(); }
    // The navmesh nav.build made (null before it), for a game's own C++ (an AiWorld).
    ai::NavMesh* navMesh() { return m_nav.get(); }
    // ai.*'s creatures (docs/SCRIPTING.md "Creatures"): a game's C++ adds its
    // player here as an actor (AiWorld::addActor / setTransform) so they see it.
    ai::AiWorld& aiWorld() { return *m_ai; }
    // clock.*: the time of day (kke/ScriptClock.h, docs/MOODS.md "Time of day").
    ScriptClock* clock() { return m_clock.get(); }

    // Whether a script file runs on this machine right now (realms above).
    static bool runsHere(const std::string& path, bool authority);

    // Called by the UI click listener; runs at the next update.
    void queueClick(int document, const std::string& elementId);

private:
    struct ScriptFile { std::string path; std::filesystem::file_time_type mtime; bool ok = false; };
    // netId: its NetModule spawn id when replicated (0 = local only).
    struct Body { uint32_t id; std::string source; bool sphere; glm::vec3 half; glm::vec3 color; uint16_t netId = 0; std::vector<uint8_t> netDesc{}; uint16_t group = 0;
                  uint8_t look = 0; float cloudy = 0.0f; }; // look: script_net::BodySpawn::Look
    struct ModelInstance { ModelModule::InstanceId id; std::string source; };
    struct Joint { uint32_t id; std::string source; }; // physics.joint (RigidWorld::JointId)
    struct Breakable { uint32_t handle; std::string source; bool broken = false; uint16_t netId = 0; uint16_t netKind = 0; std::vector<uint8_t> netDesc{}; };
    struct Document { int id; Rml::ElementDocument* doc; std::string source; };
    struct ClickHandler { int document; std::string element; int ref; std::string source; };
    struct Scene { int id; LoadedScene loaded; std::string source; glm::vec3 spawn; float spawnYaw; bool streamed = false; }; // streamed: a world cell's (no maxScenesPerScript)

    void bindAll();
    void bindModels();
    void bindBreakables();
    void bindUi();
    void bindScenes();
    void bindNet();
    void bindStore();
    void bindAudio(); // audio.sound/play/music/bus/duck (ScriptBindings.cpp)
    void bindNav(); // nav.* pathfinding (ScriptBindings.cpp)
    void bindCreatures();  // ai.* over script-spawned models (ScriptBindings.cpp)
    void stepCreatures(float dt); // moves them, plays their clips, runs the ai hooks
    void forgetCreature(uint32_t instance);
    void bindCamera(); // ScriptCamera.cpp
    // world.* streaming (ScriptWorld.cpp, kke/WorldStream.h, docs/WORLD_STREAMING.md).
    void bindWorld();
    void updateWorld(float dt);
    void releaseWorld(const std::string& source);
    int loadSceneByName(const std::string& name, const glm::vec3& origin, const std::string& source, std::string& error, size_t* missing);
    void unloadSceneById(int id);
    // Multiplayer copies of what sv_ scripts spawn (ScriptReplication.cpp).
    void bindReplication();
    void syncNetRole();                    // hosting / joining / leaving changed what's replicated
    bool replicates(const std::string& source) const; // an sv_ script's, while hosting
    void replicate(Body& b);               // host: tell the clients (no-op otherwise)
    void replicate(Breakable& b);
    void unreplicate(uint16_t netId);      // it's gone (host: everywhere)
    void onNetSpawn(uint16_t id, uint16_t kind, const std::vector<uint8_t>& desc); // client: build the host's object
    void onNetDespawn(uint16_t id);
    void releaseScript(const std::string& source); // everything `source` made
    void releaseAll();
    void checkBreaks();
    void dispatchClicks();
    void dispatchNet();
    bool runCallAtomically(const std::function<bool()>& run); // ScriptCalls::Link::atomically
    void scanFolder(bool reloadChanged);
    void log(const std::string& source, const std::string& text);
    const AssetCatalog& catalog(); // the game's models/ folder, then the installed packs; scanned on first use
    Document* document(int id);

    Application* m_app = nullptr;
    std::unique_ptr<ScriptVM> m_vm;
    std::unique_ptr<ScriptCalls> m_calls;   // net.call / net.handle (after m_vm: they go first)
    std::unique_ptr<ScriptTables> m_tables; // net.table / net.watch
    // Inside a net.call handler: the net.sends that wait for it to succeed.
    bool m_inCall = false;
    std::vector<std::vector<uint8_t>> m_heldSends;
    bool m_callsOnline = false; // a client connected, last we looked
    std::unique_ptr<ScriptStore> m_store;
    storage::Store* m_sharedStore = nullptr;
    std::string m_dir;
    std::vector<ScriptFile> m_files;
    std::vector<Body> m_bodies;
    std::vector<Joint> m_joints;
    std::vector<ModelInstance> m_models;
    std::vector<Breakable> m_breakables;
    std::vector<Document> m_documents;
    std::vector<ClickHandler> m_clickHandlers;
    std::vector<std::pair<int, std::string>> m_pendingClicks;
    std::unique_ptr<Rml::EventListener> m_clickListener;
    std::vector<Scene> m_scenes;
    // The world streamer's cells: what loading one does (a scene at an origin, and/or the script's own functions).
    struct WorldCell {
        std::string source;
        std::string scene;        // a scene to load, "" = only the functions
        glm::vec3 origin{0.0f};
        int sceneId = 0;          // while loaded
        int loadRef = 0, unloadRef = 0; // script functions (registry refs), 0 = none
        bool implicit = false;    // a grid cell
    };
    WorldStreamer m_world;
    std::map<std::string, WorldCell> m_worldCells;
    struct WorldGrid { std::string source; int loadRef = 0, unloadRef = 0, sceneRef = 0; bool active = false; } m_worldGrid;
    std::vector<glm::vec3> m_worldObservers; // set by world.observer(s); empty = the camera
    bool m_worldUsed = false;
    struct Sound { uint32_t voice; std::string source; };
    std::vector<Sound> m_sounds;                 // audio.play's voices, by script
    std::vector<SoundHandle> m_soundFiles;       // audio.sound ids - 1
    std::unordered_map<std::string, int> m_soundIds;
    std::string m_musicSource;                   // the script that started the music
    std::unique_ptr<ai::NavMesh> m_nav;         // nav.build's
    std::string m_navSource;                    // the script that built it
    struct NavObstacle { ai::NavMesh::ObstacleId id; std::string source; };
    std::vector<NavObstacle> m_navObstacles;
    uint32_t m_navRandom = 0x9E3779B9u;         // nav.random's sequence
    std::unique_ptr<ai::AiWorld> m_ai = std::make_unique<ai::AiWorld>(1);
    struct CreatureClips { std::unordered_map<std::string, int> byAnim; std::string playing; };
    std::unordered_map<uint32_t, CreatureClips> m_creatureClips; // ai.anims, by model instance
    // camera.*: the cutscene director (run by Application after every update),
    // the script that started the shot, and what was shown last frame.
    CameraDirector m_director;
    std::string m_directorOwner;
    Camera m_shownCamera;
    bool m_cameraShown = false, m_cameraDone = false;
    const Camera& shownCamera() const;
    ScriptLights m_lights; // light.*: lamps scripts placed (kke/ScriptLights.h)
    std::unique_ptr<ScriptTerrain> m_terrain; // terrain.*
    std::unique_ptr<ScriptClock> m_clock;     // clock.*
    // fx.*: one particle pool and library for every script, made on first use.
    static constexpr size_t kFxParticles = 8000;
    std::unique_ptr<ParticleEffects> m_fxEffects;
    std::unique_ptr<ParticleLibrary> m_fxLibrary;
    ScriptFx m_fx; // which script started which emitter
    glm::vec3 m_fxWind{0.0f};
    ParticleLibrary& fxLibrary();
    int m_nextId = 1;
    AssetCatalog m_catalog;
    bool m_catalogScanned = false;
    struct NetMessage { std::string name, data; int from; };
    std::vector<NetMessage> m_netInbox;
    bool m_authority = true;
    int m_netRole = 0; // NetModule::Role as last seen (0 = offline)
    std::unique_ptr<DynamicMeshRenderer> m_batch, m_glass; // solid bodies; glass ones (and hidden ones while shown)
    size_t m_batchIndices = 0, m_glassIndices = 0;
    bool m_showHidden = false; // physics.showHidden / the panel: draw invisible bodies, faintly
    double m_time = 0.0, m_scanTimer = 0.0;
    float m_dt = 0.0f;
    bool m_inited = false;
    uint32_t m_promptSerial = 0; // InputModule::promptSerial() last told to scripts (InputStyle hook)
    std::vector<std::string> m_console;
    char m_consoleInput[512] = {};
    std::string m_consoleTarget = "console"; // which script's globals the console line runs in
    std::unordered_map<std::string, double> m_cpuLast, m_cpuMs; // per script: total at last frame, smoothed ms/frame
};

} // namespace kke
