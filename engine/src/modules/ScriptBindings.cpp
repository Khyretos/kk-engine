// ScriptModule's v2 bindings (docs/SCRIPTING.md): models, FEMFX breakables,
// RmlUi documents, scenes and networking. The core bindings (kke, physics,
// audio, input, camera) are in ScriptModule.cpp.
//
// Every binding follows the same rules:
// - What a script makes is remembered with the script's name, and
//   releaseScript() takes it all back out when the script is reloaded,
//   unloaded or stopped. A per-script budget caps each kind.
// - A script only changes what it made itself (an id from another script,
//   or a made-up one, is an error naming the id).
// - Anything that can fail for reasons outside the script (a pack that
//   isn't installed, a scene that doesn't exist) returns nil plus the
//   reason, so a script can carry on; a wrong argument raises an error.
// - Paths a script names stay inside the game's folders: no absolute
//   paths, no "..".
#include "kke/modules/ScriptModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/RmlTextSafety.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/UiModule.h"
#if KKE_ENABLE_FEMFX
#include "kke/FracturePattern.h"
#include "kke/modules/PhysicsModule.h"
#endif
#if KKE_ENABLE_JOLT
#include "kke/modules/RigidBodyModule.h"
#endif
#if KKE_ENABLE_NET
#include "kke/modules/NetModule.h"
#endif

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <SDL3/SDL.h>
#include <lauxlib.h>
#include <lua.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace kke {

namespace {

// A path a script may name: relative, inside its base folder.
bool safeRelativePath(const std::string& path) {
    if (path.empty() || path.size() > 512) return false;
    const std::filesystem::path p(path);
    if (p.is_absolute() || p.has_root_name() || p.has_root_directory()) return false;
    for (const auto& part : p)
        if (part == "..") return false;
    return path.find('\\') == std::string::npos && path.find(':') == std::string::npos;
}

std::string baseDir() {
    const char* base = SDL_GetBasePath();
    return base ? base : "";
}

// Script ids are plain integers; this reads one or raises a clear error.
int checkId(lua_State* L, int index, const char* what) {
    if (!lua_isinteger(L, index)) luaL_error(L, "%s: expected an id (an integer), got %s", what, luaL_typename(L, index));
    return int(lua_tointeger(L, index));
}

glm::mat4 placement(const glm::vec3& pos, float yawDegrees, const glm::vec3& scale) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), pos);
    m = glm::rotate(m, glm::radians(yawDegrees), glm::vec3(0, 1, 0));
    return glm::scale(m, scale);
}

// `scale` may be a number (uniform) or a Vec.
glm::vec3 fieldScale(lua_State* L, int table, float fallback) {
    return ScriptVM::fieldVec3(L, table, "scale", glm::vec3(fallback));
}

} // namespace

// One listener for every script document: a click anywhere in a document
// is matched against the ui.onClick handlers of the clicked element and its
// parents, and run at the next update (not inside RmlUi's event dispatch).
class ScriptClickListener : public Rml::EventListener {
public:
    ScriptClickListener(ScriptModule& module) : m_module(module) {}
    void ProcessEvent(Rml::Event& event) override {
        Rml::Element* e = event.GetTargetElement();
        Rml::ElementDocument* doc = e ? e->GetOwnerDocument() : nullptr;
        if (!doc) return;
        const int id = doc->GetAttribute<int>("data-kke-script-doc", 0);
        for (; e && e != doc; e = e->GetParentNode())
            if (!e->GetId().empty()) m_module.queueClick(id, e->GetId());
    }

private:
    ScriptModule& m_module;
};

void ScriptModule::queueClick(int document, const std::string& elementId) { m_pendingClicks.emplace_back(document, elementId); }

bool ScriptModule::runsHere(const std::string& path, bool authority) {
    const std::string file = std::filesystem::path(path).filename().string();
    if (file.rfind("sv_", 0) == 0) return authority;
    return true;
}

const AssetCatalog& ScriptModule::catalog() {
    if (!m_catalogScanned) {
        m_catalogScanned = true;
        const std::string root = findAssetFolder("assets/synty", {"KKE_ASSETS_DIR", "KKE_SYNTY_DIR"}, baseDir());
        if (!root.empty()) m_catalog = AssetCatalog::scan(root);
        log("", "asset catalog: " + std::to_string(m_catalog.assets.size()) + " assets" + (root.empty() ? " (no packs installed)" : " in " + root));
    }
    return m_catalog;
}

ScriptModule::Document* ScriptModule::document(int id) {
    for (Document& d : m_documents)
        if (d.id == id) return &d;
    return nullptr;
}

