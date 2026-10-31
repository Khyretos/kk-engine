// world.*: streaming a big world and its levels from Lua (kke/WorldStream.h decides, this loads and unloads;
// docs/WORLD_STREAMING.md).
#include "kke/Application.h"
#include "kke/ScriptVM.h"
#include "kke/modules/ScriptModule.h"

#include <lauxlib.h>
#include <lua.h>

#include <algorithm>
#include <cstdio>

namespace kke {

namespace {

const char* stateName(CellState s) {
    switch (s) {
    case CellState::Unknown: return "unknown";
    case CellState::Unloaded: return "unloaded";
    case CellState::Loading: return "loading";
    case CellState::Loaded: return "loaded";
    case CellState::Failed: return "failed";
    }
    return "unknown";
}

bool parseGridId(const std::string& id, long& x, long& z) { return std::sscanf(id.c_str(), "%ld,%ld", &x, &z) == 2; }

} // namespace

void ScriptModule::bindWorld() {
    ScriptVM& vm = *m_vm;
    // world.config{loadRadius=100, unloadRadius=130, loads=1, inFlight=1, unloads=2, resident=0, retry=5}
    //   loads/unloads: most started per frame; inFlight: loads going at once; resident: most cells loaded at once (0 = any).
    vm.registerFunction("world", "config", [this](lua_State* L) {
        luaL_checktype(L, 1, LUA_TTABLE);
        StreamConfig c = m_world.config();
        c.loadRadius = ScriptVM::fieldNumber(L, 1, "loadRadius", c.loadRadius);
        c.unloadRadius = ScriptVM::fieldNumber(L, 1, "unloadRadius", std::max(c.unloadRadius, c.loadRadius * 1.3f));
        c.maxLoadsPerUpdate = int(ScriptVM::fieldNumber(L, 1, "loads", float(c.maxLoadsPerUpdate)));
        c.maxInFlight = int(ScriptVM::fieldNumber(L, 1, "inFlight", float(c.maxInFlight)));
        c.maxUnloadsPerUpdate = int(ScriptVM::fieldNumber(L, 1, "unloads", float(c.maxUnloadsPerUpdate)));
        c.maxResident = size_t(std::max(0.0f, ScriptVM::fieldNumber(L, 1, "resident", float(c.maxResident))));
        c.retryAfter = ScriptVM::fieldNumber(L, 1, "retry", c.retryAfter);
        m_world.setConfig(c);
        m_worldUsed = true;
        return 0;
    });

    // world.cell(id, {box = {min, max}  or  center = Vec, size = Vec|number,
    //                 scene = "name", origin = Vec, load = fn(id), unload = fn(id),
    //                 priority = 0, always = false, radius = nil, unloadRadius = nil}) -> true, or false when the id is taken
    // A cell is a part of the world: a scene loaded where an observer comes near, and/or your own functions.
    vm.registerFunction("world", "cell", [this](lua_State* L) {
        const std::string id = luaL_checkstring(L, 1);
        luaL_checktype(L, 2, LUA_TTABLE);
        StreamCell c;
        c.id = id;
        lua_getfield(L, 2, "box");
        const bool hasBox = lua_istable(L, -1);
        if (hasBox) {
            c.box.min = ScriptVM::fieldVec3(L, lua_gettop(L), "min", glm::vec3(0.0f));
            c.box.max = ScriptVM::fieldVec3(L, lua_gettop(L), "max", glm::vec3(0.0f));
        }
        lua_pop(L, 1);
        if (!hasBox) {
            const glm::vec3 centre = ScriptVM::fieldVec3(L, 2, "center", glm::vec3(0.0f));
            const glm::vec3 half = ScriptVM::fieldVec3(L, 2, "size", glm::vec3(64.0f)) * 0.5f;
            c.box.min = centre - half;
            c.box.max = centre + half;
        }
        c.box.max = glm::max(c.box.max, c.box.min);
        c.priority = int(ScriptVM::fieldNumber(L, 2, "priority", 0.0f));
        c.always = ScriptVM::fieldBool(L, 2, "always", false);
        c.loadRadius = ScriptVM::fieldNumber(L, 2, "radius", -1.0f);
        c.unloadRadius = ScriptVM::fieldNumber(L, 2, "unloadRadius", -1.0f);
        WorldCell w;
        w.source = m_vm->currentSource();
        w.scene = ScriptVM::fieldString(L, 2, "scene", "");
        w.origin = ScriptVM::fieldVec3(L, 2, "origin", c.box.min);
        lua_getfield(L, 2, "load");
        if (lua_isfunction(L, -1)) w.loadRef = m_vm->ref(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "unload");
        if (lua_isfunction(L, -1)) w.unloadRef = m_vm->ref(L, -1);
        lua_pop(L, 1);
        if (w.scene.empty() && !w.loadRef) return luaL_error(L, "world.cell '%s': give it a scene = \"name\" and/or a load = function", id.c_str());
        if (!m_world.addCell(c)) {
            m_vm->unref(w.loadRef);
            m_vm->unref(w.unloadRef);
            lua_pushboolean(L, 0);
            return 1;
        }
        m_worldCells[id] = std::move(w);
        m_worldUsed = true;
        lua_pushboolean(L, 1);
        return 1;
    });

    // world.grid{size = 64, load = fn(x, z), unload = fn(x, z), scene = fn(x, z) -> "name" | nil}
    // An open world as a grid of square cells that appear where observers come within the load radius (world.config)
    // and are forgotten once unloaded: a chunk is loaded by `load` (spawn what belongs in it, from the cell numbers x, z; the
    // cell covers x*size .. (x+1)*size) or, when `scene` returns a name, by loading that scene at the cell's corner.
    vm.registerFunction("world", "grid", [this](lua_State* L) {
        luaL_checktype(L, 1, LUA_TTABLE);
        const float size = ScriptVM::fieldNumber(L, 1, "size", 0.0f);
        if (size <= 0.0f) return luaL_error(L, "world.grid: size must be more than 0 (metres across a cell)");
        m_vm->unref(m_worldGrid.loadRef);
        m_vm->unref(m_worldGrid.unloadRef);
        m_vm->unref(m_worldGrid.sceneRef);
        m_worldGrid = WorldGrid{};
        m_worldGrid.source = m_vm->currentSource();
        for (auto [key, ref] : { std::pair<const char*, int*>{ "load", &m_worldGrid.loadRef }, { "unload", &m_worldGrid.unloadRef }, { "scene", &m_worldGrid.sceneRef } }) {
            lua_getfield(L, 1, key);
            if (lua_isfunction(L, -1)) *ref = m_vm->ref(L, -1);
            lua_pop(L, 1);
        }
        if (!m_worldGrid.loadRef && !m_worldGrid.sceneRef) return luaL_error(L, "world.grid: give it load = function(x, z) and/or scene = function(x, z) returning a scene name");
        m_worldGrid.active = true;
        StreamConfig c = m_world.config();
        c.gridSize = size;
        m_world.setConfig(c);
        m_worldUsed = true;
        return 0;
    });

    // world.observer(Vec): where the player is (default: the camera). world.observers({Vec, Vec}): several. world.observer(nil): back to the camera.
    vm.registerFunction("world", "observer", [this](lua_State* L) {
        m_worldObservers.clear();
        if (!lua_isnoneornil(L, 1)) m_worldObservers.push_back(ScriptVM::toVec3(L, 1));
        return 0;
    });
    vm.registerFunction("world", "observers", [this](lua_State* L) {
        m_worldObservers.clear();
        if (lua_istable(L, 1)) {
            const int n = int(lua_rawlen(L, 1));
            for (int i = 1; i <= n && i <= 16; ++i) {
                lua_rawgeti(L, 1, i);
                m_worldObservers.push_back(ScriptVM::toVec3(L, -1));
                lua_pop(L, 1);
            }
        }
        return 0;
    });

    vm.registerFunction("world", "state", [this](lua_State* L) {
        lua_pushstring(L, stateName(m_world.state(luaL_checkstring(L, 1))));
        return 1;
    });
    vm.registerFunction("world", "loaded", [this](lua_State* L) {
        lua_pushboolean(L, m_world.isLoaded(luaL_checkstring(L, 1)));
        return 1;
    });
    // world.cells() -> ids of the cells that are loaded or loading.
    vm.registerFunction("world", "cells", [this](lua_State* L) {
        const std::vector<std::string> ids = m_world.resident();
        lua_createtable(L, int(ids.size()), 0);
        int i = 1;
        for (const std::string& id : ids) {
            lua_pushstring(L, id.c_str());
            lua_rawseti(L, -2, i++);
        }
        return 1;
    });
    // world.stats() -> {cells, loaded, loading, failed, loads, unloads, failures}
    vm.registerFunction("world", "stats", [this](lua_State* L) {
        const WorldStreamer::Stats s = m_world.stats();
        lua_createtable(L, 0, 7);
        for (auto [key, value] : { std::pair<const char*, size_t>{ "cells", s.cells }, { "loaded", s.loaded }, { "loading", s.loading }, { "failed", s.failed },
                                   { "loads", s.loadsAsked }, { "unloads", s.unloadsAsked }, { "failures", s.failures } }) {
            lua_pushinteger(L, lua_Integer(value));
            lua_setfield(L, -2, key);
        }
        return 1;
    });
    // world.remove(id): unloads it now (if it is loaded) and forgets it.
    vm.registerFunction("world", "remove", [this](lua_State* L) {
        const std::string id = luaL_checkstring(L, 1);
        auto it = m_worldCells.find(id);
        if (it == m_worldCells.end() || it->second.source != m_vm->currentSource()) {
            lua_pushboolean(L, 0);
            return 1;
        }
        if (m_world.isLoaded(id)) {
            if (it->second.unloadRef) m_vm->callRef(it->second.unloadRef, it->second.source, [&](lua_State* S) { lua_pushstring(S, id.c_str()); return 1; });
            if (it->second.sceneId) unloadSceneById(it->second.sceneId);
        }
        m_vm->unref(it->second.loadRef);
        m_vm->unref(it->second.unloadRef);
        m_world.removeCell(id);
        m_worldCells.erase(it);
        lua_pushboolean(L, 1);
        return 1;
    });
}

// Each frame: where the observers are, what the streamer wants done, and doing it.
void ScriptModule::updateWorld(float dt) {
    if (!m_worldUsed) return;
    m_world.setObservers(m_worldObservers.empty() ? std::vector<glm::vec3>{ shownCamera().position } : m_worldObservers);
    for (const StreamAction& a : m_world.update(dt)) {
        const std::string& id = a.id;
        if (a.kind == StreamAction::Kind::Unload) {
            auto it = m_worldCells.find(id);
            if (it != m_worldCells.end()) {
                WorldCell& w = it->second;
                long gx = 0, gz = 0;
                if (w.implicit && parseGridId(id, gx, gz)) {
                    if (m_worldGrid.unloadRef)
                        m_vm->callRef(m_worldGrid.unloadRef, m_worldGrid.source, [&](lua_State* S) { lua_pushinteger(S, gx); lua_pushinteger(S, gz); return 2; });
                } else if (w.unloadRef) {
                    m_vm->callRef(w.unloadRef, w.source, [&](lua_State* S) { lua_pushstring(S, id.c_str()); return 1; });
                }
                if (w.sceneId) unloadSceneById(w.sceneId);
                if (w.implicit) m_worldCells.erase(it);
                else w.sceneId = 0;
            }
            m_vm->callHook("CellUnloaded", id);
            continue;
        }
        // Load: an explicit cell's scene and function, or a grid cell's.
        auto it = m_worldCells.find(id);
        long gx = 0, gz = 0;
        if (it == m_worldCells.end()) {
            if (!m_worldGrid.active || !parseGridId(id, gx, gz)) {
                m_world.failed(id); // nothing knows how to load it
                continue;
            }
            WorldCell w;
            w.implicit = true;
            w.source = m_worldGrid.source;
            const StreamCell* sc = m_world.cell(id);
            w.origin = sc ? sc->box.min : glm::vec3(0.0f);
            w.origin.y = 0.0f;
            it = m_worldCells.emplace(id, std::move(w)).first;
        } else if (it->second.implicit) {
            parseGridId(id, gx, gz);
        }
        WorldCell& w = it->second;
        bool ok = true;
        std::string scene = w.scene;
        if (w.implicit && m_worldGrid.sceneRef) {
            std::string name;
            ok = m_vm->callRefResults(m_worldGrid.sceneRef, m_worldGrid.source, [&](lua_State* S) { lua_pushinteger(S, gx); lua_pushinteger(S, gz); return 2; }, 1,
                                      [&](lua_State* S) { if (lua_isstring(S, -1)) name = lua_tostring(S, -1); });
            scene = name;
        }
        if (ok && !scene.empty()) {
            std::string error;
            size_t missing = 0;
            w.sceneId = loadSceneByName(scene, w.origin, w.source, error, &missing);
            if (!w.sceneId) {
                log(w.source, "world: cell '" + id + "': " + error);
                ok = false;
            } else {
                m_scenes.back().streamed = true; // does not count against maxScenesPerScript
                if (missing) log(w.source, "world: cell '" + id + "' (scene '" + scene + "'): " + std::to_string(missing) + " asset(s) not installed, left out");
            }
        }
        if (ok) {
            if (w.implicit && m_worldGrid.loadRef)
                ok = m_vm->callRef(m_worldGrid.loadRef, m_worldGrid.source, [&](lua_State* S) { lua_pushinteger(S, gx); lua_pushinteger(S, gz); return 2; });
            else if (!w.implicit && w.loadRef)
                ok = m_vm->callRef(w.loadRef, w.source, [&](lua_State* S) { lua_pushstring(S, id.c_str()); return 1; });
        }
        if (!ok) { // a failed load leaves nothing half done
            if (w.sceneId) unloadSceneById(w.sceneId);
            w.sceneId = 0;
            if (w.implicit) m_worldCells.erase(it);
            m_world.failed(id);
            continue;
        }
        m_world.loaded(id);
        m_vm->callHook("CellLoaded", id);
    }
}

// A script's cells go with it (its scenes were unloaded with the rest of what it made): no unload functions run.
void ScriptModule::releaseWorld(const std::string& source) {
    for (auto it = m_worldCells.begin(); it != m_worldCells.end();) {
        if (it->second.source != source) {
            ++it;
            continue;
        }
        m_vm->unref(it->second.loadRef);
        m_vm->unref(it->second.unloadRef);
        m_world.removeCell(it->first);
        it = m_worldCells.erase(it);
    }
    if (m_worldGrid.active && m_worldGrid.source == source) {
        m_vm->unref(m_worldGrid.loadRef);
        m_vm->unref(m_worldGrid.unloadRef);
        m_vm->unref(m_worldGrid.sceneRef);
        m_worldGrid = WorldGrid{};
        StreamConfig c = m_world.config();
        c.gridSize = 0.0f;
        m_world.setConfig(c);
    }
}

} // namespace kke
