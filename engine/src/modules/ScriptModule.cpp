#include "kke/modules/ScriptModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/AudioModule.h"
#if KKE_ENABLE_JOLT
#include "kke/modules/RigidBodyModule.h"
#endif
#if KKE_ENABLE_NET
#include "kke/modules/NetModule.h"
#endif

#include <RmlUi/Core/EventListener.h>
#include <imgui.h>
#include <lauxlib.h>
#include <lua.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstdlib>

namespace kke {

namespace {

void appendBox(const glm::mat4& m, const glm::vec3& half, const glm::vec3& color, std::vector<Vertex>& v, std::vector<uint32_t>& idx) {
    const glm::vec3 n[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const glm::mat3 nm = glm::mat3(m);
    for (const glm::vec3& normal : n) {
        const glm::vec3 u = std::abs(normal.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        const glm::vec3 w = glm::cross(normal, u);
        const uint32_t base = uint32_t(v.size());
        const glm::vec3 c = color * (normal.y > 0.5f ? 1.0f : normal.y < -0.5f ? 0.6f : 0.85f);
        for (glm::vec2 k : {glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1)}) {
            const glm::vec3 p = (normal + u * k.x + w * k.y) * half;
            v.push_back({glm::vec3(m * glm::vec4(p, 1.0f)), c, glm::normalize(nm * normal), glm::vec2(0.0f)});
        }
        const glm::vec3 a = v[base].position, b = v[base + 1].position, cc = v[base + 2].position;
        if (glm::dot(glm::cross(b - a, cc - a), nm * normal) >= 0.0f) idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        else idx.insert(idx.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
    }
}

// 8 x 12 UV sphere: 96 quads, enough to read as round at game distances.
void appendSphere(const glm::mat4& m, float r, const glm::vec3& color, std::vector<Vertex>& v, std::vector<uint32_t>& idx) {
    const int rings = 8, segs = 12;
    const uint32_t base = uint32_t(v.size());
    const glm::mat3 nm = glm::mat3(m);
    for (int i = 0; i <= rings; ++i) {
        const float th = glm::pi<float>() * float(i) / rings;
        for (int j = 0; j <= segs; ++j) {
            const float ph = glm::two_pi<float>() * float(j) / segs;
            const glm::vec3 n(std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph));
            // A darker band on one side so rolling is visible.
            const glm::vec3 c = color * (j < 2 ? 0.55f : 1.0f);
            v.push_back({glm::vec3(m * glm::vec4(n * r, 1.0f)), c, glm::normalize(nm * n), glm::vec2(0.0f)});
        }
    }
    for (int i = 0; i < rings; ++i)
        for (int j = 0; j < segs; ++j) {
            const uint32_t a = base + uint32_t(i * (segs + 1) + j), b = a + uint32_t(segs + 1);
            idx.insert(idx.end(), {a, a + 1, b, a + 1, b + 1, b});
        }
}

} // namespace

ScriptModule::ScriptModule(std::string scriptsDir) : m_dir(std::move(scriptsDir)) {}
ScriptModule::~ScriptModule() = default;

void ScriptModule::log(const std::string& source, const std::string& text) {
    const std::string name = source.empty() ? "script" : std::filesystem::path(source).filename().string();
    m_console.push_back("[" + name + "] " + text);
    if (m_console.size() > 300) m_console.erase(m_console.begin());
    if (text.rfind("ERROR:", 0) == 0) log::get("Scripts")->error("{}: {}", name, text.substr(7));
    else log::get("Scripts")->info("{}: {}", name, text);
}

void ScriptModule::init(Application& app) {
    m_app = &app;
    if (const char* d = std::getenv("KKE_SCRIPTS_DIR"); d && *d) m_dir = d;
    m_vm = std::make_unique<ScriptVM>();
    m_vm->printSink = [this](const std::string& src, const std::string& text) { log(src, text); };
    m_vm->onUnload = [this](const std::string& source) { releaseScript(source); };
    m_vm->onStopped = [this](const std::string& source) {
        for (ScriptFile& f : m_files)
            if (f.path == source) f.ok = false;
    };
    m_batch = std::make_unique<DynamicMeshRenderer>(app);
#if KKE_ENABLE_NET
    if (auto* net = app.getModule<NetModule>()) m_authority = net->authority();
#endif
    bindAll();
    scanFolder(false);
    log::get(name())->info("{} script(s) from '{}'", m_files.size(), m_dir);
}

void ScriptModule::scanFolder(bool reloadChanged) {
    std::error_code ec;
    if (!std::filesystem::is_directory(m_dir, ec)) return;
    std::vector<std::string> found;
    for (const auto& e : std::filesystem::directory_iterator(m_dir, ec))
        if (e.is_regular_file() && e.path().extension() == ".lua" && runsHere(e.path().generic_string(), m_authority))
            found.push_back(e.path().generic_string());
    std::sort(found.begin(), found.end());
    for (const std::string& path : found) {
        const auto mtime = std::filesystem::last_write_time(path, ec);
        auto it = std::find_if(m_files.begin(), m_files.end(), [&](const ScriptFile& f) { return f.path == path; });
        if (it == m_files.end()) {
            ScriptFile f{path, mtime, false};
            f.ok = m_vm->runFile(path);
            m_files.push_back(f);
            if (reloadChanged) log(path, "loaded");
        } else if (reloadChanged && mtime != it->mtime) {
            it->mtime = mtime;
            it->ok = m_vm->reloadFile(path);
            log(path, it->ok ? "reloaded" : "reload failed (see error above)");
        }
    }
    // Deleted files (or sv_ scripts once we're a client): unload them.
    for (auto it = m_files.begin(); it != m_files.end();) {
        if (std::find(found.begin(), found.end(), it->path) == found.end()) {
            m_vm->unload(it->path);
            log(it->path, "removed");
            it = m_files.erase(it);
        } else {
            ++it;
        }
    }
}

void ScriptModule::reloadAll() {
    for (ScriptFile& f : m_files) f.ok = m_vm->reloadFile(f.path);
}

void ScriptModule::bindAll() {
    ScriptVM& vm = *m_vm;
    Application* app = m_app;

    vm.registerFunction("kke", "log", [this](lua_State* L) {
        std::string out;
        for (int i = 1; i <= lua_gettop(L); ++i) {
            if (i > 1) out += ' ';
            out += luaL_tolstring(L, i, nullptr);
            lua_pop(L, 1);
        }
        log(m_vm->currentSource(), out);
        return 0;
    });
    vm.registerFunction("kke", "time", [this](lua_State* L) { lua_pushnumber(L, m_time); return 1; });
    vm.registerFunction("kke", "dt", [this](lua_State* L) { lua_pushnumber(L, m_dt); return 1; });

    // camera.*
    vm.registerFunction("camera", "position", [app](lua_State* L) { ScriptVM::pushVec3(L, app->camera().position); return 1; });
    vm.registerFunction("camera", "target", [app](lua_State* L) { ScriptVM::pushVec3(L, app->camera().target); return 1; });
    vm.registerFunction("camera", "forward", [app](lua_State* L) {
        glm::vec3 f = app->camera().target - app->camera().position;
        ScriptVM::pushVec3(L, glm::length(f) > 1e-6f ? glm::normalize(f) : glm::vec3(0, 0, -1));
        return 1;
    });

    // input.*
    if (auto* in = app->getModule<InputModule>()) {
        vm.registerFunction("input", "define", [in](lua_State* L) {
            const std::string id = luaL_checkstring(L, 1);
            const std::string label = luaL_optstring(L, 2, id.c_str());
            InputMap& map = in->map(0);
            if (!map.action(id)) {
                map.defineAction({id, label, "Scripts", "game"});
                if (lua_isstring(L, 3)) {
                    const SDL_Scancode sc = SDL_GetScancodeFromName(lua_tostring(L, 3));
                    if (sc == SDL_SCANCODE_UNKNOWN) return luaL_error(L, "input.define: unknown key name '%s'", lua_tostring(L, 3));
                    map.addBinding(InputModule::bind(id, InputModule::key(sc)));
                }
            }
            return 0;
        });
        vm.registerFunction("input", "pressed", [in](lua_State* L) { lua_pushboolean(L, in->map(0).pressed(luaL_checkstring(L, 1))); return 1; });
        vm.registerFunction("input", "held", [in](lua_State* L) { lua_pushboolean(L, in->map(0).held(luaL_checkstring(L, 1))); return 1; });
        vm.registerFunction("input", "value", [in](lua_State* L) { lua_pushnumber(L, in->map(0).state(luaL_checkstring(L, 1)).value); return 1; });
    }

    // audio.*
    if (auto* audio = app->getModule<AudioModule>()) {
        vm.registerFunction("audio", "impact", [audio](lua_State* L) {
            const glm::vec3 p = ScriptVM::toVec3(L, 1);
            uint32_t mat = 0;
            if (lua_isnumber(L, 2)) mat = uint32_t(lua_tointeger(L, 2));
            else if (lua_isstring(L, 2)) {
                const std::string n = lua_tostring(L, 2);
                for (const auto& [id, m] : audio->materials().all())
                    if (m.name == n) mat = id;
            }
            audio->playImpact(p, mat, float(luaL_optnumber(L, 3, 0.7)));
            return 0;
        });
        // audio.materials() -> { Stone = 1, Wood = 2, ... }
        vm.registerFunction("audio", "materials", [audio](lua_State* L) {
            lua_newtable(L);
            for (const auto& [id, m] : audio->materials().all()) {
                lua_pushinteger(L, lua_Integer(id));
                lua_setfield(L, -2, m.name.c_str());
            }
            return 1;
        });
    }

#if KKE_ENABLE_JOLT
    if (auto* rbm = app->getModule<RigidBodyModule>()) {
        auto spawn = [this, rbm](lua_State* L, bool sphere) {
            const std::string src = m_vm->currentSource();
            size_t mine = 0;
            for (const Body& b : m_bodies) mine += b.source == src;
            if (mine >= maxBodiesPerScript) return luaL_error(L, "physics: this script already has %d bodies (maxBodiesPerScript)", int(maxBodiesPerScript));
            luaL_checktype(L, 1, LUA_TTABLE);
            RigidWorld::BodyDesc d;
            d.shape = sphere ? RigidWorld::Shape::Sphere : RigidWorld::Shape::Box;
            d.position = ScriptVM::fieldVec3(L, 1, "pos", glm::vec3(0, 2, 0));
            d.velocity = ScriptVM::fieldVec3(L, 1, "velocity", glm::vec3(0.0f));
            d.radius = std::max(0.02f, ScriptVM::fieldNumber(L, 1, "radius", 0.25f));
            d.halfExtents = glm::max(ScriptVM::fieldVec3(L, 1, "size", glm::vec3(0.5f)) * 0.5f, glm::vec3(0.01f));
            d.density = std::max(1.0f, ScriptVM::fieldNumber(L, 1, "density", 500.0f));
            d.friction = ScriptVM::fieldNumber(L, 1, "friction", 0.6f);
            d.restitution = ScriptVM::fieldNumber(L, 1, "bounce", 0.1f);
            d.material = uint32_t(std::max(0.0f, ScriptVM::fieldNumber(L, 1, "material", 0.0f)));
            if (ScriptVM::fieldBool(L, 1, "static", false)) d.motion = RigidWorld::Motion::Static;
            const glm::vec3 color = ScriptVM::fieldVec3(L, 1, "color", glm::vec3(0.8f));
            const RigidWorld::BodyId id = rbm->world().add(d);
            if (id == RigidWorld::kNoBody) return luaL_error(L, "physics: body limit reached");
            m_bodies.push_back({id, src, sphere, sphere ? glm::vec3(d.radius) : d.halfExtents, color});
            lua_pushinteger(L, lua_Integer(id));
            return 1;
        };
        auto owned = [this](lua_State* L, int idx) -> uint32_t {
            const auto id = uint32_t(luaL_checkinteger(L, idx));
            for (const Body& b : m_bodies)
                if (b.id == id) return id;
            luaL_error(L, "physics: %d is not a body spawned by a script", int(id));
            return 0;
        };
        vm.registerFunction("physics", "box", [spawn](lua_State* L) { return spawn(L, false); });
        vm.registerFunction("physics", "sphere", [spawn](lua_State* L) { return spawn(L, true); });
        vm.registerFunction("physics", "remove", [this, rbm, owned](lua_State* L) {
            const uint32_t id = owned(L, 1);
            rbm->world().remove(id);
            m_bodies.erase(std::remove_if(m_bodies.begin(), m_bodies.end(), [&](const Body& b) { return b.id == id; }), m_bodies.end());
            return 0;
        });
        // Reading works on any body (contacts report level bodies too); writing only on script bodies.
        vm.registerFunction("physics", "position", [rbm](lua_State* L) { ScriptVM::pushVec3(L, rbm->world().position(uint32_t(luaL_checkinteger(L, 1)))); return 1; });
        vm.registerFunction("physics", "velocity", [rbm](lua_State* L) { ScriptVM::pushVec3(L, rbm->world().velocity(uint32_t(luaL_checkinteger(L, 1)))); return 1; });
        vm.registerFunction("physics", "setVelocity", [rbm, owned](lua_State* L) { rbm->world().setVelocity(owned(L, 1), ScriptVM::toVec3(L, 2)); return 0; });
        vm.registerFunction("physics", "impulse", [rbm, owned](lua_State* L) {
            const uint32_t id = owned(L, 1);
            rbm->world().addImpulse(id, ScriptVM::toVec3(L, 2), lua_istable(L, 3) ? ScriptVM::toVec3(L, 3) : rbm->world().position(id));
            return 0;
        });
        vm.registerFunction("physics", "raycast", [rbm](lua_State* L) {
            const RigidWorld::RayHit h = rbm->world().raycast(ScriptVM::toVec3(L, 1), ScriptVM::toVec3(L, 2), float(luaL_optnumber(L, 3, 100.0)));
            if (!h.hit) { lua_pushnil(L); return 1; }
            lua_createtable(L, 0, 5);
            ScriptVM::pushVec3(L, h.point);
            lua_setfield(L, -2, "pos");
            ScriptVM::pushVec3(L, h.normal);
            lua_setfield(L, -2, "normal");
            lua_pushnumber(L, h.distance);
            lua_setfield(L, -2, "distance");
            lua_pushinteger(L, lua_Integer(h.body));
            lua_setfield(L, -2, "body");
            lua_pushinteger(L, lua_Integer(h.material));
            lua_setfield(L, -2, "material");
            return 1;
        });
        vm.registerFunction("physics", "count", [this](lua_State* L) { lua_pushinteger(L, lua_Integer(m_bodies.size())); return 1; });
    }
#endif
    bindModels();
    bindBreakables();
    bindUi();
    bindScenes();
    bindNet();
}

void ScriptModule::fixedUpdate(const FixedUpdateContext& ctx) {
    m_vm->callHook("Tick", ctx.fixedDt, ctx.tickIndex);
}

void ScriptModule::update(const UpdateContext& ctx) {
    m_dt = ctx.dt;
    m_time = ctx.totalTime;
    if (!m_inited) {
        m_inited = true;
        m_vm->callHook("Init");
    }
    bool rescan = (m_scanTimer -= ctx.dt) <= 0.0;
#if KKE_ENABLE_NET
    // Hosting, joining or leaving changes which scripts run here (sv_*).
    if (auto* net = m_app->getModule<NetModule>(); net && net->authority() != m_authority) {
        m_authority = net->authority();
        log("", m_authority ? "this game is the server now: loading sv_ scripts" : "joined a game: sv_ scripts run on the host");
        rescan = true;
    }
#endif
    if (rescan) {
        m_scanTimer = 0.5; // hot reload: poll modification times twice a second
        scanFolder(true);
    }
    m_vm->updateTimers(m_time);
    dispatchNet();
    dispatchClicks();
    checkBreaks();
#if KKE_ENABLE_JOLT
    if (auto* rbm = m_app->getModule<RigidBodyModule>(); rbm && m_vm->hookCount("Contact") > 0) {
        int n = 0;
        for (const RigidWorld::Contact& c : rbm->frameContacts()) {
            if (n++ >= maxContactsPerFrame) break;
            m_vm->callHookWith("Contact", [&](lua_State* L) {
                lua_createtable(L, 0, 6);
                lua_pushinteger(L, lua_Integer(c.a));
                lua_setfield(L, -2, "a");
                lua_pushinteger(L, lua_Integer(c.b));
                lua_setfield(L, -2, "b");
                lua_pushnumber(L, c.speed);
                lua_setfield(L, -2, "speed");
                lua_pushinteger(L, lua_Integer(c.materialA));
                lua_setfield(L, -2, "materialA");
                lua_pushinteger(L, lua_Integer(c.materialB));
                lua_setfield(L, -2, "materialB");
                ScriptVM::pushVec3(L, c.point);
                lua_setfield(L, -2, "pos");
                return 1;
            });
        }
    }
#endif
    m_vm->callHook("Think", ctx.dt);
    // Per-script CPU time this frame, smoothed for the panel.
    for (const auto& [source, total] : m_vm->cpuTimes()) {
        const double ms = (total - m_cpuLast[source]) * 1000.0;
        m_cpuLast[source] = total;
        double& shown = m_cpuMs[source];
        shown += (ms - shown) * 0.1;
    }

    // One mesh for every script body, rebuilt each frame (same approach as
    // kke_demo's crates: one draw instead of hundreds).
    static std::vector<Vertex> v;
    static std::vector<uint32_t> idx;
    v.clear();
    idx.clear();
#if KKE_ENABLE_JOLT
    if (auto* rbm = m_app->getModule<RigidBodyModule>()) {
        for (const Body& b : m_bodies) {
            const glm::mat4 t = rbm->world().transform(b.id);
            if (b.sphere) appendSphere(t, b.half.x, b.color, v, idx);
            else appendBox(t, b.half, b.color, v, idx);
        }
    }
#endif
    m_batchIndices = idx.size();
    if (!idx.empty()) m_batch->upload(v, idx);
}

void ScriptModule::render(const RenderContext& ctx) {
    if (m_batchIndices) m_batch->draw(ctx, glm::mat4(1.0f), 0.0f, 0.6f);
}

void ScriptModule::renderShadow(const ShadowRenderContext& ctx) {
    if (m_batchIndices) m_batch->drawShadow(ctx, glm::mat4(1.0f));
}

void ScriptModule::renderUi() {
    const float s = ImGui::GetFontSize() / 13.0f;
    ImGui::SetNextWindowSize(ImVec2(420 * s, 320 * s), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Scripts (Lua)")) { ImGui::End(); return; }
    ImGui::Text("Folder: %s   (edit a file and save: it reloads)", m_dir.c_str());
    ImGui::Text("Memory %.1f KB, bodies %zu, models %zu, breakables %zu, UI %zu, errors %zu", double(m_vm->memoryUsed()) / 1024.0,
                m_bodies.size(), m_models.size(), m_breakables.size(), m_documents.size(), m_vm->errors().size());
    for (const ScriptFile& f : m_files) {
        const bool stopped = std::find(m_vm->stoppedScripts().begin(), m_vm->stoppedScripts().end(), f.path) != m_vm->stoppedScripts().end();
        ImGui::TextColored(f.ok ? ImVec4(0.5f, 1, 0.5f, 1) : ImVec4(1, 0.4f, 0.4f, 1), "%s %s", stopped ? "STOP" : f.ok ? "OK  " : "ERR ", f.path.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%.2f ms", m_cpuMs[f.path]);
        ImGui::SameLine();
        ImGui::PushID(f.path.c_str());
        if (ImGui::SmallButton("Reload")) {
            for (ScriptFile& g : m_files)
                if (g.path == f.path) g.ok = m_vm->reloadFile(g.path);
        }
        ImGui::PopID();
    }
    if (ImGui::Button("Reload all")) reloadAll();
    ImGui::SameLine();
    if (ImGui::Button("Clear console")) { m_console.clear(); m_vm->clearErrors(); }
    ImGui::Separator();
    const float footer = ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("console", ImVec2(0, -footer), true);
    for (const std::string& line : m_console) {
        const bool err = line.find("ERROR:") != std::string::npos;
        if (err) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.45f, 0.45f, 1));
        ImGui::TextWrapped("%s", line.c_str());
        if (err) ImGui::PopStyleColor();
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    // The console line runs in its own globals, or in one script's (to
    // look at or poke its state: print(score)).
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.3f);
    if (ImGui::BeginCombo("##target", std::filesystem::path(m_consoleTarget).filename().string().c_str())) {
        if (ImGui::Selectable("console", m_consoleTarget == "console")) m_consoleTarget = "console";
        for (const ScriptFile& f : m_files)
            if (ImGui::Selectable(std::filesystem::path(f.path).filename().string().c_str(), m_consoleTarget == f.path)) m_consoleTarget = f.path;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##lua", "Lua, e.g. print(camera.position())  (Enter runs)", m_consoleInput, sizeof(m_consoleInput),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
        m_console.push_back("> " + std::string(m_consoleInput));
        m_vm->runString(m_consoleInput, m_consoleTarget);
        m_consoleInput[0] = 0;
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::End();
}

void ScriptModule::shutdown() {
    if (m_vm && m_inited) m_vm->callHook("Shutdown");
    if (m_app) releaseAll();
    m_batch.reset();
}

} // namespace kke