// ---------------------------------------------------------------- models.*
void ScriptModule::bindModels() {
    auto* models = m_app->getModule<ModelModule>();
    if (!models) return;
    ScriptVM& vm = *m_vm;

    // models.load(name) -> model id, or nil + reason. `name` is an asset
    // from an installed pack ("SM_Prop_Crate_01") or a file in the game's
    // folder ("models/door.fbx").
    vm.registerFunction("models", "load", [this, models](lua_State* L) {
        const std::string name = luaL_checkstring(L, 1);
        ModelModule::ModelId id = 0;
        if (const CatalogAsset* a = catalog().find(name)) {
            id = models->load(a->path, packLoadOptions(catalog(), *a));
        } else if (safeRelativePath(name)) {
            const std::filesystem::path file = std::filesystem::path(baseDir()) / name;
            std::error_code ec;
            if (std::filesystem::is_regular_file(file, ec)) id = models->load(file.string());
        } else {
            return luaL_error(L, "models.load: '%s' must be an asset name or a relative path inside the game's folder", name.c_str());
        }
        if (!id) {
            lua_pushnil(L);
            lua_pushfstring(L, "'%s' not found (not in any installed pack or the game's folder)", name.c_str());
            return 2;
        }
        lua_pushinteger(L, lua_Integer(id));
        return 1;
    });
    // models.spawn(model, {pos, yaw, scale, tint}) -> instance id
    vm.registerFunction("models", "spawn", [this, models](lua_State* L) {
        const auto model = ModelModule::ModelId(checkId(L, 1, "models.spawn"));
        if (!models->model(model)) return luaL_error(L, "models.spawn: %d is not a loaded model (use models.load)", int(model));
        const std::string src = m_vm->currentSource();
        size_t mine = 0;
        for (const ModelInstance& m : m_models) mine += m.source == src;
        if (mine >= maxModelsPerScript) return luaL_error(L, "models.spawn: this script already has %d models (maxModelsPerScript)", int(maxModelsPerScript));
        const glm::mat4 t = placement(ScriptVM::fieldVec3(L, 2, "pos", glm::vec3(0.0f)), ScriptVM::fieldNumber(L, 2, "yaw", 0.0f), fieldScale(L, 2, 1.0f));
        const ModelModule::InstanceId inst = models->spawn(model, t);
        if (!inst) return luaL_error(L, "models.spawn: the model module refused the instance");
        if (lua_istable(L, 2)) {
            lua_getfield(L, 2, "tint");
            if (lua_istable(L, -1)) models->setTint(inst, ScriptVM::toVec3(L, -1, glm::vec3(1.0f)));
            lua_pop(L, 1);
        }
        m_models.push_back({inst, src});
        lua_pushinteger(L, lua_Integer(inst));
        return 1;
    });
    auto owned = [this](lua_State* L, int idx, const char* what) -> ModelModule::InstanceId {
        const auto id = ModelModule::InstanceId(checkId(L, idx, what));
        for (const ModelInstance& m : m_models)
            if (m.id == id) return id;
        luaL_error(L, "%s: %d is not a model spawned by a script", what, int(id));
        return 0;
    };
    vm.registerFunction("models", "move", [models, owned](lua_State* L) {
        const auto inst = owned(L, 1, "models.move");
        models->setTransform(inst, placement(ScriptVM::toVec3(L, 2), float(luaL_optnumber(L, 3, 0.0)),
                                             lua_istable(L, 4) ? ScriptVM::toVec3(L, 4, glm::vec3(1.0f)) : glm::vec3(float(luaL_optnumber(L, 4, 1.0)))));
        return 0;
    });
    vm.registerFunction("models", "remove", [this, models, owned](lua_State* L) {
        const auto inst = owned(L, 1, "models.remove");
        models->remove(inst);
        m_models.erase(std::remove_if(m_models.begin(), m_models.end(), [&](const ModelInstance& m) { return m.id == inst; }), m_models.end());
        return 0;
    });
    vm.registerFunction("models", "tint", [models, owned](lua_State* L) {
        models->setTint(owned(L, 1, "models.tint"), ScriptVM::toVec3(L, 2, glm::vec3(1.0f)));
        return 0;
    });
    vm.registerFunction("models", "visible", [models, owned](lua_State* L) {
        models->setVisible(owned(L, 1, "models.visible"), lua_toboolean(L, 2));
        return 0;
    });
    // models.play(instance, clip [, loop=true, speed=1]): clip by name, or
    // its number (1-based) in models.clips(); nil stops.
    vm.registerFunction("models", "play", [models, owned](lua_State* L) {
        const auto inst = owned(L, 1, "models.play");
        int clip = -1;
        if (lua_isinteger(L, 2)) {
            clip = int(lua_tointeger(L, 2)) - 1;
        } else if (lua_isstring(L, 2)) {
            const std::string want = lua_tostring(L, 2);
            const ModelData* d = models->model(models->instanceModel(inst));
            if (d)
                for (size_t k = 0; k < d->animations.size(); ++k)
                    if (d->animations[k].name == want) clip = int(k);
            if (clip < 0) return luaL_error(L, "models.play: the model has no clip named '%s' (see models.clips)", want.c_str());
        } else if (!lua_isnoneornil(L, 2)) {
            return luaL_error(L, "models.play: clip must be a name, a number or nil");
        }
        const ModelData* d = models->model(models->instanceModel(inst));
        if (clip >= 0 && (!d || clip >= int(d->animations.size())))
            return luaL_error(L, "models.play: clip %d doesn't exist (the model has %d)", clip + 1, d ? int(d->animations.size()) : 0);
        models->playAnimation(inst, clip, lua_isnoneornil(L, 3) ? true : bool(lua_toboolean(L, 3)), float(luaL_optnumber(L, 4, 1.0)));
        return 0;
    });
    // models.clips(model) -> { "Idle", "Walk", ... }
    vm.registerFunction("models", "clips", [models](lua_State* L) {
        const ModelData* d = models->model(ModelModule::ModelId(checkId(L, 1, "models.clips")));
        if (!d) return luaL_error(L, "models.clips: %d is not a loaded model", int(lua_tointeger(L, 1)));
        lua_createtable(L, int(d->animations.size()), 0);
        for (size_t k = 0; k < d->animations.size(); ++k) {
            lua_pushstring(L, d->animations[k].name.c_str());
            lua_rawseti(L, -2, lua_Integer(k + 1));
        }
        return 1;
    });
    // models.bounds(model) -> min, max (model space)
    vm.registerFunction("models", "bounds", [models](lua_State* L) {
        const ModelData* d = models->model(ModelModule::ModelId(checkId(L, 1, "models.bounds")));
        if (!d) return luaL_error(L, "models.bounds: %d is not a loaded model", int(lua_tointeger(L, 1)));
        ScriptVM::pushVec3(L, d->boundsMin);
        ScriptVM::pushVec3(L, d->boundsMax);
        return 2;
    });
}

