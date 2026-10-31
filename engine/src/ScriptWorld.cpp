#include "kke/ScriptWorld.h"

#include "kke/ScriptVM.h"

#include <lauxlib.h>
#include <lua.h>

#include <nlohmann/json.hpp>

#include <cmath>

namespace kke {

using nlohmann::json;

// The streamer's hands: it asks the script's functions to load and unload cells.
class ScriptWorld::Backend : public StreamBackend {
public:
    ScriptVM* vm = nullptr;
    std::string source;
    int loadRef = 0, unloadRef = 0;
    bool callbacks = true;
    struct Status {
        CellLoad state = CellLoad::Pending;
        std::string reason;
    };
    std::map<std::string, Status> status;

    static void pushCell(lua_State* L, const StreamCell& c) {
        lua_createtable(L, 0, 8);
        lua_pushstring(L, c.name.c_str());
        lua_setfield(L, -2, "name");
        lua_pushstring(L, c.data.c_str());
        lua_setfield(L, -2, "data");
        const struct { const char* key; double v; } numbers[] = {
            { "minX", c.min.x }, { "minZ", c.min.y }, { "maxX", c.max.x }, { "maxZ", c.max.y },
            { "x", 0.5 * (static_cast<double>(c.min.x) + c.max.x) }, { "z", 0.5 * (static_cast<double>(c.min.y) + c.max.y) },
        };
        for (const auto& n : numbers) {
            lua_pushnumber(L, n.v);
            lua_setfield(L, -2, n.key);
        }
        lua_pushboolean(L, c.persistent);
        lua_setfield(L, -2, "persistent");
    }

