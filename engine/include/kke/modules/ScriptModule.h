#pragma once

#include "kke/Module.h"
#include "kke/ScriptVM.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace kke {

class DynamicMeshRenderer;

// Lua gameplay scripts in a game, Garry's Mod style (SCRIPTING.md).
//
// Loads every *.lua in a folder (default "scripts/", or KKE_SCRIPTS_DIR),
// in name order, and reloads a file the moment it changes on disk: edit,
// save, see it in the running game. Each script's hooks, timers and
// spawned bodies are replaced on reload, not duplicated.
//
// Hooks the engine runs: "Init" (once, after the first load), "Think"
// (dt, every frame), "Tick" (dt, tick; fixed rate, before physics results
// are read), "Contact" (a table per physics contact, capped per frame),
// "Shutdown".
//
// Bindings, each present only when its module is in the game:
//   kke.*      log, time, dt
//   physics.*  box/sphere spawn (drawn by this module), remove, position,
//              velocity, setVelocity, impulse, raycast   (RigidBodyModule)
//   audio.*    impact(pos, material, intensity), materials   (AudioModule)
//   input.*    define(id, label, key), pressed, held, value  (InputModule)
//   camera.*   position, forward, target
//
// The panel (F1 panels in kke_demo) lists scripts and errors, has
// Reload, and a one-line Lua console.
class ScriptModule : public Module {
public:
    explicit ScriptModule(std::string scriptsDir = "scripts");
    ~ScriptModule() override;

    const char* name() const override { return "Scripts"; }
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

private:
    struct ScriptFile { std::string path; std::filesystem::file_time_type mtime; bool ok = false; };
    struct Body { uint32_t id; std::string source; bool sphere; glm::vec3 half; glm::vec3 color; };

    void bindAll();
    void scanFolder(bool reloadChanged);
    void log(const std::string& source, const std::string& text);

    Application* m_app = nullptr;
    std::unique_ptr<ScriptVM> m_vm;
    std::string m_dir;
    std::vector<ScriptFile> m_files;
    std::vector<Body> m_bodies;
    std::unique_ptr<DynamicMeshRenderer> m_batch;
    size_t m_batchIndices = 0;
    double m_time = 0.0, m_scanTimer = 0.0;
    float m_dt = 0.0f;
    bool m_inited = false;
    std::vector<std::string> m_console;
    char m_consoleInput[512] = {};
    size_t m_errorsShown = 0;
};

} // namespace kke