// ------------------------------------------------------------- breakable.*
void ScriptModule::bindBreakables() {
#if KKE_ENABLE_FEMFX
    auto* femfx = m_app->getModule<PhysicsModule>();
    if (!femfx) return;
    ScriptVM& vm = *m_vm;

    struct Preset { const char* name; Material m; FracturePattern pattern; float chunk; };
    auto presets = std::make_shared<std::vector<Preset>>();
    {
        // The showcase's breaking-yard materials (textureId: PhysicsModule's
        // library order, 0 wood, 1 stone, 2 iron, 3 rubber, 4 glass).
        Material glass; glass.density = 2500.0f; glass.stiffness = 7.0e7f; glass.poissonsRatio = 0.22f;
        glass.fractureStressThreshold = 1.0e5f; glass.roughness = 0.05f; glass.textureId = 4;
        Material stone; stone.density = 2500.0f; stone.stiffness = 3.0e7f; stone.poissonsRatio = 0.25f;
        stone.fractureStressThreshold = 1.0e5f; stone.roughness = 0.9f; stone.textureId = 1;
        Material wood; wood.density = 600.0f; wood.stiffness = 1.0e7f; wood.poissonsRatio = 0.3f;
        wood.fractureStressThreshold = 1.5e5f; wood.roughness = 0.75f; wood.textureId = 0;
        Material iron; iron.density = 7800.0f; iron.stiffness = 2.0e8f; iron.fractureStressThreshold = 1.0e12f;
        iron.metallic = 1.0f; iron.roughness = 0.35f; iron.textureId = 2;
        Material ice = glass; ice.density = 920.0f; ice.stiffness = 9.0e6f; ice.fractureStressThreshold = 4.0e4f; ice.roughness = 0.15f;
        *presets = {{"glass", glass, FracturePattern::Radial, 0.45f},
                    {"stone", stone, FracturePattern::Voronoi, 0.3f},
                    {"wood", wood, FracturePattern::Splinters, 0.35f},
                    {"ice", ice, FracturePattern::Shards, 0.2f},
                    {"iron", iron, FracturePattern::Solid, 1.0f}};
    }
    auto preset = [presets](lua_State* L, int table, const char* what) -> const Preset& {
        const std::string want = ScriptVM::fieldString(L, table, "material", "stone");
        for (const Preset& p : *presets)
            if (want == p.name) return p;
        luaL_error(L, "%s: unknown material '%s' (glass, stone, wood, ice, iron)", what, want.c_str());
        return presets->front();
    };
    auto budget = [this](lua_State* L, const char* what) {
        const std::string src = m_vm->currentSource();
        size_t mine = 0;
        for (const Breakable& b : m_breakables) mine += b.source == src;
        if (mine >= maxBreakablesPerScript)
            luaL_error(L, "%s: this script already has %d breakables (maxBreakablesPerScript)", what, int(maxBreakablesPerScript));
        return src;
    };

    // breakable.box{pos, size, material="stone", pattern, cells, chunk, arm=3} -> id
    vm.registerFunction("breakable", "box", [this, femfx, preset, budget](lua_State* L) {
        luaL_checktype(L, 1, LUA_TTABLE);
        const std::string src = budget(L, "breakable.box");
        const Preset& p = preset(L, 1, "breakable.box");
        const glm::vec3 size = glm::clamp(ScriptVM::fieldVec3(L, 1, "size", glm::vec3(1.0f)), glm::vec3(0.02f), glm::vec3(20.0f));
        FracturePattern pattern = p.pattern;
        const std::string pat = ScriptVM::fieldString(L, 1, "pattern", "");
        if (!pat.empty()) {
            const std::pair<const char*, FracturePattern> names[] = {{"shards", FracturePattern::Shards}, {"voronoi", FracturePattern::Voronoi},
                                                                     {"splinters", FracturePattern::Splinters}, {"radial", FracturePattern::Radial},
                                                                     {"solid", FracturePattern::Solid}};
            bool found = false;
            for (const auto& [n, f] : names)
                if (pat == n) { pattern = f; found = true; }
            if (!found) return luaL_error(L, "breakable.box: unknown pattern '%s' (shards, voronoi, splinters, radial, solid)", pat.c_str());
        }
        // Default cells: about one per 12 cm, capped (cost grows with tets).
        const glm::vec3 autoCells = glm::clamp(glm::ceil(size / 0.12f), glm::vec3(1.0f), glm::vec3(16.0f));
        const glm::ivec3 cells = glm::clamp(glm::ivec3(ScriptVM::fieldVec3(L, 1, "cells", autoCells)), glm::ivec3(1), glm::ivec3(24));
        const PhysicsModule::ObjectHandle h = femfx->spawnPatternedBox(
            cells, size, ScriptVM::fieldVec3(L, 1, "pos", glm::vec3(0, 1, 0)), p.m, int(pattern),
            std::max(0.05f, ScriptVM::fieldNumber(L, 1, "chunk", p.chunk)), 0, ScriptVM::fieldVec3(L, 1, "velocity", glm::vec3(0.0f)),
            std::max(0.0f, ScriptVM::fieldNumber(L, 1, "arm", 3.0f)));
        if (h == PhysicsModule::kInvalidHandle) {
            lua_pushnil(L);
            lua_pushstring(L, "the FEMFX scene is full");
            return 2;
        }
        m_breakables.push_back({h, src});
        lua_pushinteger(L, lua_Integer(h));
        return 1;
    });
    // breakable.ball{pos, radius, velocity, material="iron"} -> id (a thrown ball that can break things)
    vm.registerFunction("breakable", "ball", [this, femfx, preset, budget](lua_State* L) {
        luaL_checktype(L, 1, LUA_TTABLE);
        const std::string src = budget(L, "breakable.ball");
        lua_getfield(L, 1, "material");
        const bool hasMaterial = !lua_isnil(L, -1);
        lua_pop(L, 1);
        Material m;
        if (hasMaterial) m = preset(L, 1, "breakable.ball").m;
        else { m.density = 7800.0f; m.stiffness = 2.0e8f; m.fractureStressThreshold = 1.0e12f; m.metallic = 1.0f; m.roughness = 0.35f; m.textureId = 2; }
        const float r = std::clamp(ScriptVM::fieldNumber(L, 1, "radius", 0.15f), 0.03f, 2.0f);
        const PhysicsModule::ObjectHandle h = femfx->spawnFracturableTetMesh(PhysicsModule::buildSphere(3, r), ScriptVM::fieldVec3(L, 1, "pos", glm::vec3(0, 1, 0)), m,
                                                                            ScriptVM::fieldVec3(L, 1, "velocity", glm::vec3(0.0f)));
        if (h == PhysicsModule::kInvalidHandle) {
            lua_pushnil(L);
            lua_pushstring(L, "the FEMFX scene is full");
            return 2;
        }
        m_breakables.push_back({h, src});
        lua_pushinteger(L, lua_Integer(h));
        return 1;
    });
    auto owned = [this](lua_State* L, int idx, const char* what) -> Breakable& {
        const auto id = uint32_t(checkId(L, idx, what));
        for (Breakable& b : m_breakables)
            if (b.handle == id) return b;
        luaL_error(L, "%s: %d is not a breakable made by a script", what, int(id));
        return m_breakables.front();
    };
    vm.registerFunction("breakable", "remove", [this, femfx, owned](lua_State* L) {
        const uint32_t h = owned(L, 1, "breakable.remove").handle;
        femfx->removeObject(h);
        m_breakables.erase(std::remove_if(m_breakables.begin(), m_breakables.end(), [&](const Breakable& b) { return b.handle == h; }), m_breakables.end());
        return 0;
    });
    vm.registerFunction("breakable", "broken", [femfx, owned](lua_State* L) {
        lua_pushboolean(L, femfx->pieceCount(owned(L, 1, "breakable.broken").handle) > 1);
        return 1;
    });
    vm.registerFunction("breakable", "pieces", [femfx, owned](lua_State* L) {
        lua_pushinteger(L, lua_Integer(femfx->pieceCount(owned(L, 1, "breakable.pieces").handle)));
        return 1;
    });
    vm.registerFunction("breakable", "count", [this](lua_State* L) {
        lua_pushinteger(L, lua_Integer(m_breakables.size()));
        return 1;
    });
#endif
}