    bool begin(const StreamCell& c, std::string*) override {
        status[c.name] = { CellLoad::Pending, {} };
        if (!callbacks || loadRef <= 0) {
            status[c.name].state = CellLoad::Ready; // no function: nothing to wait for
            return true;
        }
        Status result;
        const bool ok = vm->callRefResults(
            loadRef, source, [&](lua_State* L) { pushCell(L, c); return 1; }, 2,
            [&](lua_State* L) {
                if (lua_isstring(L, -2) && std::string(lua_tostring(L, -2)) == "pending") {
                    result.state = CellLoad::Pending;
                } else if (lua_isboolean(L, -2) && !lua_toboolean(L, -2)) {
                    result.state = CellLoad::Failed;
                    result.reason = lua_isstring(L, -1) ? lua_tostring(L, -1) : "the load function answered false";
                } else {
                    result.state = CellLoad::Ready;
                }
            });
        if (!ok) result = { CellLoad::Failed, "the load function raised an error (see the script's errors)" };
        status[c.name] = result; // a failure is reported by poll, which fails the cell and retries it later
        return true;
    }
    CellLoad poll(const StreamCell& c, std::string* error) override {
        const auto it = status.find(c.name);
        if (it == status.end()) return CellLoad::Failed;
        if (it->second.state == CellLoad::Failed && error) *error = it->second.reason;
        return it->second.state;
    }
    void unload(const StreamCell& c) override {
        status.erase(c.name);
        if (callbacks && unloadRef > 0) vm->callRef(unloadRef, source, [&](lua_State* L) { pushCell(L, c); return 1; });
    }
};

ScriptWorld::ScriptWorld() : m_streamer(nullptr), m_backend(std::make_unique<Backend>()) { m_streamer.setBackend(m_backend.get()); }
ScriptWorld::~ScriptWorld() = default;

void ScriptWorld::release(const std::string& source) {
    if (!m_active || m_owner != source) return;
    m_backend->callbacks = false; // its functions are gone with it
    if (m_vm) {
        m_vm->unref(m_backend->loadRef);
        m_vm->unref(m_backend->unloadRef);
    }
    m_backend->loadRef = m_backend->unloadRef = 0;
    m_streamer.clear();
    m_backend->status.clear();
    m_active = false;
    m_owner.clear();
}

void ScriptWorld::update(float dt, ScriptVM& vm, const glm::vec3& cameraPosition) {
    if (!m_active) return;
    if (m_followCamera) m_streamer.setFocus("camera", cameraPosition);
    m_streamer.update(dt);
    for (const StreamEvent& e : m_streamer.takeEvents()) {
        const char* hook = nullptr;
        switch (e.kind) {
        case StreamEvent::Kind::Loaded: hook = "CellLoaded"; break;
        case StreamEvent::Kind::Unloaded: hook = "CellUnloaded"; break;
        case StreamEvent::Kind::Failed: hook = "CellFailed"; break;
        case StreamEvent::Kind::Retrying: break;
        }
        if (!hook) continue;
        vm.callHookWith(hook, [&e](lua_State* L) {
            lua_createtable(L, 0, 2);
            lua_pushstring(L, e.name.c_str());
            lua_setfield(L, -2, "name");
            if (!e.reason.empty()) {
                lua_pushstring(L, e.reason.c_str());
                lua_setfield(L, -2, "reason");
            }
            return 1;
        });
    }
}

namespace {

bool number(const json& j, const char* key, float& out) {
    const auto it = j.find(key);
    if (it == j.end()) return true;
    if (!it->is_number()) return false;
    out = it->get<float>();
    return std::isfinite(out);
}

// {x, z} or {x, y, z} (a Vec): the ground position.
bool ground(const json& j, glm::vec2& out) {
    if (!j.is_array() || (j.size() != 2 && j.size() != 3)) return false;
    for (const json& v : j)
        if (!v.is_number()) return false;
    out = { j[0].get<float>(), j[j.size() - 1].get<float>() };
    return true;
}

} // namespace

void ScriptWorld::bind(ScriptVM& vm) {
    m_vm = &vm;
    m_backend->vm = &vm;
    auto fail = [](lua_State* L, const std::string& why) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, why.c_str());
        return 2;
    };
    // world.stream{cells = {{name, min, max, ...}} and/or grid = {prefix, size, x = {a, b}, z = {a, b}},
    //              loadRadius, unloadRadius, requiredRadius, maxLoading, maxStarts, loadTimeout, retryDelay,
    //              maxRetries, followCamera, load = fn(cell), unload = fn(cell)} -> true, or false and why
    vm.registerFunction("world", "stream", [this, &vm, fail](lua_State* L) {
        luaL_checktype(L, 1, LUA_TTABLE);
        const json j = ScriptVM::toJson(L, 1);
        StreamConfig cfg;
        bool ok = number(j, "loadRadius", cfg.loadRadius) && number(j, "unloadRadius", cfg.unloadRadius) && number(j, "requiredRadius", cfg.requiredRadius) &&
                  number(j, "loadTimeout", cfg.loadTimeout) && number(j, "retryDelay", cfg.retryDelay);
        float maxLoading = static_cast<float>(cfg.maxLoading), maxStarts = static_cast<float>(cfg.maxStartsPerUpdate), retries = static_cast<float>(cfg.maxRetries);
        ok = ok && number(j, "maxLoading", maxLoading) && number(j, "maxStarts", maxStarts) && number(j, "maxRetries", retries);
        if (!ok) return fail(L, "radii, counts and times are numbers");
        cfg.maxLoading = std::max(1, static_cast<int>(maxLoading));
        cfg.maxStartsPerUpdate = std::max(1, static_cast<int>(maxStarts));
        cfg.maxRetries = std::max(0, static_cast<int>(retries));
        if (cfg.loadRadius <= 0.0f) return fail(L, "loadRadius must be above 0");

        // build into a fresh streamer, so a mistake leaves the world as it was
        WorldStreamer fresh(nullptr, cfg);
        std::string error;
        if (const auto cells = j.find("cells"); cells != j.end() && !(cells->is_object() && cells->empty())) {
            if (!cells->is_array()) return fail(L, "cells is a list");
            for (size_t i = 0; i < cells->size(); ++i) {
                const json& cj = (*cells)[i];
                const std::string where = "cells[" + std::to_string(i) + "]";
                if (!cj.is_object() || !cj.contains("name") || !cj["name"].is_string()) return fail(L, where + " needs a name");
                StreamCell c;
                c.name = cj["name"].get<std::string>();
                if (!cj.contains("min") || !cj.contains("max") || !ground(cj["min"], c.min) || !ground(cj["max"], c.max))
                    return fail(L, where + " ('" + c.name + "') needs min and max: {x, z} (or a Vec)");
                if (cj.contains("data") && cj["data"].is_string()) c.data = cj["data"].get<std::string>();
                else c.data = c.name;
                if (cj.contains("persistent") && cj["persistent"].is_boolean()) c.persistent = cj["persistent"].get<bool>();
                if (!number(cj, "loadRadius", c.loadRadius) || !number(cj, "unloadRadius", c.unloadRadius) || !number(cj, "bias", c.bias)) return fail(L, where + ": radii and bias are numbers");
                if (!fresh.addCell(c, &error)) return fail(L, where + ": " + error);
            }
        }
        if (const auto g = j.find("grid"); g != j.end() && g->is_object() && !g->empty()) {
            const std::string prefix = g->contains("prefix") && (*g)["prefix"].is_string() ? (*g)["prefix"].get<std::string>() : "cell";
            float size = 0.0f;
            if (!number(*g, "size", size) || !(size > 0.0f)) return fail(L, "grid needs a size above 0");
            auto range = [&](const char* key, int& a, int& b) {
                const auto it = g->find(key);
                if (it == g->end() || !it->is_array() || it->size() != 2 || !(*it)[0].is_number() || !(*it)[1].is_number()) return false;
                a = static_cast<int>((*it)[0].get<double>());
                b = static_cast<int>((*it)[1].get<double>());
                return true;
            };
            int x0 = 0, x1 = 0, z0 = 0, z1 = 0;
            if (!range("x", x0, x1) || !range("z", z0, z1)) return fail(L, "grid needs x = {first, last} and z = {first, last} (cell indices)");
            if (static_cast<int64_t>(x1 - x0 + 1) * (z1 - z0 + 1) > static_cast<int64_t>(WorldStreamer::kMaxCells)) return fail(L, "that grid has too many cells");
            if (fresh.addGrid(prefix, size, x0, z0, x1, z1, &error) == 0 && !error.empty()) return fail(L, "grid: " + error);
            if (!error.empty()) return fail(L, "grid: " + error);
        }
        if (fresh.cellCount() == 0) return fail(L, "no cells (give cells = {...} or grid = {...})");

        // from here on it takes over: the old world goes (its unload function sees each cell go)
        if (m_active) {
            m_streamer.clear();
            if (m_backend->loadRef) vm.unref(m_backend->loadRef);
            if (m_backend->unloadRef) vm.unref(m_backend->unloadRef);
        }
        m_backend->status.clear();
        m_backend->callbacks = true;
        m_backend->source = vm.currentSource();
        lua_getfield(L, 1, "load");
        m_backend->loadRef = lua_isfunction(L, -1) ? vm.ref(L, -1) : 0;
        lua_pop(L, 1);
        lua_getfield(L, 1, "unload");
        m_backend->unloadRef = lua_isfunction(L, -1) ? vm.ref(L, -1) : 0;
        lua_pop(L, 1);
        m_streamer = std::move(fresh);
        m_streamer.setBackend(m_backend.get());
        m_owner = vm.currentSource();
        m_followCamera = j.contains("followCamera") && j["followCamera"].is_boolean() && j["followCamera"].get<bool>();
        m_active = true;
        lua_pushboolean(L, 1);
        return 1;
    });
    // world.focus(id, pos): who the world streams around (the player, a second player); world.unfocus(id)
    vm.registerFunction("world", "focus", [this](lua_State* L) {
        const std::string id = luaL_checkstring(L, 1);
        lua_pushboolean(L, m_streamer.setFocus(id, ScriptVM::toVec3(L, 2)));
        return 1;
    });
    vm.registerFunction("world", "unfocus", [this](lua_State* L) {
        lua_pushboolean(L, m_streamer.removeFocus(luaL_checkstring(L, 1)));
        return 1;
    });
    vm.registerFunction("world", "state", [this](lua_State* L) {
        const std::string name = luaL_checkstring(L, 1);
        if (!m_streamer.hasCell(name)) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushstring(L, cellStateName(m_streamer.state(name)));
        return 1;
    });
    auto pushList = [](lua_State* L, const std::vector<std::string>& names) {
        lua_createtable(L, static_cast<int>(names.size()), 0);
        for (size_t i = 0; i < names.size(); ++i) {
            lua_pushstring(L, names[i].c_str());
            lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
        }
    };
    vm.registerFunction("world", "loaded", [this, pushList](lua_State* L) {
        pushList(L, m_streamer.loaded());
        return 1;
    });
    vm.registerFunction("world", "loading", [this, pushList](lua_State* L) {
        pushList(L, m_streamer.loading());
        return 1;
    });
    vm.registerFunction("world", "ready", [this](lua_State* L) {
        lua_pushboolean(L, m_active && m_streamer.ready());
        return 1;
    });
    vm.registerFunction("world", "progress", [this](lua_State* L) {
        lua_pushnumber(L, static_cast<double>(m_streamer.progress()));
        return 1;
    });
    vm.registerFunction("world", "distance", [this](lua_State* L) {
        const std::string name = luaL_checkstring(L, 1);
        if (!m_streamer.hasCell(name)) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushnumber(L, static_cast<double>(m_streamer.distance(name)));
        return 1;
    });
    vm.registerFunction("world", "pin", [this](lua_State* L) {
        lua_pushboolean(L, m_streamer.pin(luaL_checkstring(L, 1), lua_isnone(L, 2) || lua_toboolean(L, 2)));
        return 1;
    });
    vm.registerFunction("world", "force", [this](lua_State* L) {
        lua_pushboolean(L, m_streamer.force(luaL_checkstring(L, 1), lua_isnone(L, 2) || lua_toboolean(L, 2)));
        return 1;
    });
    // world.done(name): a cell whose load answered "pending" is in; world.failed(name, why): it will not be
    vm.registerFunction("world", "done", [this](lua_State* L) {
        const auto it = m_backend->status.find(luaL_checkstring(L, 1));
        const bool pending = it != m_backend->status.end() && it->second.state == CellLoad::Pending;
        if (pending) it->second.state = CellLoad::Ready;
        lua_pushboolean(L, pending);
        return 1;
    });
    vm.registerFunction("world", "failed", [this](lua_State* L) {
        const auto it = m_backend->status.find(luaL_checkstring(L, 1));
        const bool pending = it != m_backend->status.end() && it->second.state == CellLoad::Pending;
        if (pending) it->second = { CellLoad::Failed, lua_isstring(L, 2) ? lua_tostring(L, 2) : "the load failed" };
        lua_pushboolean(L, pending);
        return 1;
    });
    // world.stop(): everything unloads (the unload function runs for each loaded cell) and the world is forgotten
    vm.registerFunction("world", "stop", [this, &vm](lua_State*) {
        if (!m_active) return 0;
        m_streamer.clear();
        vm.unref(m_backend->loadRef);
        vm.unref(m_backend->unloadRef);
        m_backend->loadRef = m_backend->unloadRef = 0;
        m_backend->status.clear();
        m_active = false;
        m_owner.clear();
        return 0;
    });
}

} // namespace kke
