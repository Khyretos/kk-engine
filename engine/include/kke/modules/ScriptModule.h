#pragma once

#include "kke/AssetCatalog.h"
#include "kke/Module.h"
#include "kke/SceneLoader.h"
#include "kke/ScriptVM.h"
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
// "Break" (id: a script breakable came apart), "NetMessage" (name, data,
// fromPlayer), "Shutdown".
//
// Bindings, each present only when its module is in the game:
//   kke.*        log, time, dt
//   physics.*    box/sphere spawn (drawn by this module), remove, position,
//                velocity, setVelocity, impulse, raycast   (RigidBodyModule)
//   models.*     load, spawn, move, remove, tint, visible, play, clips,
//                bounds                                    (ModelModule)
//   breakable.*  box, ball, remove, broken, pieces, count
//                                                          (PhysicsModule, FEMFX)
//   ui.*         open, load, text, rml, class, property, show, close,
//                onClick                                   (UiModule, RmlUi)
//   scene.*      list, load, unload, spawnPoint            (ModelModule + scenes/)
//   net.*        role, isServer, connected, playerId, players, send
//                                                          (NetModule)
//   audio.*      impact(pos, material, intensity), materials   (AudioModule)
//   input.*      define(id, label, key), pressed, held, value  (InputModule)
//   camera.*     position, forward, target
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
    void renderShadow(const ShadowRenderContext& ctx) override;
    void renderUi() override;
    void shutdown() override;

    ScriptVM& vm() { return *m_vm; }
    const std::string& scriptsDir() const { return m_dir; }
    void reloadAll();

    int maxContactsPerFrame = 32;
    size_t maxBodiesPerScript = 2000; // budget: a spawn loop can't take down the game
    size_t maxModelsPerScript = 2000;
    size_t maxBreakablesPerScript = 64; // FEMFX bodies are expensive (docs/OPTIMIZATION.md)
    size_t maxDocumentsPerScript = 16;
    size_t maxScenesPerScript = 4;

    // Whether a script file runs on this machine right now (realms above).
    static bool runsHere(const std::string& path, bool authority);

    // Called by the UI click listener; runs at the next update.
    void queueClick(int document, const std::string& elementId);

private:
    struct ScriptFile { std::string path; std::filesystem::file_time_type mtime; bool ok = false; };
    // netId: its NetModule spawn id when replicated (0 = local only).
    struct Body { uint32_t id; std::string source; bool sphere; glm::vec3 half; glm::vec3 color; uint16_t netId = 0; std::vector<uint8_t> netDesc{}; };
    struct ModelInstance { ModelModule::InstanceId id; std::string source; };
    struct Breakable { uint32_t handle; std::string source; bool broken = false; uint16_t netId = 0; uint16_t netKind = 0; std::vector<uint8_t> netDesc{}; };
    struct Document { int id; Rml::ElementDocument* doc; std::string source; };
    struct ClickHandler { int document; std::string element; int ref; std::string source; };
    struct Scene { int id; LoadedScene loaded; std::string source; glm::vec3 spawn; float spawnYaw; };

    void bindAll();
    void bindModels();
    void bindBreakables();
    void bindUi();
    void bindScenes();
    void bindNet();
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
    void scanFolder(bool reloadChanged);
    void log(const std::string& source, const std::string& text);
    const AssetCatalog& catalog(); // installed packs, scanned on first use
    Document* document(int id);

    Application* m_app = nullptr;
    std::unique_ptr<ScriptVM> m_vm;
    std::string m_dir;
    std::vector<ScriptFile> m_files;
    std::vector<Body> m_bodies;
    std::vector<ModelInstance> m_models;
    std::vector<Breakable> m_breakables;
    std::vector<Document> m_documents;
    std::vector<ClickHandler> m_clickHandlers;
    std::vector<std::pair<int, std::string>> m_pendingClicks;
    std::unique_ptr<Rml::EventListener> m_clickListener;
    std::vector<Scene> m_scenes;
    int m_nextId = 1;
    AssetCatalog m_catalog;
    bool m_catalogScanned = false;
    struct NetMessage { std::string name, data; int from; };
    std::vector<NetMessage> m_netInbox;
    bool m_authority = true;
    int m_netRole = 0; // NetModule::Role as last seen (0 = offline)
    std::unique_ptr<DynamicMeshRenderer> m_batch;
    size_t m_batchIndices = 0;
    double m_time = 0.0, m_scanTimer = 0.0;
    float m_dt = 0.0f;
    bool m_inited = false;
    std::vector<std::string> m_console;
    char m_consoleInput[512] = {};
    std::string m_consoleTarget = "console"; // which script's globals the console line runs in
    std::unordered_map<std::string, double> m_cpuLast, m_cpuMs; // per script: total at last frame, smoothed ms/frame
};

} // namespace kke