void ScriptModule::checkBreaks() {
#if KKE_ENABLE_FEMFX
    auto* femfx = m_app->getModule<PhysicsModule>();
    if (!femfx) return;
    for (size_t i = 0; i < m_breakables.size(); ++i) {
        Breakable& b = m_breakables[i];
        if (b.broken || femfx->pieceCount(b.handle) <= 1) continue;
        b.broken = true;
        const uint32_t h = b.handle; // the hook may remove it (and reshuffle the list)
        m_vm->callHook("Break", h);
    }
#endif
}

// -------------------------------------------------------------------- ui.*
void ScriptModule::bindUi() {
    auto* ui = m_app->getModule<UiModule>();
    if (!ui) return;
    ScriptVM& vm = *m_vm;
    m_clickListener = std::make_unique<ScriptClickListener>(*this);

    // `url`: where relative paths inside the document (images, style
    // sheets) are looked up.
    auto open = [this, ui](lua_State* L, const std::string& rml, const std::string& url, const std::string& what) {
        Rml::Context* ctx = ui->context();
        if (!ctx) return luaL_error(L, "%s: the UI isn't running", what.c_str());
        const std::string src = m_vm->currentSource();
        size_t mine = 0;
        for (const Document& d : m_documents) mine += d.source == src;
        if (mine >= maxDocumentsPerScript) return luaL_error(L, "%s: this script already has %d documents (maxDocumentsPerScript)", what.c_str(), int(maxDocumentsPerScript));
        Rml::ElementDocument* doc = ctx->LoadDocumentFromMemory(rml, url);
        if (!doc) return luaL_error(L, "%s: RmlUi couldn't load the document (see the log for why)", what.c_str());
        const int id = m_nextId++;
        doc->SetAttribute("data-kke-script-doc", id);
        doc->AddEventListener(Rml::EventId::Click, m_clickListener.get());
        doc->Show();
        m_documents.push_back({id, doc, src});
        lua_pushinteger(L, id);
        return 1;
    };
    // ui.open(rml) -> document id: RML text (a full <rml> document).
    vm.registerFunction("ui", "open", [this, open](lua_State* L) {
        return open(L, luaL_checkstring(L, 1), (std::filesystem::path(m_dir) / "script.rml").generic_string(), "ui.open");
    });
    // ui.load("hud.rml") -> document id: a file next to the scripts.
    vm.registerFunction("ui", "load", [this, open](lua_State* L) {
        const std::string name = luaL_checkstring(L, 1);
        if (!safeRelativePath(name)) return luaL_error(L, "ui.load: '%s' must be a relative path inside the scripts folder", name.c_str());
        const std::filesystem::path file = std::filesystem::path(m_dir) / name;
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            lua_pushnil(L);
            lua_pushfstring(L, "can't open '%s' in %s", name.c_str(), m_dir.c_str());
            return 2;
        }
        std::stringstream ss;
        ss << in.rdbuf();
        return open(L, ss.str(), file.generic_string(), "ui.load");
    });
    auto element = [this](lua_State* L, const char* what) -> Rml::Element* {
        const int id = checkId(L, 1, what);
        Document* d = document(id);
        if (!d || d->source != m_vm->currentSource()) luaL_error(L, "%s: %d is not a document this script opened", what, id);
        const char* elementId = luaL_checkstring(L, 2);
        Rml::Element* e = d->doc->GetElementById(elementId);
        if (!e) luaL_error(L, "%s: the document has no element with id '%s'", what, elementId);
        return e;
    };
    // ui.text(doc, id, text): plain text, shown as typed (no markup).
    vm.registerFunction("ui", "text", [element](lua_State* L) {
        Rml::Element* e = element(L, "ui.text");
        e->SetInnerRML(escapeRmlText(luaL_tolstring(L, 3, nullptr)));
        return 0;
    });
    // ui.rml(doc, id, rml): markup.
    vm.registerFunction("ui", "rml", [element](lua_State* L) {
        element(L, "ui.rml")->SetInnerRML(luaL_checkstring(L, 3));
        return 0;
    });
    // ui.class(doc, id, name, on)
    vm.registerFunction("ui", "class", [element](lua_State* L) {
        element(L, "ui.class")->SetClass(luaL_checkstring(L, 3), lua_toboolean(L, 4));
        return 0;
    });
    // ui.property(doc, id, name, value): an RCSS property ("width", "40%").
    vm.registerFunction("ui", "property", [element](lua_State* L) {
        const char* prop = luaL_checkstring(L, 3);
        const char* value = luaL_checkstring(L, 4);
        if (!element(L, "ui.property")->SetProperty(prop, value)) return luaL_error(L, "ui.property: '%s: %s' isn't a valid property", prop, value);
        return 0;
    });
    auto ownedDoc = [this](lua_State* L, const char* what) -> Document& {
        const int id = checkId(L, 1, what);
        Document* d = document(id);
        if (!d || d->source != m_vm->currentSource()) luaL_error(L, "%s: %d is not a document this script opened", what, id);
        return *d;
    };
    vm.registerFunction("ui", "show", [ownedDoc](lua_State* L) {
        Document& d = ownedDoc(L, "ui.show");
        if (lua_isnoneornil(L, 2) || lua_toboolean(L, 2)) d.doc->Show();
        else d.doc->Hide();
        return 0;
    });
    vm.registerFunction("ui", "close", [this, ownedDoc](lua_State* L) {
        const int id = ownedDoc(L, "ui.close").id;
        for (auto it = m_clickHandlers.begin(); it != m_clickHandlers.end();) {
            if (it->document == id) {
                m_vm->unref(it->ref);
                it = m_clickHandlers.erase(it);
            } else {
                ++it;
            }
        }
        Document* d = document(id);
        d->doc->RemoveEventListener(Rml::EventId::Click, m_clickListener.get());
        d->doc->Close();
        m_documents.erase(std::remove_if(m_documents.begin(), m_documents.end(), [&](const Document& x) { return x.id == id; }), m_documents.end());
        return 0;
    });
    // ui.onClick(doc, id, fn): fn() runs when the element (or anything in it) is clicked.
    vm.registerFunction("ui", "onClick", [this, element](lua_State* L) {
        element(L, "ui.onClick");
        luaL_checktype(L, 3, LUA_TFUNCTION);
        const int doc = int(lua_tointeger(L, 1));
        const std::string id = lua_tostring(L, 2);
        for (ClickHandler& h : m_clickHandlers)
            if (h.document == doc && h.element == id) {
                m_vm->unref(h.ref);
                h.ref = m_vm->ref(L, 3);
                return 0;
            }
        m_clickHandlers.push_back({doc, id, m_vm->ref(L, 3), m_vm->currentSource()});
        return 0;
    });
}

