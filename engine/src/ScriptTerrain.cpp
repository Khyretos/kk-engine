#include "kke/ScriptTerrain.h"

#include "kke/Application.h"
#include "kke/Module.h"
#include "kke/Picking.h"
#include "kke/ScriptVM.h"
#include "kke/SphereImpostors.h"
#if KKE_ENABLE_JOLT
#include "kke/RigidWorld.h"
#endif

#include <lauxlib.h>
#include <lua.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <system_error>

namespace kke {

namespace {

constexpr uint32_t kNoBody = 0xffffffffu;

// A name a script may give: relative, inside the folders we search.
bool safeRelative(const std::string& path) {
    if (path.empty() || path.size() > 512) return false;
    const std::filesystem::path p(path);
    if (p.is_absolute() || p.has_root_name() || p.has_root_directory()) return false;
    for (const auto& part : p)
        if (part == "..") return false;
    return path.find('\\') == std::string::npos && path.find(':') == std::string::npos;
}

// A layer by number (1-4) or by name.
int layerArg(lua_State* L, int idx, const Terrain& t, const char* what) {
    if (lua_type(L, idx) == LUA_TNUMBER) {
        const lua_Integer i = lua_tointeger(L, idx);
        if (i < 1 || i > Terrain::kLayers) luaL_error(L, "%s: layer %d: there are layers 1 to %d", what, int(i), Terrain::kLayers);
        return int(i) - 1;
    }
    const std::string name = luaL_checkstring(L, idx);
    for (int i = 0; i < Terrain::kLayers; ++i)
        if (t.layers[size_t(i)].name == name) return i;
    luaL_error(L, "%s: no layer called '%s'", what, name.c_str());
    return 0;
}

} // namespace

ScriptTerrain::ScriptTerrain(Application* app, RigidWorld* world) : m_app(app), m_world(world) {}

ScriptTerrain::~ScriptTerrain() { clear(); }

void ScriptTerrain::clear() {
#if KKE_ENABLE_JOLT
    if (m_world && m_body != kNoBody) m_world->remove(m_body);
#endif
    m_body = kNoBody;
    m_terrain.reset();
    m_chunks.reset();
    m_meshes.clear();
    m_meshIndices.clear();
    m_bounds.clear();
    m_owner.clear();
    m_collisionDirty = false;
}

void ScriptTerrain::release(const std::string& source) {
    if (m_terrain && m_owner == source) clear();
}

void ScriptTerrain::touched(const Terrain::Rect& r) {
    if (r.empty() || !m_chunks) return;
    m_chunks->touch(r);
    m_collisionDirty = true;
    m_sinceEdit = 0.0f;
}

void ScriptTerrain::rebuildCollision() {
    m_collisionDirty = false;
    m_dirtyFor = 0.0f;
#if KKE_ENABLE_JOLT
    if (!m_world || !m_terrain) return;
    if (m_body != kNoBody) m_world->remove(m_body);
    RigidWorld::BodyDesc d;
    d.shape = RigidWorld::Shape::HeightField;
    d.motion = RigidWorld::Motion::Static;
    d.heights = m_terrain->heights();
    d.heightSamples = uint32_t(m_terrain->samples());
    d.heightSpacing = m_terrain->spacing();
    d.position = m_terrain->corner();
    d.friction = 0.8f;
    d.restitution = 0.05f;
    m_body = m_world->add(d);
#endif
}

void ScriptTerrain::update(float dt, const glm::vec3& camera) {
    if (!m_terrain) return;
    if (m_collisionDirty) {
        m_sinceEdit += dt;
        m_dirtyFor += dt;
        if (m_sinceEdit >= collisionDelay || m_dirtyFor >= collisionDelay * 4.0f) rebuildCollision();
    }
    if (!m_app) return;
    static std::vector<Vertex> v;
    static std::vector<uint32_t> idx;
    const float skirt = std::max(0.5f, m_terrain->spacing() * 4.0f);
    // The first frame builds every chunk, so a new terrain never shows holes.
    const bool fresh = m_chunksBuilt == m_freshAt;
    for (size_t i : m_chunks->update(camera, fresh ? m_chunks->chunks().size() : chunkBuildsPerFrame)) {
        TerrainChunks::Chunk& c = m_chunks->chunks()[i];
        const int step = m_chunks->stepFor(c, camera);
        v.clear();
        idx.clear();
        m_terrain->buildMesh(c.x0, c.z0, m_chunks->cells(), m_chunks->cells(), step, skirt, v, idx);
        if (!m_meshes[i]) m_meshes[i] = std::make_unique<DynamicMeshRenderer>(*m_app);
        if (!idx.empty()) m_meshes[i]->upload(v, idx);
        m_meshIndices[i] = idx.size();
        // Its bounds, for culling (vertices are in world space; skirts included).
        glm::vec3 lo(1e30f), hi(-1e30f);
        for (const Vertex& vert : v) {
            lo = glm::min(lo, vert.position);
            hi = glm::max(hi, vert.position);
        }
        m_bounds[i] = {lo, hi};
        m_chunks->built(i, step);
        ++m_chunksBuilt;
    }
}

void ScriptTerrain::render(const RenderContext& ctx) {
    // Only the chunks the camera can see.
    const Frustum f = Frustum::fromViewProj(ctx.proj * ctx.view);
    m_drawnLastFrame = 0;
    for (size_t i = 0; i < m_meshes.size(); ++i)
        if (m_meshes[i] && m_meshIndices[i] && f.intersectsAabb(m_bounds[i].first, m_bounds[i].second)) {
            m_meshes[i]->draw(ctx, glm::mat4(1.0f), 0.0f, 0.9f);
            ++m_drawnLastFrame;
        }
}

void ScriptTerrain::renderShadow(const ShadowRenderContext& ctx) {
    const Frustum f = Frustum::fromViewProj(ctx.lightViewProj);
    for (size_t i = 0; i < m_meshes.size(); ++i)
        if (m_meshes[i] && m_meshIndices[i] && f.intersectsAabb(m_bounds[i].first, m_bounds[i].second)) m_meshes[i]->drawShadow(ctx, glm::mat4(1.0f));
}

void ScriptTerrain::bind(ScriptVM& vm) {
    // The terrain, or a clear error (made by another script is fine to read).
    auto need = [this](lua_State* L, const char* what) -> Terrain& {
        if (!m_terrain) luaL_error(L, "%s: there is no terrain (make one with terrain.create)", what);
        return *m_terrain;
    };
    auto mine = [this, &vm, need](lua_State* L, const char* what) -> Terrain& {
        Terrain& t = need(L, what);
        if (m_owner != vm.currentSource()) luaL_error(L, "%s: the terrain belongs to %s", what, m_owner.c_str());
        return t;
    };
    auto xz = [](lua_State* L, int idx) -> glm::vec2 {
        if (lua_type(L, idx) == LUA_TNUMBER) return {float(luaL_checknumber(L, idx)), float(luaL_checknumber(L, idx + 1))};
        const glm::vec3 p = ScriptVM::toVec3(L, idx);
        return {p.x, p.z};
    };

    // terrain.create{size, resolution, pos, height, seed, feature, octaves,
    // roughness, ridged, image, flat, rock, rockSlope, layers} -> true, or nil + why
    vm.registerFunction("terrain", "create", [this, &vm](lua_State* L) {
        luaL_checktype(L, 1, LUA_TTABLE);
        const std::string src = vm.currentSource();
        if (m_terrain && m_owner != src) return luaL_error(L, "terrain.create: %s already made the terrain", m_owner.c_str());
        const float size = std::clamp(ScriptVM::fieldNumber(L, 1, "size", 256.0f), 4.0f, 16384.0f);
        const int samples = std::clamp(int(ScriptVM::fieldNumber(L, 1, "resolution", 257.0f)), Terrain::kMinSamples, std::min(maxSamples, Terrain::kMaxSamples));
        auto t = std::make_unique<Terrain>(size, samples, ScriptVM::fieldVec3(L, 1, "pos", glm::vec3(0.0f)));
        const float height = std::max(0.0f, ScriptVM::fieldNumber(L, 1, "height", 20.0f));
        const std::string image = ScriptVM::fieldString(L, 1, "image", "");
        if (!image.empty()) {
            if (!safeRelative(image)) return luaL_error(L, "terrain.create: image '%s' must be a relative path inside the game's folder", image.c_str());
            std::filesystem::path found;
            std::error_code ec;
            for (const std::filesystem::path& dir : searchDirs)
                if (std::filesystem::is_regular_file(dir / image, ec)) {
                    found = dir / image;
                    break;
                }
            std::string err = "'" + image + "' not found";
            if (found.empty() || !t->loadImage(found, height, &err)) {
                lua_pushnil(L);
                lua_pushstring(L, err.c_str());
                return 2;
            }
        } else if (!ScriptVM::fieldBool(L, 1, "flat", false)) {
            Terrain::Noise n;
            n.seed = uint32_t(ScriptVM::fieldNumber(L, 1, "seed", 1.0f));
            n.height = height;
            n.feature = ScriptVM::fieldNumber(L, 1, "feature", 0.0f);
            n.octaves = int(ScriptVM::fieldNumber(L, 1, "octaves", 5.0f));
            n.roughness = ScriptVM::fieldNumber(L, 1, "roughness", 0.5f);
            n.ridged = ScriptVM::fieldBool(L, 1, "ridged", false);
            t->generate(n);
        }
        t->autoRock = ScriptVM::fieldBool(L, 1, "rock", true);
        t->rockSlope = std::clamp(ScriptVM::fieldNumber(L, 1, "rockSlope", 38.0f), 5.0f, 89.0f);
        lua_getfield(L, 1, "layers");
        if (lua_istable(L, -1)) {
            for (int i = 0; i < Terrain::kLayers; ++i) {
                lua_rawgeti(L, -1, i + 1);
                if (lua_istable(L, -1)) {
                    const int at = lua_gettop(L);
                    t->layers[size_t(i)].name = ScriptVM::fieldString(L, at, "name", t->layers[size_t(i)].name);
                    t->layers[size_t(i)].color = ScriptVM::fieldVec3(L, at, "color", t->layers[size_t(i)].color);
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
        clear();
        m_terrain = std::move(t);
        m_owner = src;
        // Chunks of 32 cells, bigger on big terrains so there are never more than 16 x 16 to draw.
        const int cells = std::max(32, (m_terrain->samples() - 1 + 15) / 16);
        m_chunks = std::make_unique<TerrainChunks>(*m_terrain, cells, std::max(30.0f, m_terrain->spacing() * 48.0f));
        m_meshes.resize(m_chunks->chunks().size());
        m_meshIndices.assign(m_chunks->chunks().size(), 0);
        m_bounds.assign(m_chunks->chunks().size(), {glm::vec3(0.0f), glm::vec3(0.0f)});
        m_freshAt = m_chunksBuilt;
        rebuildCollision(); // at once: things placed next line land on it
        lua_pushboolean(L, 1);
        return 1;
    });
    vm.registerFunction("terrain", "exists", [this](lua_State* L) {
        lua_pushboolean(L, m_terrain != nullptr);
        return 1;
    });
    // terrain.info() -> {size, resolution, spacing, corner, centre, owner}
    vm.registerFunction("terrain", "info", [this, need](lua_State* L) {
        const Terrain& t = need(L, "terrain.info");
        lua_newtable(L);
        lua_pushnumber(L, t.size());
        lua_setfield(L, -2, "size");
        lua_pushinteger(L, t.samples());
        lua_setfield(L, -2, "resolution");
        lua_pushnumber(L, t.spacing());
        lua_setfield(L, -2, "spacing");
        ScriptVM::pushVec3(L, t.corner());
        lua_setfield(L, -2, "corner");
        ScriptVM::pushVec3(L, t.centre());
        lua_setfield(L, -2, "centre");
        lua_pushstring(L, m_owner.c_str());
        lua_setfield(L, -2, "owner");
        return 1;
    });
    // terrain.height(x, z) or (pos) -> y, on (false past the edge: the edge's height)
    vm.registerFunction("terrain", "height", [need, xz](lua_State* L) {
        const Terrain& t = need(L, "terrain.height");
        const glm::vec2 p = xz(L, 1);
        lua_pushnumber(L, t.heightAt(p.x, p.y));
        lua_pushboolean(L, t.contains(p.x, p.y));
        return 2;
    });
    vm.registerFunction("terrain", "normal", [need, xz](lua_State* L) {
        const glm::vec2 p = xz(L, 1);
        ScriptVM::pushVec3(L, need(L, "terrain.normal").normalAt(p.x, p.y));
        return 1;
    });
    vm.registerFunction("terrain", "slope", [need, xz](lua_State* L) {
        const glm::vec2 p = xz(L, 1);
        lua_pushnumber(L, need(L, "terrain.slope").slopeAt(p.x, p.y));
        return 1;
    });
    // terrain.ground(pos) -> pos with y on the ground (for placing things)
    vm.registerFunction("terrain", "ground", [need](lua_State* L) {
        glm::vec3 p = ScriptVM::toVec3(L, 1);
        p.y = need(L, "terrain.ground").heightAt(p.x, p.z);
        ScriptVM::pushVec3(L, p);
        return 1;
    });
    auto radiusArg = [](lua_State* L, int idx, const char* what) {
        const float r = float(luaL_checknumber(L, idx));
        if (!(r > 0.0f) || r > 10000.0f) luaL_error(L, "%s: the radius must be above 0 (metres)", what);
        return r;
    };
    // terrain.raise(pos, radius, metres) / lower(pos, radius, metres)
    vm.registerFunction("terrain", "raise", [this, mine, radiusArg](lua_State* L) {
        Terrain& t = mine(L, "terrain.raise");
        touched(t.raise(ScriptVM::toVec3(L, 1), radiusArg(L, 2, "terrain.raise"), float(luaL_checknumber(L, 3))));
        return 0;
    });
    vm.registerFunction("terrain", "lower", [this, mine, radiusArg](lua_State* L) {
        Terrain& t = mine(L, "terrain.lower");
        touched(t.raise(ScriptVM::toVec3(L, 1), radiusArg(L, 2, "terrain.lower"), -float(luaL_checknumber(L, 3))));
        return 0;
    });
    // terrain.smooth(pos, radius [, strength 0..1])
    vm.registerFunction("terrain", "smooth", [this, mine, radiusArg](lua_State* L) {
        Terrain& t = mine(L, "terrain.smooth");
        touched(t.smooth(ScriptVM::toVec3(L, 1), radiusArg(L, 2, "terrain.smooth"), float(luaL_optnumber(L, 3, 0.5))));
        return 0;
    });
    // terrain.flatten(pos, radius [, height (pos.y), strength 0..1])
    vm.registerFunction("terrain", "flatten", [this, mine, radiusArg](lua_State* L) {
        Terrain& t = mine(L, "terrain.flatten");
        const glm::vec3 at = ScriptVM::toVec3(L, 1);
        touched(t.flatten(at, radiusArg(L, 2, "terrain.flatten"), float(luaL_optnumber(L, 3, at.y)), float(luaL_optnumber(L, 4, 1.0))));
        return 0;
    });
    // terrain.paint(pos, radius, layer [, strength 0..1])
    vm.registerFunction("terrain", "paint", [this, mine, radiusArg](lua_State* L) {
        Terrain& t = mine(L, "terrain.paint");
        const int layer = layerArg(L, 3, t, "terrain.paint");
        const Terrain::Rect r = t.paint(ScriptVM::toVec3(L, 1), radiusArg(L, 2, "terrain.paint"), layer, float(luaL_optnumber(L, 4, 1.0)));
        if (m_chunks) m_chunks->touch(r); // colour only: the collision stays
        return 0;
    });
    // terrain.layer(layer, {name, color}): rename or recolour one
    vm.registerFunction("terrain", "layer", [this, mine](lua_State* L) {
        Terrain& t = mine(L, "terrain.layer");
        const int layer = layerArg(L, 1, t, "terrain.layer");
        luaL_checktype(L, 2, LUA_TTABLE);
        t.layers[size_t(layer)].name = ScriptVM::fieldString(L, 2, "name", t.layers[size_t(layer)].name);
        t.layers[size_t(layer)].color = ScriptVM::fieldVec3(L, 2, "color", t.layers[size_t(layer)].color);
        if (m_chunks) m_chunks->touchAll();
        return 0;
    });
    vm.registerFunction("terrain", "remove", [this, mine](lua_State* L) {
        mine(L, "terrain.remove");
        clear();
        return 0;
    });
}

} // namespace kke