void ScriptModule::dispatchClicks() {
    if (m_pendingClicks.empty()) return;
    auto clicks = std::move(m_pendingClicks);
    m_pendingClicks.clear();
    for (const auto& [doc, element] : clicks) {
        int ref = 0;
        std::string source;
        for (const ClickHandler& h : m_clickHandlers)
            if (h.document == doc && h.element == element) { ref = h.ref; source = h.source; }
        if (ref) m_vm->callRef(ref, source);
    }
}

// ----------------------------------------------------------------- scene.*
void ScriptModule::bindScenes() {
#if KKE_ENABLE_JOLT
    auto* models = m_app->getModule<ModelModule>();
    if (!models) return;
    ScriptVM& vm = *m_vm;
    auto sceneDir = []() { return findAssetFolder("scenes", {"KKE_SCENES_DIR"}, baseDir()); };

    // scene.list() -> { "forest_trail", "town_block", ... }
    vm.registerFunction("scene", "list", [sceneDir](lua_State* L) {
        lua_newtable(L);
        const std::string dir = sceneDir();
        if (dir.empty()) return 1;
        std::vector<std::string> names;
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
            const std::string f = e.path().filename().string();
            const std::string ext = ".scene.json";
            if (f.size() > ext.size() && f.compare(f.size() - ext.size(), ext.size(), ext) == 0) names.push_back(f.substr(0, f.size() - ext.size()));
        }
        std::sort(names.begin(), names.end());
        for (size_t i = 0; i < names.size(); ++i) {
            lua_pushstring(L, names[i].c_str());
            lua_rawseti(L, -2, lua_Integer(i + 1));
        }
        return 1;
    });
    // scene.load(name, origin) -> scene id (+ how many assets were missing), or nil + reason
    vm.registerFunction("scene", "load", [this, models, sceneDir](lua_State* L) {
        const std::string name = luaL_checkstring(L, 1);
        if (!safeRelativePath(name) || name.find('/') != std::string::npos)
            return luaL_error(L, "scene.load: '%s' must be a scene name from scene.list()", name.c_str());
        const std::string src = m_vm->currentSource();
        size_t mine = 0;
        for (const Scene& s : m_scenes) mine += s.source == src;
        if (mine >= maxScenesPerScript) return luaL_error(L, "scene.load: this script already has %d scenes loaded (maxScenesPerScript)", int(maxScenesPerScript));
        const std::string dir = sceneDir();
        const std::filesystem::path file = std::filesystem::path(dir) / (name + ".scene.json");
        std::error_code ec;
        if (dir.empty() || !std::filesystem::is_regular_file(file, ec)) {
            lua_pushnil(L);
            lua_pushfstring(L, "no scene '%s' in the scenes folder", name.c_str());
            return 2;
        }
        SceneFile sf;
        try {
            sf = SceneFile::load(file.string());
        } catch (const std::exception& e) {
            lua_pushnil(L);
            lua_pushstring(L, e.what());
            return 2;
        }
        auto* rbm = m_app->getModule<RigidBodyModule>();
        const glm::vec3 origin = ScriptVM::toVec3(L, 2);
        Scene s{m_nextId++, loadScene(sf, catalog(), *models, rbm ? &rbm->world() : nullptr, origin), src, origin + sf.spawn, sf.spawnYaw};
        const size_t missing = s.loaded.missing.size();
        m_scenes.push_back(std::move(s));
        if (missing) log(src, "scene '" + name + "': " + std::to_string(missing) + " asset(s) not installed, left out");
        lua_pushinteger(L, m_scenes.back().id);
        lua_pushinteger(L, lua_Integer(missing));
        return 2;
    });
    auto owned = [this](lua_State* L, const char* what) -> Scene& {
        const int id = checkId(L, 1, what);
        for (Scene& s : m_scenes)
            if (s.id == id && s.source == m_vm->currentSource()) return s;
        luaL_error(L, "%s: %d is not a scene this script loaded", what, id);
        return m_scenes.front();
    };
    vm.registerFunction("scene", "unload", [this, models, owned](lua_State* L) {
        Scene& s = owned(L, "scene.unload");
        auto* rbm = m_app->getModule<RigidBodyModule>();
        unloadScene(s.loaded, *models, rbm ? &rbm->world() : nullptr);
        const int id = s.id;
        m_scenes.erase(std::remove_if(m_scenes.begin(), m_scenes.end(), [&](const Scene& x) { return x.id == id; }), m_scenes.end());
        return 0;
    });
    // scene.spawnPoint(id) -> position, yaw (degrees)
    vm.registerFunction("scene", "spawnPoint", [this, owned](lua_State* L) {
        const Scene& sc = owned(L, "scene.spawnPoint");
        ScriptVM::pushVec3(L, sc.spawn);
        lua_pushnumber(L, sc.spawnYaw);
        return 2;
    });
#endif
}

// ------------------------------------------------------------------- net.*
void ScriptModule::bindNet() {
#if KKE_ENABLE_NET
    auto* net = m_app->getModule<NetModule>();
    if (!net) return;
    ScriptVM& vm = *m_vm;
    net->addEventListener([this](const net::GameEventMsg& e) {
        if (e.kind != NetModule::kScriptEventKind) return;
        // name \0 value-bytes (ScriptVM::encodeValue); anything else is dropped.
        const auto zero = std::find(e.payload.begin(), e.payload.end(), uint8_t(0));
        if (zero == e.payload.end() || zero == e.payload.begin() || zero - e.payload.begin() > 64) {
            log::get(name())->warn("dropped a script net message from player {}: no name", e.fromPlayer);
            return;
        }
        if (m_netInbox.size() >= 256) {
            log::get(name())->warn("dropped a script net message from player {}: 256 already waiting", e.fromPlayer);
            return;
        }
        m_netInbox.push_back({std::string(e.payload.begin(), zero), std::string(zero + 1, e.payload.end()), int(e.fromPlayer)});
    });
    vm.registerFunction("net", "role", [net](lua_State* L) {
        lua_pushstring(L, net->role() == NetModule::Role::Host ? "host" : net->role() == NetModule::Role::Client ? "client" : "offline");
        return 1;
    });
    vm.registerFunction("net", "isServer", [net](lua_State* L) { lua_pushboolean(L, net->authority()); return 1; });
    vm.registerFunction("net", "connected", [net](lua_State* L) { lua_pushboolean(L, net->connected()); return 1; });
    vm.registerFunction("net", "playerId", [net](lua_State* L) { lua_pushinteger(L, net->localPlayerId()); return 1; });
    // net.players() -> { { id = 1, name = "Kees" }, ... } (the others)
    vm.registerFunction("net", "players", [net](lua_State* L) {
        const auto& players = net->remotePlayers();
        lua_createtable(L, int(players.size()), 0);
        for (size_t i = 0; i < players.size(); ++i) {
            lua_createtable(L, 0, 2);
            lua_pushinteger(L, players[i].id);
            lua_setfield(L, -2, "id");
            lua_pushstring(L, players[i].name.c_str());
            lua_setfield(L, -2, "name");
            lua_rawseti(L, -2, lua_Integer(i + 1));
        }
        return 1;
    });
    // net.send(name, data): from a client to the host; from the host to
    // every client. The receiver's scripts get hook "NetMessage"(name, data, from).
    vm.registerFunction("net", "send", [net](lua_State* L) {
        size_t len = 0;
        const char* name = luaL_checklstring(L, 1, &len);
        if (len == 0 || len > 64 || std::memchr(name, 0, len)) return luaL_error(L, "net.send: the name must be 1-64 characters");
        std::string bytes, err;
        if (!ScriptVM::encodeValue(L, 2, bytes, err, 1024)) return luaL_error(L, "net.send: %s", err.c_str());
        if (!net->connected() && net->role() != NetModule::Role::Host) {
            lua_pushboolean(L, 0);
            return 1;
        }
        std::vector<uint8_t> payload(name, name + len);
        payload.push_back(0);
        payload.insert(payload.end(), bytes.begin(), bytes.end());
        net->sendEvent(NetModule::kScriptEventKind, payload);
        lua_pushboolean(L, 1);
        return 1;
    });
#endif
}

void ScriptModule::dispatchNet() {
    if (m_netInbox.empty()) return;
    auto inbox = std::move(m_netInbox);
    m_netInbox.clear();
    for (const NetMessage& m : inbox) {
        m_vm->callHookWith("NetMessage", [&](lua_State* L) {
            lua_pushlstring(L, m.name.data(), m.name.size());
            std::string err;
            if (!ScriptVM::decodeValue(L, m.data, err)) {
                log::get(name())->warn("script net message '{}' from player {} is damaged ({}): data is nil", m.name, m.from, err);
                lua_pushnil(L);
            }
            lua_pushinteger(L, m.from);
            return 3;
        });
    }
}

// ---------------------------------------------------------------- cleanup
void ScriptModule::releaseScript(const std::string& source) {
    auto mine = [&](const std::string& s) { return s == source; };
#if KKE_ENABLE_JOLT
    auto* rb = m_app->getModule<RigidBodyModule>();
    for (const Body& b : m_bodies)
        if (mine(b.source) && rb) rb->world().remove(b.id);
#endif
    m_bodies.erase(std::remove_if(m_bodies.begin(), m_bodies.end(), [&](const Body& b) { return mine(b.source); }), m_bodies.end());

    if (auto* models = m_app->getModule<ModelModule>())
        for (const ModelInstance& m : m_models)
            if (mine(m.source)) models->remove(m.id);
    m_models.erase(std::remove_if(m_models.begin(), m_models.end(), [&](const ModelInstance& m) { return mine(m.source); }), m_models.end());

#if KKE_ENABLE_FEMFX
    if (auto* femfx = m_app->getModule<PhysicsModule>())
        for (const Breakable& b : m_breakables)
            if (mine(b.source)) femfx->removeObject(b.handle);
#endif
    m_breakables.erase(std::remove_if(m_breakables.begin(), m_breakables.end(), [&](const Breakable& b) { return mine(b.source); }), m_breakables.end());

    for (const ClickHandler& h : m_clickHandlers)
        if (mine(h.source)) m_vm->unref(h.ref);
    m_clickHandlers.erase(std::remove_if(m_clickHandlers.begin(), m_clickHandlers.end(), [&](const ClickHandler& h) { return mine(h.source); }), m_clickHandlers.end());
    for (const Document& d : m_documents)
        if (mine(d.source)) {
            d.doc->RemoveEventListener(Rml::EventId::Click, m_clickListener.get());
            d.doc->Close();
        }
    m_documents.erase(std::remove_if(m_documents.begin(), m_documents.end(), [&](const Document& d) { return mine(d.source); }), m_documents.end());

#if KKE_ENABLE_JOLT
    if (auto* models = m_app->getModule<ModelModule>())
        for (Scene& s : m_scenes)
            if (mine(s.source)) unloadScene(s.loaded, *models, rb ? &rb->world() : nullptr);
#endif
    m_scenes.erase(std::remove_if(m_scenes.begin(), m_scenes.end(), [&](const Scene& s) { return mine(s.source); }), m_scenes.end());
}

void ScriptModule::releaseAll() {
    std::vector<std::string> sources;
    auto add = [&](const std::string& s) {
        if (std::find(sources.begin(), sources.end(), s) == sources.end()) sources.push_back(s);
    };
    for (const Body& b : m_bodies) add(b.source);
    for (const ModelInstance& m : m_models) add(m.source);
    for (const Breakable& b : m_breakables) add(b.source);
    for (const Document& d : m_documents) add(d.source);
    for (const ClickHandler& h : m_clickHandlers) add(h.source);
    for (const Scene& s : m_scenes) add(s.source);
    for (const std::string& s : sources) releaseScript(s);
}

} // namespace kke
