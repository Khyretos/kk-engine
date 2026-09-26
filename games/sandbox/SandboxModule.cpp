#include "SandboxModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/FracturePattern.h"
#include "kke/InteriorColor.h"
#include "kke/VoronoiFracture.h"
#include "kke/Material.h"
#include "kke/SceneLoader.h"
#if KKE_ENABLE_FEMFX
#include "kke/modules/PhysicsModule.h"
#endif

#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <typeindex>

namespace kke_sandbox {

namespace {

// What "Make breakable" turns a prop into: a FEMFX material plus how it
// breaks (kke/FracturePattern.h) — the material decides the pattern, so
// wood splinters, stone crumbles into chunks, glass shatters radially and
// metal bends instead of breaking. Material values are the measured
// presets from MaterialGridModule (see its comment and BUGS.md BUG-020
// for how the fracture thresholds were found); textureId picks
// PhysicsModule's matching texture, shown on fresh crack faces only.
struct BreakMaterial { const char* name; kke::Material material; kke::FracturePattern pattern; float chunkSize; int cellsPerCluster; bool plastic; };
// Fracture thresholds are FEMFX's per-tet stress (Pa) at piece borders,
// on top of each border's own resting stress (settle, then arm). Chosen
// with tools/physics_lab "shoot" (a 0.8 m crate, the sandbox's ball):
// nothing breaks at 12 m/s, the default 18 m/s ball breaks all four
// clearly, and Toughness x1.25 turns stone into chipping (~7 of 19
// pieces at 18 m/s). The response is steep: FEMFX's stress jumps ~4x
// between a 12 and an 18 m/s hit. The old 2k-8k values sat
// inside FEMFX's stress noise (~400 in free fall, spikes to 60k).
const BreakMaterial kBreakMaterials[] = {
    { "Wood (splinters)", []{ kke::Material m; m.density=600.0f;  m.stiffness=1.0e7f; m.poissonsRatio=0.30f; m.fractureStressThreshold=1.2e5f; m.plasticYieldThreshold=6000.0f; m.plasticCreep=0.3f; m.metallic=0.0f; m.roughness=0.75f; m.textureId=0; return m; }(), kke::FracturePattern::Splinters, 0.35f, 0, false },
    { "Stone (chunks)",   []{ kke::Material m; m.density=2500.0f; m.stiffness=3.0e7f; m.poissonsRatio=0.25f; m.fractureStressThreshold=8.0e4f; m.plasticYieldThreshold=3000.0f; m.plasticCreep=0.1f; m.metallic=0.0f; m.roughness=0.9f; m.textureId=1; return m; }(), kke::FracturePattern::Voronoi, 0.3f, 3, false },
    { "Glass (shatters)", []{ kke::Material m; m.density=2500.0f; m.stiffness=7.0e7f; m.poissonsRatio=0.22f; m.fractureStressThreshold=8.0e4f; m.plasticYieldThreshold=1800.0f; m.plasticCreep=0.02f; m.metallic=0.0f; m.roughness=0.05f; m.textureId=4; return m; }(), kke::FracturePattern::Radial, 0.45f, 0, false },
    { "Ceramic (shards)", []{ kke::Material m; m.density=2300.0f; m.stiffness=5.0e7f; m.poissonsRatio=0.22f; m.fractureStressThreshold=8.0e4f; m.plasticYieldThreshold=2400.0f; m.plasticCreep=0.02f; m.metallic=0.0f; m.roughness=0.3f; m.textureId=1; return m; }(), kke::FracturePattern::Shards, 0.3f, 0, false },
    // Metal dents instead of breaking: FEMFX plasticity, no fracture.
    { "Metal (dents)",    []{ kke::Material m; m.density=7870.0f; m.stiffness=2.0e7f; m.poissonsRatio=0.30f; m.fractureStressThreshold=1.0e9f; m.plasticYieldThreshold=2000.0f; m.plasticCreep=0.5f; m.metallic=0.9f; m.roughness=0.35f; m.textureId=2; return m; }(), kke::FracturePattern::Solid, 1.0f, 0, true },
};
constexpr int kBreakMaterialCount = static_cast<int>(sizeof(kBreakMaterials) / sizeof(kBreakMaterials[0]));

// Budgets (OPTIMIZATION.md rule 5: everything that can pile up gets a cap
// and a policy). Past the cap the oldest ball is removed — a thrown ball
// that's been lying around is the least interesting object in the scene.
constexpr size_t kMaxBalls = 6;

const glm::vec3 kSelectColor(1.0f, 0.75f, 0.1f);
const glm::vec3 kHoverColor(0.55f, 0.8f, 1.0f);
const glm::vec3 kGhostColor(0.55f, 1.0f, 0.55f); // placement outline
const glm::vec3 kSpawnColor(0.3f, 0.9f, 1.0f);
const glm::vec3 kLightColor(1.0f, 0.85f, 0.4f);

// Break materials by their kke.scene name ("breakable": "wood", ...), in
// kBreakMaterials order.
const char* const kBreakSceneNames[] = { "wood", "stone", "glass", "ceramic", "metal" };
static_assert(sizeof(kBreakSceneNames) / sizeof(kBreakSceneNames[0]) == sizeof(kBreakMaterials) / sizeof(kBreakMaterials[0]));

int breakMaterialFromName(const std::string& n) {
    for (int i = 0; i < kBreakMaterialCount; ++i)
        if (n == kBreakSceneNames[i] || n == kBreakMaterials[i].name) return i; // scene name, or an old layout's label
    return -1;
}

// Closest points between a ray and the line p + s*axis: s on the line.
float rayLineParam(const kke::Ray& ray, const glm::vec3& p, const glm::vec3& axis, float* distance = nullptr) {
    const glm::vec3 w = ray.origin - p;
    const float b = glm::dot(ray.direction, axis), d = glm::dot(ray.direction, w), e = glm::dot(axis, w);
    const float denom = 1.0f - b * b; // both unit length
    float s = 0.0f, t = 0.0f;
    if (denom > 1e-6f) {
        t = (b * e - d) / denom;
        s = (e - b * d) / denom;
    } else {
        s = e; // parallel: any point; the one in front of the origin
    }
    if (t < 0.0f) { t = 0.0f; s = e; }
    if (distance) *distance = glm::length((ray.origin + ray.direction * t) - (p + axis * s));
    return s;
}

} // namespace

std::vector<kke::ModuleDependency> SandboxModule::dependencies() const {
    return {
        { std::type_index(typeid(kke::ModelModule)), true, "loads and draws the placed assets" },
        { std::type_index(typeid(kke::DebugDrawModule)), true, "selection boxes, grid, placement preview" },
    };
}

void SandboxModule::init(kke::Application& app) {
    m_app = &app;
    m_models = app.getModule<kke::ModelModule>();
    m_debug = app.getModule<kke::DebugDrawModule>();
    auto providers = app.findCapability<kke::IRagdollPhysics>();
    m_ragdolls = providers.empty() ? nullptr : providers.front();
#if KKE_ENABLE_FEMFX
    m_hasFemfx = app.getModule<kke::PhysicsModule>() != nullptr;
#endif
    const char* base = SDL_GetBasePath();
    std::string folder = kke::findAssetFolder("assets/synty", { "KKE_ASSETS_DIR", "KKE_SYNTY_DIR" }, base ? base : "", &m_searched);
    if (!folder.empty()) openAssetFolder(folder);
    else kke::log::get(name())->info("no asset folder found - the Assets panel lists where it looked and takes a path");
    // Save next to the scenes kke_demo loads (its Scenes panel lists every
    // scenes/*.scene.json), so a level built here is playable there.
    const std::string scenes = kke::findAssetFolder("scenes", { "KKE_SCENES_DIR" }, base ? base : "");
    if (!scenes.empty())
        std::snprintf(m_layoutPath, sizeof(m_layoutPath), "%s", (std::filesystem::path(scenes) / "sandbox.scene.json").string().c_str());
    // Optional: KKE_SANDBOX_LAYOUT=file.json loads a layout at startup
    // (used by the automated screenshot tests, handy for sharing scenes).
    if (const char* layout = std::getenv("KKE_SANDBOX_LAYOUT")) {
        std::snprintf(m_layoutPath, sizeof(m_layoutPath), "%s", layout);
        loadLayout(layout);
    }
    // KKE_SANDBOX_SAVE=file.scene.json saves the level right after startup
    // (with KKE_SANDBOX_LAYOUT: converts an old layout, and the automated
    // round-trip check in tests/sandbox_roundtrip.sh).
    if (const char* save = std::getenv("KKE_SANDBOX_SAVE")) saveLayout(save);
}

void SandboxModule::setEnginePanels(std::vector<kke::Module*> panels) {
    m_enginePanels = std::move(panels);
    for (kke::Module* m : m_enginePanels) m->setUiVisible(m_showEnginePanels);
}

void SandboxModule::openAssetFolder(const std::string& folder) {
    clearAll();
    m_catalog = kke::AssetCatalog::scan(folder);
    m_assetFolder = m_catalog.assets.empty() ? std::string() : folder;
    m_filterPack.clear();
    m_filterCategory.clear();
    m_filterDirty = true;
    if (m_assetFolder.empty()) {
        m_status = "No model files (.fbx/.obj/.gltf) under '" + folder + "'";
        kke::log::get(name())->warn("{}", m_status);
        return;
    }
    std::snprintf(m_folderInput, sizeof(m_folderInput), "%s", folder.c_str());
    m_variant = 0;
    m_overlay = 1; // the pack's first grid, if it has one: Synty's Prototype look
    applyLook();
    m_status = std::to_string(m_catalog.assets.size()) + " assets in " + std::to_string(m_catalog.packs.size()) + " pack(s)";
    kke::log::get(name())->info("asset folder '{}': {}", folder, m_status);
}

// ---------------------------------------------------------------- objects

const kke::CatalogAsset* SandboxModule::resolve(const std::string& name, const std::string& pack) const {
    if (!pack.empty()) {
        std::vector<std::string> prefer{ pack };
        prefer.insert(prefer.end(), m_scenePacks.begin(), m_scenePacks.end());
        return m_catalog.find(name, prefer);
    }
    return m_catalog.find(name, m_scenePacks);
}

const kke::CatalogPack* SandboxModule::packOf(const std::string& asset, const std::string& pack) const {
    const kke::CatalogAsset* a = resolve(asset, pack);
    return a ? m_catalog.pack(a->pack) : nullptr;
}

// The overlay comes from the first pack that has grids (packs rarely mix).
void SandboxModule::applyLook() {
    std::string overlay;
    for (const kke::CatalogPack& p : m_catalog.packs) {
        if (m_overlay > 0 && m_overlay <= static_cast<int>(p.overlayTextures.size())) { overlay = p.overlayTextures[m_overlay - 1]; break; }
    }
    m_models->setWorldOverlay(overlay, m_overlayTile, m_overlayStrength);
}

kke::ModelModule::ModelId SandboxModule::loadAsset(const std::string& assetName, const std::string& pack) {
    const kke::CatalogAsset* asset = resolve(assetName, pack);
    if (!asset) return 0;
    // The same options kke::loadScene uses, so a level looks the same here
    // and in the games that load it; plus animations (characters idle).
    kke::ModelLoadOptions opts = kke::packLoadOptions(m_catalog, *asset);
    opts.loadAnimations = true;
    // Synchronous: a Synty FBX loads in a few ms and ModelModule caches
    // it. Streaming/time-sliced loading is in OPTIMIZATION.md's backlog.
    return m_models->load(asset->path, opts);
}

// Synty pivots differ per piece (corner for walls, center for props), so
// objects are positioned by their bounds: `position` is the bottom-center.
// Same maths as kke::SceneFile::placement, so saved scenes load where
// they were placed.
glm::mat4 SandboxModule::objectTransform(const kke::ModelData& d, const glm::vec3& position, float yawDegrees, float scale) const {
    glm::vec3 center = (d.boundsMin + d.boundsMax) * 0.5f;
    glm::mat4 t = glm::translate(glm::mat4(1.0f), position);
    t = glm::rotate(t, glm::radians(yawDegrees), glm::vec3(0, 1, 0));
    t = glm::scale(t, glm::vec3(scale));
    return glm::translate(t, glm::vec3(-center.x, -d.boundsMin.y, -center.z));
}

glm::mat4 SandboxModule::objectTransform(const Object& o) const {
    const kke::ModelData* d = m_models->model(o.model);
    return d ? objectTransform(*d, o.position, o.yawDegrees, o.scale) : glm::translate(glm::mat4(1.0f), o.position);
}

void SandboxModule::applyTransform(Object& o) {
    m_models->setTransform(o.instance, objectTransform(o));
}

SandboxModule::Object* SandboxModule::spawnObject(const std::string& asset, const glm::vec3& position, float yawDegrees, uint32_t id,
                                                  const std::string& pack) {
    kke::ModelModule::ModelId model = loadAsset(asset, pack);
    const kke::ModelData* d = model ? m_models->model(model) : nullptr;
    if (!d) {
        m_status = "Could not load '" + asset + "'";
        return nullptr;
    }
    Object o;
    o.id = id ? id : m_nextId++;
    m_nextId = std::max(m_nextId, o.id + 1);
    o.asset = asset;
    o.pack = resolve(asset, pack)->pack; // loaded, so it resolves
    o.position = position;
    o.yawDegrees = yawDegrees;
    o.model = model;
    o.instance = m_models->spawn(model, objectTransform(*d, position, yawDegrees));
    o.character = !d->bones.empty() && d->meshes.size() > 0 && resolve(asset, pack)->skinned;
    if (o.character && !d->animations.empty()) m_models->playAnimation(o.instance, 0, true);
    m_objects.push_back(std::move(o));
    return &m_objects.back();
}

SandboxModule::Object* SandboxModule::find(uint32_t id) {
    for (Object& o : m_objects) if (o.id == id) return &o;
    return nullptr;
}

void SandboxModule::removeObject(uint32_t id) {
    auto it = std::find_if(m_objects.begin(), m_objects.end(), [&](const Object& o) { return o.id == id; });
    if (it == m_objects.end()) return;
    if (it->ragdoll && m_ragdolls) m_ragdolls->destroyRagdoll(it->ragdoll);
#if KKE_ENABLE_FEMFX
    if (it->proxy) if (auto* p = m_app->getModule<kke::PhysicsModule>()) p->removeObject(it->proxy);
#endif
    m_models->remove(it->instance);
    m_objects.erase(it);
    std::erase(m_selection, id);
    if (m_selected == id) m_selected = m_selection.empty() ? 0 : m_selection.back();
    if (m_hovered == id) m_hovered = 0;
}

void SandboxModule::clearAll() {
    cancelPlacing();
    while (!m_objects.empty()) removeObject(m_objects.back().id);
#if KKE_ENABLE_FEMFX
    if (auto* p = m_app->getModule<kke::PhysicsModule>()) for (uint32_t b : m_balls) p->removeObject(b);
#endif
    m_balls.clear();
}

void SandboxModule::worldBounds(const Object& o, glm::vec3& mn, glm::vec3& mx) const {
    const kke::ModelData* d = m_models->model(o.model);
    if (!d) { mn = mx = o.position; return; }
    kke::transformAabb(d->boundsMin, d->boundsMax, objectTransform(o), mn, mx);
}

// ---------------------------------------------------------------- picking

kke::Ray SandboxModule::mouseRay() const {
    const kke::Camera& cam = m_app->camera();
    const auto& mouse = m_app->window().mouseState();
    int w = 1, h = 1;
    SDL_GetWindowSize(m_app->window().handle(), &w, &h); // points, same space as the mouse
    glm::mat4 view = glm::lookAt(cam.position, cam.target, cam.up);
    glm::mat4 proj = kke::engineProjection(cam.fovDegrees, static_cast<float>(w) / static_cast<float>(std::max(h, 1)), cam.nearPlane, cam.farPlane);
    return kke::screenToRay({ mouse.x, mouse.y }, { static_cast<float>(w), static_cast<float>(h) }, view, proj);
}

// Brute force over every object's bounds: a slab test is ~20 flops, so
// even 2,000 objects is well under 0.1 ms. A grid/BVH is in the
// OPTIMIZATION.md backlog for when levels outgrow that.
uint32_t SandboxModule::pickObject() const {
    kke::Ray ray = mouseRay();
    float best = 1e30f;
    uint32_t hit = 0;
    for (const Object& o : m_objects) {
        if (o.id == m_movingId || o.proxy || o.ragdoll) continue;
        glm::vec3 mn, mx;
        worldBounds(o, mn, mx);
        float t = kke::rayAabb(ray, mn, mx);
        if (t >= 0.0f && t < best) { best = t; hit = o.id; }
    }
    return hit;
}

// Where a new piece goes: on top of the object under the mouse (stacking
// crates, props on tables), else on the ground plane y = 0. X/Z snap to
// the grid; Y never snaps, so stacking is exact.
bool SandboxModule::placementPoint(glm::vec3& out, uint32_t ignoreId) const {
    kke::Ray ray = mouseRay();
    float best = kke::rayPlaneY(ray, 0.0f);
    float topY = 0.0f;
    for (const Object& o : m_objects) {
        if (o.id == ignoreId || o.proxy || o.ragdoll) continue;
        // Moving a group: its other members travel along, so they're not
        // something to land on.
        if (std::any_of(m_groupMove.begin(), m_groupMove.end(), [&](const DragStart& g) { return g.id == o.id; })) continue;
        glm::vec3 mn, mx;
        worldBounds(o, mn, mx);
        float t = kke::rayAabb(ray, mn, mx);
        if (t >= 0.0f && (best < 0.0f || t < best)) { best = t; topY = mx.y; }
    }
    if (best < 0.0f || best > m_app->camera().farPlane) return false;
    glm::vec3 p = ray.at(best);
    out = glm::vec3(kke::snapTo(p.x, m_snap), topY, kke::snapTo(p.z, m_snap));
    return true;
}

// ---------------------------------------------------------------- selection

bool SandboxModule::isSelected(uint32_t id) const {
    return std::find(m_selection.begin(), m_selection.end(), id) != m_selection.end();
}

// Click: select only this. Shift+click: add it, or take it out again.
void SandboxModule::select(uint32_t id, bool additive) {
    if (!additive) m_selection.clear();
    if (!id) {
        if (!additive) m_selected = 0;
        return;
    }
    if (additive && isSelected(id)) {
        std::erase(m_selection, id);
        m_selected = m_selection.empty() ? 0 : m_selection.back();
        return;
    }
    m_selection.push_back(id);
    m_selected = id;
}

void SandboxModule::clearSelection() {
    m_selection.clear();
    m_selected = 0;
}

std::vector<SandboxModule::Object*> SandboxModule::selectedObjects() {
    std::vector<Object*> out;
    for (uint32_t id : m_selection)
        if (Object* o = find(id); o && !o->ragdoll && !o->proxy) out.push_back(o);
    return out;
}

// ---------------------------------------------------------------- undo / redo

SandboxModule::Snapshot SandboxModule::snapshot() const {
    Snapshot s;
    s.reserve(m_objects.size());
    for (const Object& o : m_objects)
        s.push_back({ o.id, o.asset, o.pack, o.texture, o.position, o.yawDegrees, o.scale, o.collision, o.fractureSeed, o.proxy ? o.breakMaterial : -1 });
    return s;
}

// Call before an edit: the level as it was goes on the undo stack. Any
// new edit forgets what was undone (the usual editor rule).
void SandboxModule::pushUndo() {
    m_undo.push_back(snapshot());
    if (m_undo.size() > kMaxUndo) m_undo.erase(m_undo.begin());
    m_redo.clear();
}

// Puts the level back to `s` by difference, by object id: untouched
// objects stay as they are (a live breakable keeps its pieces, a ragdoll
// keeps lying there), changed ones are updated, deleted ones come back
// with their old id, added ones go.
void SandboxModule::restore(const Snapshot& s) {
    cancelPlacing();
    m_drag = Handle::None;
    std::vector<uint32_t> gone;
    for (const Object& o : m_objects)
        if (std::none_of(s.begin(), s.end(), [&](const ObjectState& st) { return st.id == o.id; })) gone.push_back(o.id);
    for (uint32_t id : gone) removeObject(id);
    size_t missing = 0;
    for (const ObjectState& st : s) {
        Object* o = find(st.id);
        if (!o) {
            o = spawnObject(st.asset, st.position, st.yawDegrees, st.id, st.pack);
            if (!o) { ++missing; continue; }
        }
        if (o->ragdoll) continue;
        const bool moved = o->position != st.position || o->yawDegrees != st.yawDegrees || o->scale != st.scale;
        if (moved && o->proxy) restoreProp(*o); // re-made below if the snapshot has it breakable
        o->position = st.position;
        o->yawDegrees = st.yawDegrees;
        o->scale = st.scale;
        o->collision = st.collision;
        o->fractureSeed = st.fractureSeed;
        if (o->texture != st.texture) {
            o->texture = st.texture;
            m_models->setTextureOverride(o->instance, o->texture);
        }
        if (!o->proxy) applyTransform(*o);
        if (st.breakMaterial < 0 && o->proxy) restoreProp(*o);
        else if (st.breakMaterial >= 0 && !o->proxy && m_hasFemfx) {
            const int keep = m_breakMaterial, keepPattern = m_patternOverride;
            m_breakMaterial = st.breakMaterial;
            m_patternOverride = 0;
            makeBreakable(*o);
            m_breakMaterial = keep;
            m_patternOverride = keepPattern;
        }
    }
    // Saved scenes list objects in creation order: keep it that way.
    std::sort(m_objects.begin(), m_objects.end(), [](const Object& a, const Object& b) { return a.id < b.id; });
    std::erase_if(m_selection, [&](uint32_t id) { return !find(id); });
    if (!find(m_selected)) m_selected = m_selection.empty() ? 0 : m_selection.back();
    if (missing) m_status = std::to_string(missing) + " object(s) couldn't be restored: their assets no longer load";
}

void SandboxModule::undo() {
    if (m_tool == Tool::Place) {
        const bool duplicating = m_duplicating;
        cancelPlacing(); // cancelling a Ctrl+D is itself its undo
        if (duplicating) { m_status = "Undone (" + std::to_string(m_undo.size()) + " more)"; return; }
    }
    if (m_undo.empty()) { m_status = "Nothing to undo"; return; }
    m_redo.push_back(snapshot());
    Snapshot s = std::move(m_undo.back());
    m_undo.pop_back();
    restore(s);
    m_status = "Undone (" + std::to_string(m_undo.size()) + " more)";
}

void SandboxModule::redo() {
    cancelPlacing();
    if (m_redo.empty()) { m_status = "Nothing to redo"; return; }
    m_undo.push_back(snapshot());
    Snapshot s = std::move(m_redo.back());
    m_redo.pop_back();
    restore(s);
    m_status = "Redone (" + std::to_string(m_redo.size()) + " more)";
}

// ---------------------------------------------------------------- editing

// Around the selection's centre, keeping each object's place in the group.
void SandboxModule::rotateSelection(float degrees) {
    std::vector<Object*> sel = selectedObjects();
    if (sel.empty()) return;
    pushUndo();
    glm::vec3 pivot(0.0f);
    for (Object* o : sel) pivot += o->position;
    pivot /= static_cast<float>(sel.size());
    const float c = std::cos(glm::radians(degrees)), sn = std::sin(glm::radians(degrees));
    for (Object* o : sel) {
        if (sel.size() > 1) {
            const glm::vec3 d = o->position - pivot;
            // Same direction as the yaw (glm::rotate around +Y).
            o->position = pivot + glm::vec3(d.x * c + d.z * sn, d.y, -d.x * sn + d.z * c);
        }
        o->yawDegrees = std::fmod(o->yawDegrees + degrees + 360.0f, 360.0f);
        applyTransform(*o);
    }
}

void SandboxModule::deleteSelection() {
    std::vector<uint32_t> ids = m_selection;
    if (ids.empty()) return;
    pushUndo();
    for (uint32_t id : ids) removeObject(id);
    clearSelection();
}

// Copies land on the originals and are picked up to be moved (G) at once.
void SandboxModule::duplicateSelection() {
    std::vector<Object*> sel = selectedObjects();
    if (sel.empty()) return;
    pushUndo();
    struct Copy { std::string asset, pack, texture; glm::vec3 position; float yaw, scale; kke::SceneObject::Collision collision; bool primary; };
    std::vector<Copy> copies;
    for (Object* o : sel) copies.push_back({ o->asset, o->pack, o->texture, o->position, o->yawDegrees, o->scale, o->collision, o->id == m_selected });
    clearSelection();
    uint32_t primary = 0;
    for (const Copy& c : copies) {
        Object* o = spawnObject(c.asset, c.position, c.yaw, 0, c.pack);
        if (!o) continue;
        o->texture = c.texture;
        o->scale = c.scale;
        o->collision = c.collision;
        m_models->setTextureOverride(o->instance, o->texture);
        applyTransform(*o);
        m_selection.push_back(o->id);
        if (c.primary || !primary) primary = o->id;
    }
    m_selected = primary;
    if (Object* p = find(primary)) {
        beginPlacing(p->asset, p->yawDegrees, p->id, p->pack);
        m_duplicating = m_tool == Tool::Place;
    }
}

// ---------------------------------------------------------------- placing

void SandboxModule::beginPlacing(const std::string& asset, float yawDegrees, uint32_t movingId, const std::string& pack) {
    cancelPlacing();
    kke::ModelModule::ModelId model = loadAsset(asset, pack);
    if (!model) { m_status = "Could not load '" + asset + "'"; return; }
    m_tool = Tool::Place;
    m_placeAsset = asset;
    m_placePack = pack;
    m_placeYaw = yawDegrees;
    m_movingId = movingId;
    m_groupMove.clear();
    if (Object* moving = find(movingId)) {
        // Move the real object instead of spawning a ghost; the rest of the
        // selection keeps its offset from it.
        m_ghost = moving->instance;
        m_placeScale = moving->scale;
        for (Object* o : selectedObjects()) m_groupMove.push_back({ o->id, o->position, o->yawDegrees, o->scale });
    } else {
        // The preview is the real model with its real texture; the green
        // outline (drawn in update()) marks it as not placed yet.
        m_placeScale = 1.0f;
        m_ghost = m_models->spawn(model);
        if (const kke::CatalogPack* p = packOf(asset, pack); p && m_variant > 0 && m_variant < static_cast<int>(p->textureVariants.size()))
            m_models->setTextureOverride(m_ghost, p->textureVariants[m_variant]);
    }
    m_ghostModel = model;
    m_ghostValid = false;
}

void SandboxModule::cancelPlacing() {
    if (m_tool != Tool::Place) return;
    if (Object* moving = find(m_movingId)) {
        // Put moved objects back where they were.
        for (Object& o : m_objects) {
            if (o.id != moving->id && std::none_of(m_groupMove.begin(), m_groupMove.end(), [&](const DragStart& g) { return g.id == o.id; })) continue;
            applyTransform(o);
            m_models->setVisible(o.instance, true);
        }
    } else if (m_ghost) {
        m_models->remove(m_ghost);
    }
    m_ghost = 0;
    m_movingId = 0;
    m_groupMove.clear();
    m_tool = Tool::Select;
    // Esc right after Ctrl+D: no copies left lying on the originals.
    if (m_duplicating) {
        m_duplicating = false;
        if (!m_undo.empty()) {
            Snapshot before = std::move(m_undo.back());
            m_undo.pop_back();
            restore(before);
        }
    }
}

void SandboxModule::commitPlacement(bool keepPlacing) {
    if (!m_ghostValid) return;
    if (Object* moving = find(m_movingId)) {
        if (!m_duplicating) pushUndo(); // a duplicate's undo step was taken when copying
        m_duplicating = false;
        const glm::vec3 delta = m_ghostPos - moving->position;
        for (const DragStart& g : m_groupMove) {
            Object* o = find(g.id);
            if (!o || o->id == moving->id) continue;
            o->position = g.position + delta;
            o->yawDegrees = g.yaw;
            applyTransform(*o);
        }
        moving->position = m_ghostPos;
        moving->yawDegrees = m_placeYaw;
        applyTransform(*moving);
        m_models->setVisible(moving->instance, true);
        m_ghost = 0;
        m_movingId = 0;
        m_groupMove.clear();
        m_tool = Tool::Select;
        return;
    }
    pushUndo();
    if (Object* o = spawnObject(m_placeAsset, m_ghostPos, m_placeYaw, 0, m_placePack)) {
        select(o->id, false);
        o->texture = m_models->textureOverride(m_ghost);
        m_models->setTextureOverride(o->instance, o->texture);
    }
    if (!keepPlacing) cancelPlacing();
}

// ---------------------------------------------------------------- gizmo

// At the selection's centre (bottom-centres averaged), sized to stay the
// same on screen at any zoom.
bool SandboxModule::gizmoPivot(glm::vec3& pivot, float& size) const {
    if (m_tool != Tool::Select || m_selection.empty()) return false;
    glm::vec3 sum(0.0f);
    int n = 0;
    for (uint32_t id : m_selection)
        for (const Object& o : m_objects)
            if (o.id == id && !o.ragdoll && !o.proxy) { sum += o.position; ++n; }
    if (!n) return false;
    pivot = sum / static_cast<float>(n);
    size = std::max(0.2f, glm::length(m_app->camera().position - pivot) * 0.15f);
    return true;
}

SandboxModule::Handle SandboxModule::hoverHandle() const {
    glm::vec3 pivot;
    float size;
    if (!gizmoPivot(pivot, size)) return Handle::None;
    const kke::Ray ray = mouseRay();
    const float grab = size * 0.08f;
    if (m_gizmo == Gizmo::Move) {
        Handle best = Handle::None;
        float bestD = grab;
        const glm::vec3 axes[3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
        const Handle ids[3] = { Handle::X, Handle::Y, Handle::Z };
        for (int i = 0; i < 3; ++i) {
            float d = 0.0f;
            const float s = rayLineParam(ray, pivot, axes[i], &d);
            if (s > size * 0.1f && s < size * 1.1f && d < bestD) { bestD = d; best = ids[i]; }
        }
        return best;
    }
    if (m_gizmo == Gizmo::Rotate) {
        const float t = kke::rayPlaneY(ray, pivot.y);
        if (t < 0.0f) return Handle::None;
        const glm::vec3 h = ray.at(t) - pivot;
        return std::fabs(glm::length(glm::vec2(h.x, h.z)) - size) < grab * 1.5f ? Handle::Ring : Handle::None;
    }
    const glm::vec3 c = pivot + glm::vec3(0.0f, size, 0.0f);
    const float t = kke::rayAabb(ray, c - glm::vec3(size * 0.1f), c + glm::vec3(size * 0.1f));
    return t >= 0.0f ? Handle::Scale : Handle::None;
}

void SandboxModule::beginDrag(Handle h) {
    glm::vec3 pivot;
    float size;
    if (h == Handle::None || !gizmoPivot(pivot, size)) return;
    m_drag = h;
    m_dragPivot = pivot;
    m_dragSize = size;
    m_dragMoved = false;
    m_dragStart.clear();
    for (Object* o : selectedObjects()) m_dragStart.push_back({ o->id, o->position, o->yawDegrees, o->scale });
    m_undo.push_back(snapshot()); // popped again if the drag changes nothing
    if (m_undo.size() > kMaxUndo) m_undo.erase(m_undo.begin());
    const kke::Ray ray = mouseRay();
    if (h == Handle::X || h == Handle::Y || h == Handle::Z) {
        const glm::vec3 axis = h == Handle::X ? glm::vec3(1, 0, 0) : h == Handle::Y ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
        m_dragStartParam = rayLineParam(ray, pivot, axis);
    } else if (h == Handle::Ring) {
        const glm::vec3 p = ray.at(std::max(0.0f, kke::rayPlaneY(ray, pivot.y))) - pivot;
        m_dragStartParam = std::atan2(p.x, p.z);
    }
    m_dragStartMouseY = m_app->window().mouseState().y;
}

void SandboxModule::updateDrag() {
    const kke::Ray ray = mouseRay();
    const bool fine = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0; // Shift: no snapping
    for (const DragStart& g : m_dragStart) {
        Object* o = find(g.id);
        if (!o) continue;
        if (m_drag == Handle::X || m_drag == Handle::Y || m_drag == Handle::Z) {
            const glm::vec3 axis = m_drag == Handle::X ? glm::vec3(1, 0, 0) : m_drag == Handle::Y ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
            float delta = rayLineParam(ray, m_dragPivot, axis) - m_dragStartParam;
            if (!fine && m_snap > 0.0f) delta = kke::snapTo(delta, m_snap);
            o->position = g.position + axis * delta;
            if (m_drag == Handle::Y) o->position.y = std::max(o->position.y, 0.0f); // not under the ground
        } else if (m_drag == Handle::Ring) {
            const float t = kke::rayPlaneY(ray, m_dragPivot.y);
            if (t < 0.0f) continue;
            const glm::vec3 p = ray.at(t) - m_dragPivot;
            float deg = glm::degrees(std::atan2(p.x, p.z) - m_dragStartParam);
            if (!fine && m_rotateStep > 0.0f) deg = kke::snapTo(deg, m_rotateStep);
            const float c = std::cos(glm::radians(deg)), sn = std::sin(glm::radians(deg));
            const glm::vec3 d = g.position - m_dragPivot;
            o->position = m_dragPivot + glm::vec3(d.x * c + d.z * sn, d.y, -d.x * sn + d.z * c);
            o->yawDegrees = std::fmod(g.yaw + deg + 720.0f, 360.0f);
        } else if (m_drag == Handle::Scale) {
            // Up = bigger; 200 px doubles. Snaps to 10 % steps.
            float f = std::exp2((m_dragStartMouseY - m_app->window().mouseState().y) / 200.0f);
            if (!fine) f = std::max(0.1f, std::round(f * 10.0f) / 10.0f);
            const float scale = std::clamp(g.scale * f, 0.05f, 20.0f);
            const float k = scale / g.scale;
            const glm::vec3 d = g.position - m_dragPivot;
            o->position = m_dragPivot + glm::vec3(d.x * k, d.y * k, d.z * k);
            o->scale = scale;
        }
        if (o->position != g.position || o->yawDegrees != g.yaw || o->scale != g.scale) m_dragMoved = true;
        applyTransform(*o);
    }
}

void SandboxModule::drawGizmo() {
    glm::vec3 pivot;
    float size;
    if (m_drag != Handle::None) { pivot = m_dragPivot; size = m_dragSize; }
    else if (!gizmoPivot(pivot, size)) return;
    const Handle hot = m_drag != Handle::None ? m_drag : m_hoverHandle;
    auto col = [&](Handle h, const glm::vec3& c) { return h == hot ? glm::vec3(1.0f, 1.0f, 0.3f) : c; };
    const float w = size * 0.02f;
    if (m_gizmo == Gizmo::Move) {
        m_debug->line(pivot, pivot + glm::vec3(size, 0, 0), col(Handle::X, { 0.95f, 0.25f, 0.25f }), w, true);
        m_debug->line(pivot, pivot + glm::vec3(0, size, 0), col(Handle::Y, { 0.3f, 0.9f, 0.3f }), w, true);
        m_debug->line(pivot, pivot + glm::vec3(0, 0, size), col(Handle::Z, { 0.3f, 0.5f, 1.0f }), w, true);
        const float tip = size * 0.06f;
        m_debug->box(pivot + glm::vec3(size, 0, 0) - glm::vec3(tip), pivot + glm::vec3(size, 0, 0) + glm::vec3(tip), col(Handle::X, { 0.95f, 0.25f, 0.25f }), w, true);
        m_debug->box(pivot + glm::vec3(0, size, 0) - glm::vec3(tip), pivot + glm::vec3(0, size, 0) + glm::vec3(tip), col(Handle::Y, { 0.3f, 0.9f, 0.3f }), w, true);
        m_debug->box(pivot + glm::vec3(0, 0, size) - glm::vec3(tip), pivot + glm::vec3(0, 0, size) + glm::vec3(tip), col(Handle::Z, { 0.3f, 0.5f, 1.0f }), w, true);
    } else if (m_gizmo == Gizmo::Rotate) {
        const int segments = 48;
        for (int i = 0; i < segments; ++i) {
            const float a0 = glm::two_pi<float>() * i / segments, a1 = glm::two_pi<float>() * (i + 1) / segments;
            m_debug->line(pivot + glm::vec3(std::sin(a0), 0, std::cos(a0)) * size, pivot + glm::vec3(std::sin(a1), 0, std::cos(a1)) * size,
                          col(Handle::Ring, { 0.3f, 0.9f, 0.3f }), w, true);
        }
    } else {
        const glm::vec3 c = pivot + glm::vec3(0, size, 0);
        m_debug->line(pivot, c, col(Handle::Scale, { 0.9f, 0.9f, 0.9f }), w, true);
        m_debug->box(c - glm::vec3(size * 0.1f), c + glm::vec3(size * 0.1f), col(Handle::Scale, { 0.9f, 0.9f, 0.9f }), w, true);
    }
}

// ---------------------------------------------------------------- frame

void SandboxModule::update(const kke::UpdateContext&) {
    bool mouseFree = !ImGui::GetIO().WantCaptureMouse && !m_app->uiCapturesMouse();

    // Ground grid around the camera target, snapped so it doesn't swim.
    glm::vec3 target = m_app->camera().target;
    float gridStep = m_snap > 0.0f ? std::max(m_snap, 0.5f) : 1.0f;
    glm::vec3 gridCenter(kke::snapTo(target.x, gridStep * 2), 0.002f, kke::snapTo(target.z, gridStep * 2));
    m_debug->gridXZ(gridCenter, 12.0f, gridStep, glm::vec3(0.32f, 0.34f, 0.4f), 0.012f);

    updateBreakables();

    // Ragdolls drive their characters' skeletons.
    std::vector<glm::mat4> bodies;
    for (Object& o : m_objects) {
        if (o.ragdoll && m_ragdolls && m_ragdolls->ragdollBodyTransforms(o.ragdoll, bodies)) {
            const kke::ModelData* d = m_models->model(o.model);
            glm::mat4 worldToModel = glm::inverse(m_models->transform(o.instance));
            m_models->setBoneWorldOverride(o.instance, kke::poseFromRagdoll(*d, o.binding, bodies, worldToModel));
        }
    }

    // Level markers: the player spawn (arrow = facing) and point lights,
    // which light the sandbox too (slots 2-3; 0-1 are the sun and sky).
    {
        const glm::vec3 fwd(std::sin(glm::radians(m_spawnYaw)), 0.0f, -std::cos(glm::radians(m_spawnYaw)));
        m_debug->box(m_spawn + glm::vec3(-0.3f, 0.0f, -0.3f), m_spawn + glm::vec3(0.3f, 1.8f, 0.3f), kSpawnColor, 0.02f);
        m_debug->line(m_spawn + glm::vec3(0, 0.05f, 0), m_spawn + glm::vec3(0, 0.05f, 0) + fwd, kSpawnColor, 0.03f);
        kke::Lighting& lighting = m_app->lighting();
        for (int i = 0; i < 2; ++i) {
            kke::Light& l = lighting.lights[2 + i];
            l.enabled = i < static_cast<int>(m_pointLights.size());
            if (!l.enabled) continue;
            l.isDirectional = false;
            l.position = m_pointLights[i].position;
            l.color = m_pointLights[i].color;
            l.intensity = m_pointLights[i].intensity;
            m_debug->cross(l.position, i == m_selectedLight ? 0.5f : 0.3f, kLightColor);
        }
    }

    if (m_drag != Handle::None) {
        updateDrag();
        m_hovered = 0;
    } else if (m_tool == Tool::Place) {
        m_ghostValid = mouseFree && placementPoint(m_ghostPos, m_movingId);
        m_models->setVisible(m_ghost, m_ghostValid);
        if (m_ghostValid) {
            if (const kke::ModelData* d = m_models->model(m_ghostModel)) {
                glm::mat4 t = objectTransform(*d, m_ghostPos, m_placeYaw, m_placeScale);
                m_models->setTransform(m_ghost, t);
                m_debug->box(t, d->boundsMin, d->boundsMax, kGhostColor, 0.02f, true);
            }
            m_debug->cross(m_ghostPos, 0.25f, kGhostColor);
            if (Object* moving = find(m_movingId)) {
                const glm::vec3 delta = m_ghostPos - moving->position;
                for (const DragStart& g : m_groupMove) {
                    Object* o = find(g.id);
                    if (!o || o->id == m_movingId) continue;
                    const kke::ModelData* d = m_models->model(o->model);
                    if (!d) continue;
                    const glm::mat4 t = objectTransform(*d, g.position + delta, g.yaw, g.scale);
                    m_models->setTransform(o->instance, t);
                    m_debug->box(t, d->boundsMin, d->boundsMax, kGhostColor, 0.02f, true);
                }
            }
        }
        m_hovered = 0;
    } else if (m_tool == Tool::Shoot) {
        m_hovered = 0;
        // Aim marker where the ball is heading (first hit: ground or object).
        glm::vec3 aim;
        if (mouseFree && placementPoint(aim, 0)) m_debug->cross(aim, 0.3f, glm::vec3(1.0f, 0.35f, 0.25f));
    } else {
        m_hoverHandle = mouseFree ? hoverHandle() : Handle::None;
        m_hovered = mouseFree && m_hoverHandle == Handle::None ? pickObject() : 0;
    }

    for (const Object& o : m_objects) {
        const bool selected = isSelected(o.id);
        if (!selected && o.id != m_hovered) continue;
        if (o.proxy || o.ragdoll) continue;
        const kke::ModelData* d = m_models->model(o.model);
        if (!d) continue;
        m_debug->box(objectTransform(o), d->boundsMin, d->boundsMax, selected ? kSelectColor : kHoverColor,
                     selected ? (o.id == m_selected ? 0.03f : 0.02f) : 0.015f, true);
    }
    drawGizmo();
}

void SandboxModule::onEvent(const SDL_Event& event) {
    ImGuiIO& io = ImGui::GetIO();
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT && m_drag != Handle::None) {
        m_drag = Handle::None;
        if (!m_dragMoved && !m_undo.empty()) m_undo.pop_back(); // a click on a handle is not an edit
        else m_redo.clear();
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
        if (io.WantCaptureMouse || m_app->uiCapturesMouse()) return;
        bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
        if (m_tool == Tool::Place) commitPlacement(/*keepPlacing=*/shift);
        else if (m_tool == Tool::Shoot) throwBall();
        else if (Handle h = hoverHandle(); h != Handle::None) beginDrag(h);
        else select(pickObject(), shift);
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_WHEEL && m_tool == Tool::Place && (SDL_GetModState() & SDL_KMOD_CTRL)) {
        m_placeYaw += event.wheel.y > 0 ? m_rotateStep : -m_rotateStep;
        return;
    }
    // WantTextInput, not WantCaptureKeyboard: ImGui claims the keyboard
    // whenever one of its windows has focus (e.g. right after clicking an
    // asset), which silently swallowed F/Del/R. Only typing into a text
    // field should block the shortcuts (BUG-041).
    if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat || io.WantTextInput) return;
    bool ctrl = (event.key.mod & SDL_KMOD_CTRL) != 0;
    bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
    Object* sel = find(m_selected);
    switch (event.key.key) {
    case SDLK_ESCAPE:
        if (m_tool == Tool::Place) cancelPlacing();
        else if (m_tool == Tool::Shoot) m_tool = Tool::Select;
        else clearSelection();
        break;
    case SDLK_1:
        cancelPlacing();
        m_tool = Tool::Select;
        break;
    case SDLK_2:
        cancelPlacing();
        if (m_hasFemfx) m_tool = Tool::Shoot;
        break;
    case SDLK_TAB:
        // Gizmo: move -> rotate -> scale.
        m_gizmo = m_gizmo == Gizmo::Move ? Gizmo::Rotate : m_gizmo == Gizmo::Rotate ? Gizmo::Scale : Gizmo::Move;
        break;
    case SDLK_SPACE:
        throwBall();
        break;
    case SDLK_R:
        if (m_tool == Tool::Place) m_placeYaw += shift ? -m_rotateStep : m_rotateStep;
        else rotateSelection(shift ? -m_rotateStep : m_rotateStep);
        break;
    case SDLK_DELETE:
    case SDLK_BACKSPACE:
        if (m_tool == Tool::Select) deleteSelection();
        break;
    case SDLK_G:
        if (sel && !sel->ragdoll && !sel->proxy) beginPlacing(sel->asset, sel->yawDegrees, sel->id, sel->pack);
        break;
    case SDLK_D:
        if (ctrl) duplicateSelection();
        break;
    case SDLK_A:
        if (ctrl) {
            clearSelection();
            for (const Object& o : m_objects) m_selection.push_back(o.id);
            m_selected = m_selection.empty() ? 0 : m_selection.back();
        }
        break;
    case SDLK_Z:
        if (ctrl && shift) redo();
        else if (ctrl) undo();
        break;
    case SDLK_Y:
        if (ctrl) redo();
        break;
    case SDLK_F1:
        m_showEnginePanels = !m_showEnginePanels;
        for (kke::Module* m : m_enginePanels) m->setUiVisible(m_showEnginePanels);
        break;
    case SDLK_F:
        throwBall();
        break;
    case SDLK_K:
        if (sel && sel->character) {
            if (sel->ragdoll) standUp(*sel);
            else {
                glm::vec3 away = m_app->camera().target - m_app->camera().position;
                away.y = 0.0f;
                away = glm::length(away) > 1e-3f ? glm::normalize(away) : glm::vec3(0, 0, -1);
                ragdoll(*sel, away * 4.0f + glm::vec3(0, 1.0f, 0));
            }
        }
        break;
    case SDLK_X:
        if (sel && !sel->character) {
            pushUndo();
            if (sel->proxy) restoreProp(*sel);
            else makeBreakable(*sel);
        }
        break;
    case SDLK_S:
        if (ctrl) saveLayout(m_layoutPath);
        break;
    case SDLK_L:
        if (ctrl) loadLayout(m_layoutPath);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- physics toys

void SandboxModule::ragdoll(Object& o, const glm::vec3& push) {
    if (!m_ragdolls || o.ragdoll) return;
    const kke::ModelData* d = m_models->model(o.model);
    if (!d) return;
    glm::mat4 instance = m_models->transform(o.instance);
    std::vector<glm::mat4> world = m_models->boneWorld(o.instance);
    for (glm::mat4& w : world) w = instance * w;
    std::string missing;
    o.ragdollDesc = kke::buildHumanoidRagdoll(*d, world, 70.0f, &missing);
    if (o.ragdollDesc.bodies.empty()) {
        m_status = o.asset + " can't ragdoll: no '" + missing + "' bone";
        return;
    }
    o.binding = kke::bindSkeletonToRagdoll(*d, world, o.ragdollDesc);
    o.ragdoll = m_ragdolls->createRagdoll(o.ragdollDesc, glm::vec3(0.0f));
    if (!o.ragdoll) return;
    // Shove the upper body harder than the legs so it topples, not slides.
    for (const char* body : { "torso", "head" }) m_ragdolls->pushRagdollBody(o.ragdoll, o.ragdollDesc.findBody(body), push);
    m_ragdolls->pushRagdollBody(o.ragdoll, o.ragdollDesc.findBody("pelvis"), push * 0.4f);
}

void SandboxModule::standUp(Object& o) {
    if (!o.ragdoll || !m_ragdolls) return;
    m_ragdolls->destroyRagdoll(o.ragdoll);
    o.ragdoll = 0;
    m_models->setBoneWorldOverride(o.instance, {});
}

// Turns a placed prop into a physics object with its own shape and look:
//   1. the prop's triangles (all mesh parts, in world space) are voxelized
//      into a tet volume (kke::voxelizeToTets, budget m_detailCells);
//   2. tets are grouped into chunks by the material's fracture pattern and
//      faces inside a chunk are locked (kke::fractureFlagsFromChunks), so
//      it breaks into splinters / chunks / radial shards, not single tets;
//      interior vertices are jittered so cracks aren't grid-straight;
//   3. every render vertex is glued to its tet (kke::embedPoints), and
//      each frame the physics is awake the prop's own mesh is redrawn from
//      the tets (updateBreakables) — same UVs, texture and overlay, bending
//      and breaking with the simulation. PhysicsModule only draws the
//      fresh crack faces, textured with the material.
void SandboxModule::makeBreakable(Object& o) {
#if KKE_ENABLE_FEMFX
    auto* physics = m_app->getModule<kke::PhysicsModule>();
    const kke::ModelData* d = m_models->model(o.model);
    if (!physics || !d || o.proxy || o.character) return;
    const double start = SDL_GetPerformanceCounter() / static_cast<double>(SDL_GetPerformanceFrequency());
    const glm::mat4 t = objectTransform(o);
    const glm::mat3 rot(glm::transpose(glm::inverse(glm::mat3(t)))); // normals, also right when scaled

    // World-space vertices and triangles of every mesh part.
    std::vector<glm::vec3> points, normals;
    std::vector<uint32_t> tris;
    o.partOffsets.clear();
    for (const kke::ModelMesh& mesh : d->meshes) {
        o.partOffsets.push_back(points.size());
        uint32_t base = static_cast<uint32_t>(points.size());
        for (const kke::ModelVertex& v : mesh.vertices) {
            points.push_back(glm::vec3(t * glm::vec4(v.position, 1.0f)));
            normals.push_back(glm::normalize(rot * v.normal));
        }
        for (uint32_t i : mesh.indices) tris.push_back(base + i);
    }
    if (tris.empty()) return;
    // Simulate around the prop's centre (FEMFX adds the spawn position).
    glm::vec3 mn(1e30f), mx(-1e30f);
    for (const glm::vec3& p : points) { mn = glm::min(mn, p); mx = glm::max(mx, p); }
    const glm::vec3 center = (mn + mx) * 0.5f;
    for (glm::vec3& p : points) p -= center;

    const BreakMaterial& bm = kBreakMaterials[std::clamp(m_breakMaterial, 0, kBreakMaterialCount - 1)];
    const kke::FracturePattern pattern = m_patternOverride > 0 ? static_cast<kke::FracturePattern>(m_patternOverride - 1) : bm.pattern;
    const glm::vec3 size = mx - mn;
    const float maxDim = std::max({ size.x, size.y, size.z });
    // Pieces need several voxels each to have any shape: cells well
    // under the chunk size (the budget grows them again for big props).
    const float cell = std::clamp(std::min(maxDim / 8.0f, bm.chunkSize * m_chunkScale * 0.4f), 0.04f, 0.4f);
    kke::VoxelTetMesh vox = kke::voxelizeToTets(points, tris, cell, static_cast<size_t>(m_detailCells));
    if (vox.mesh.tets.empty()) return;
    // Hug the prop: pull the voxel surface onto the real triangles.
    kke::fitSurfaceToMesh(vox.mesh, points, tris, vox.cellSize * 0.75f);
    // Pieces: a Voronoi diagram of the material's pattern, cut into the
    // tet volume (kke/VoronoiFracture.h). Seed = the world's seed mixed
    // with this object's own: every prop breaks its own way, and the
    // same layout + seeds breaks the same way every time.
    if (!o.fractureSeed) o.fractureSeed = o.id;
    o.breakMaterial = std::clamp(m_breakMaterial, 0, kBreakMaterialCount - 1);
    kke::FractureSeedOptions fo;
    fo.pattern = pattern;
    fo.chunkSize = bm.chunkSize * m_chunkScale;
    fo.seed = kke::fractureSeed(m_worldSeed, o.fractureSeed);
    fo.cellsPerCluster = bm.cellsPerCluster;
    kke::BakedFracture baked = kke::bakeFracture(vox.mesh, fo);
    vox.mesh = baked.cut.mesh; // same tets, border vertices on the Voronoi planes

    kke::Material material = bm.material;
    material.fractureStressThreshold *= m_toughness;
    kke::PhysicsModule::TetSpawnOptions opts;
    opts.fracture = pattern != kke::FracturePattern::Solid;
    opts.plastic = bm.plastic;
    if (opts.fracture) {
        opts.chunkOfTet = baked.cut.chunkOfTet;
        opts.tetStrength = baked.cut.tetStrength;
    }
    opts.drawOnlyCracks = true;
    opts.armFractureAfterSeconds = 2.0f; // settle, then arm relative to resting stress (BUG-043)
    // Insides take the prop's own colours: each tet vertex gets the UV of
    // the nearest prop vertex, drawn with the prop's texture. Synty atlases
    // map each part to one colour swatch, so a blue crate is blue inside.
    {
        std::vector<glm::vec2> uvs;
        for (const kke::ModelMesh& mesh : d->meshes) for (const kke::ModelVertex& v : mesh.vertices) uvs.push_back(v.uv);
        opts.vertexUVs.resize(vox.mesh.vertices.size());
        for (size_t v = 0; v < vox.mesh.vertices.size(); ++v) {
            float best = 1e30f;
            for (size_t i = 0; i < points.size(); ++i) {
                glm::vec3 dd = points[i] - vox.mesh.vertices[v];
                float d2 = glm::dot(dd, dd);
                if (d2 < best) { best = d2; opts.vertexUVs[v] = uvs[i]; }
            }
        }
        opts.texturePath = o.texture;
        if (opts.texturePath.empty())
            for (const kke::ModelMaterial& m : d->materials) if (!m.albedoTexture.empty()) { opts.texturePath = m.albedoTexture; break; }
        // The inside of every piece: one colour, the texture's dominant
        // colour as this prop uses it (each triangle's UV centre, weighted
        // by its area: a stone pillar with a bronze trim is stone inside),
        // a little deeper. Per-vertex swatches made pieces patchy, and dark
        // or cut-out texels read as holes.
        if (!opts.texturePath.empty()) {
            std::vector<glm::vec2> centres;
            std::vector<float> areas;
            for (size_t i = 0; i + 2 < tris.size(); i += 3) {
                const uint32_t a = tris[i], b = tris[i + 1], c = tris[i + 2];
                centres.push_back((uvs[a] + uvs[b] + uvs[c]) / 3.0f);
                areas.push_back(0.5f * glm::length(glm::cross(points[b] - points[a], points[c] - points[a])));
            }
            opts.interior = kke::interiorFillFromTexture(opts.texturePath, centres, areas);
            if (!opts.interior.valid)
                kke::log::get(name())->warn("{}: no interior colour from '{}' (unreadable or fully transparent); pieces use the surface "
                                            "texture inside",
                                            o.asset, opts.texturePath);
        }
    }
    // 3 mm up so it doesn't start inside the ground plane; the render mesh
    // follows the tets, so the drop is invisible.
    o.proxy = physics->spawnTetMeshWithOptions(vox.mesh, center + glm::vec3(0.0f, 0.003f, 0.0f), material, opts);
    if (!o.proxy) {
        m_status = "Physics is full (object limit reached) - delete something first";
        return;
    }
    // The drawn surface: every part as an unshared, subdivided triangle
    // soup (edges <= half a cell) glued triangle-by-triangle to the tets.
    std::vector<std::vector<kke::ModelVertex>> topology(d->meshes.size());
    std::vector<glm::vec3> soupPositions;
    std::vector<uint32_t> pieceOfTriangle;
    o.restNormals.clear();
    o.partOffsets.clear();
    const size_t budgetPerPart = 12000 / std::max<size_t>(1, d->meshes.size());
    for (size_t m = 0; m < d->meshes.size(); ++m) {
        const kke::ModelMesh& mesh = d->meshes[m];
        kke::TriangleSoup soup;
        for (uint32_t i : mesh.indices) {
            const kke::ModelVertex& v = mesh.vertices[i];
            soup.positions.push_back(glm::vec3(t * glm::vec4(v.position, 1.0f)) - center);
            soup.normals.push_back(glm::normalize(rot * v.normal));
            soup.uvs.push_back(v.uv);
        }
        // Triangles no longer than a cell: small enough to bend with the
        // tets. Half a cell used to be needed so whole triangles could
        // follow the pieces; the cut below does that exactly now, so a
        // breakable prop carries ~2.5x fewer triangles than before (fewer
        // points to move and upload while it's awake; OPTIMIZATION.md #31).
        // Props that only dent keep half a cell: the dents show better.
        const float maxEdge = std::max({ vox.cellSize3.x, vox.cellSize3.y, vox.cellSize3.z }) * (opts.fracture ? 1.0f : 0.5f);
        kke::subdivideSoup(soup, maxEdge, budgetPerPart);
        // Cut triangles that straddle a crack, so each piece's surface
        // ends exactly at its crack face (no lip on one side, no hole
        // into the other). Room for the cuts: +50% over the budget.
        if (opts.fracture) {
            std::vector<uint32_t> pieces = kke::splitSoupAtPieces(soup, vox.mesh, baked.cut.chunkOfTet, baked.seeds, budgetPerPart + budgetPerPart / 2);
            pieceOfTriangle.insert(pieceOfTriangle.end(), pieces.begin(), pieces.end());
        }
        o.partOffsets.push_back(soupPositions.size());
        for (size_t v = 0; v < soup.positions.size(); ++v) {
            kke::ModelVertex mv;
            mv.position = soup.positions[v] + center;
            mv.normal = soup.normals[v];
            mv.uv = soup.uvs[v];
            topology[m].push_back(mv);
        }
        soupPositions.insert(soupPositions.end(), soup.positions.begin(), soup.positions.end());
        o.restNormals.insert(o.restNormals.end(), soup.normals.begin(), soup.normals.end());
    }
    o.embedding = opts.fracture ? kke::embedTrianglesInPieces(vox.mesh, soupPositions, baked.cut.chunkOfTet, pieceOfTriangle)
                                : kke::embedTriangles(vox.mesh, soupPositions);
    m_models->setDeformedTopology(o.instance, topology);
    o.settled = false;
    const double ms = (SDL_GetPerformanceCounter() / static_cast<double>(SDL_GetPerformanceFrequency()) - start) * 1000.0;
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s: %zu cells (%.2fx%.2fx%.2f m), %zu tets, %zu pieces (%s, seed %u), %zu triangles glued, %.1f ms",
                  o.asset.c_str(), vox.solidCells, vox.cellSize3.x, vox.cellSize3.y, vox.cellSize3.z, vox.mesh.tets.size(),
                  opts.fracture ? baked.pieces : size_t(1), kke::fracturePatternName(pattern), fo.seed, soupPositions.size() / 3, ms);
    m_lastBreakStats = buf;
    m_status = o.asset + " is now " + bm.name + " - shoot it (2, then click)";
    kke::log::get(name())->info("breakable {}", m_lastBreakStats);
#else
    (void)o;
#endif
}

// Redraws breakable props from their physics tets. Skipped once an
// object is asleep and its last pose was drawn: a settled pile of
// debris costs nothing here (OPTIMIZATION.md rule 1).
void SandboxModule::updateBreakables() {
#if KKE_ENABLE_FEMFX
    auto* physics = m_app->getModule<kke::PhysicsModule>();
    if (!physics) return;
    static thread_local std::vector<glm::vec3> pos, nrm;
    static thread_local std::vector<std::vector<glm::vec3>> partPos, partNrm;
    for (Object& o : m_objects) {
        if (!o.proxy) continue;
        const bool asleep = physics->isObjectAsleep(o.proxy);
        if (asleep && o.settled) continue;
        if (!physics->deformEmbedded(o.proxy, o.embedding, o.restNormals, pos, nrm)) {
            restoreProp(o); // physics removed it (runaway guard): put the prop back
            continue;
        }
        const size_t parts = o.partOffsets.size();
        partPos.resize(parts);
        partNrm.resize(parts);
        for (size_t p = 0; p < parts; ++p) {
            size_t begin = o.partOffsets[p], end = p + 1 < parts ? o.partOffsets[p + 1] : pos.size();
            partPos[p].assign(pos.begin() + begin, pos.begin() + end);
            partNrm[p].assign(nrm.begin() + begin, nrm.begin() + end);
        }
        m_models->setDeformedVertices(o.instance, partPos, partNrm);
        o.settled = asleep;
    }
#endif
}

void SandboxModule::restoreProp(Object& o) {
#if KKE_ENABLE_FEMFX
    if (auto* physics = m_app->getModule<kke::PhysicsModule>()) physics->removeObject(o.proxy);
#endif
    o.proxy = 0;
    o.embedding = {};
    o.restNormals.clear();
    m_models->setDeformedVertices(o.instance, {}, {});
    m_models->setVisible(o.instance, true);
}

// A heavy rubber-ish ball from the camera toward the mouse cursor.
void SandboxModule::throwBall() {
#if KKE_ENABLE_FEMFX
    auto* physics = m_app->getModule<kke::PhysicsModule>();
    if (!physics) return;
    if (m_balls.size() >= kMaxBalls) {
        physics->removeObject(m_balls.front());
        m_balls.erase(m_balls.begin());
    }
    kke::Ray ray = mouseRay();
    kke::Material ball;
    ball.density = 3000.0f;        // dense: carries enough momentum to break things
    ball.stiffness = 2.0e6f;
    ball.poissonsRatio = 0.4f;
    ball.fractureStressThreshold = 1.0e9f; // the ball itself never breaks
    ball.plasticYieldThreshold = 1.0e9f;
    ball.metallic = 0.0f;
    ball.roughness = 0.6f;
    ball.textureId = 3;
    glm::vec3 start = ray.origin + ray.direction * 1.0f;
    uint32_t handle = physics->spawnFracturableTetMesh(kke::PhysicsModule::buildSphere(3, 0.22f), start, ball, ray.direction * m_ballSpeed);
    if (handle) m_balls.push_back(handle);
#endif
}

// ---------------------------------------------------------------- save / load

kke::SceneFile SandboxModule::toScene() const {
    kke::SceneFile scene;
    scene.name = m_levelName;
    scene.description = m_levelDescription;
    scene.spawn = m_spawn;
    scene.spawnYaw = m_spawnYaw;
    scene.worldSeed = m_worldSeed;
    const kke::Lighting& lighting = m_app->lighting();
    scene.hasSun = true;
    scene.sunDirection = lighting.lights[0].direction;
    scene.sunColor = lighting.lights[0].color;
    scene.sunIntensity = lighting.lights[0].intensity;
    scene.hasAmbient = true;
    scene.ambient = lighting.ambientColor;
    scene.lights = m_pointLights;
    // A floor under everything: the sandbox's ground plane is implicit,
    // a game's isn't. Centred on the scene origin (where kke::loadScene
    // puts it), 20 m past the furthest object, at least 40 x 40 m.
    float reach = std::max(std::fabs(m_spawn.x), std::fabs(m_spawn.z));
    std::set<std::string> packs;
    for (const Object& o : m_objects) {
        glm::vec3 mn, mx;
        worldBounds(o, mn, mx);
        reach = std::max({ reach, std::fabs(mn.x), std::fabs(mx.x), std::fabs(mn.z), std::fabs(mx.z) });
        kke::SceneObject so;
        so.asset = o.asset;
        so.position = o.position;
        so.yaw = o.yawDegrees;
        so.scale = glm::vec3(o.scale);
        // Characters are decoration in a level (the game spawns its own
        // player); skinned meshes have no static collision anyway.
        so.collision = o.character ? kke::SceneObject::Collision::None : o.collision;
        // File name only, so a scene works on another machine's pack folder.
        if (!o.texture.empty()) so.texture = std::filesystem::path(o.texture).filename().string();
        if (o.proxy && o.breakMaterial >= 0) so.breakable = kBreakSceneNames[o.breakMaterial];
        so.fractureSeed = o.fractureSeed;
        scene.objects.push_back(std::move(so));
        packs.insert(o.pack);
    }
    // Pack preference: the loaded scene's order first, then the others.
    scene.packs = m_scenePacks;
    for (const std::string& p : packs) {
        const bool listed = std::any_of(scene.packs.begin(), scene.packs.end(), [&](const std::string& s) {
            const kke::CatalogPack* cp = m_catalog.pack(p);
            return s == p || (cp && std::filesystem::path(cp->root).filename().string() == s); // listed by folder name
        });
        if (!listed) scene.packs.push_back(p);
    }
    // Name the pack per object only where the list would pick another
    // pack's same-named model.
    for (size_t i = 0; i < scene.objects.size(); ++i) {
        const kke::CatalogAsset* picked = m_catalog.find(m_objects[i].asset, scene.packs);
        if (picked && picked->pack != m_objects[i].pack) scene.objects[i].pack = m_objects[i].pack;
    }
    const float size = std::max(40.0f, 2.0f * (std::ceil(reach) + 20.0f));
    scene.groundSize = glm::vec2(size);
    scene.groundColor = glm::vec3(0.36f, 0.38f, 0.36f);
    return scene;
}

bool SandboxModule::saveLayout(const std::string& path) {
    try {
        const kke::SceneFile scene = toScene();
        scene.save(path);
        m_status = "Saved " + std::to_string(m_objects.size()) + " objects to " + path;
        kke::log::get(name())->info("{} (assets: {})", m_status, scene.assetsUsed().size());
        return true;
    } catch (const std::exception& e) {
        m_status = e.what();
        kke::log::get(name())->error("save failed: {}", e.what());
        return false;
    }
}

void SandboxModule::fromScene(const kke::SceneFile& scene) {
    clearAll();
    clearSelection();
    m_undo.clear();
    m_redo.clear();
    std::snprintf(m_levelName, sizeof(m_levelName), "%s", scene.name.empty() ? "Sandbox level" : scene.name.c_str());
    m_levelDescription = scene.description.empty() ? "Built in the sandbox" : scene.description;
    m_spawn = scene.spawn;
    m_spawnYaw = scene.spawnYaw;
    if (scene.worldSeed) m_worldSeed = scene.worldSeed;
    kke::Lighting& lighting = m_app->lighting();
    if (scene.hasSun) {
        lighting.lights[0].direction = scene.sunDirection;
        lighting.lights[0].color = scene.sunColor;
        lighting.lights[0].intensity = scene.sunIntensity;
    }
    if (scene.hasAmbient) lighting.ambientColor = scene.ambient;
    m_pointLights = scene.lights;
    if (m_pointLights.size() > 2) {
        kke::log::get(name())->info("scene has {} point lights; the sandbox shows the first 2 (4 light slots, 2 for sun and sky), all are kept",
                                    m_pointLights.size());
    }
    m_selectedLight = -1;
    m_scenePacks = scene.packs;
    size_t missing = 0, gridsExpanded = 0;
    for (const kke::SceneObject& so : scene.objects) {
        // The sandbox edits single objects: a grid becomes its cells.
        const glm::mat4 rot = glm::rotate(glm::mat4(1.0f), glm::radians(so.yaw), glm::vec3(0, 1, 0));
        if (so.gridCount != glm::ivec2(1, 1)) ++gridsExpanded;
        if (so.pivot || so.scale.x != so.scale.y || so.scale.y != so.scale.z)
            kke::log::get(name())->info("'{}': pivot placement / non-uniform scale is kept as bounds placement / uniform scale here", so.asset);
        for (int gz = 0; gz < so.gridCount.y; ++gz)
            for (int gx = 0; gx < so.gridCount.x; ++gx) {
                const glm::vec3 step = glm::vec3(rot * glm::vec4(gx * so.gridStep.x, 0.0f, gz * so.gridStep.y, 0.0f));
                Object* o = spawnObject(so.asset, so.position + step, so.yaw, 0, so.pack);
                if (!o) { ++missing; continue; }
                o->scale = so.scale.x;
                o->collision = so.collision;
                o->fractureSeed = so.fractureSeed;
                if (const kke::CatalogPack* pack = packOf(so.asset, so.pack); pack && !so.texture.empty()) {
                    for (const std::string& v : pack->textureVariants)
                        if (std::filesystem::path(v).filename() == so.texture) { o->texture = v; m_models->setTextureOverride(o->instance, v); }
                }
                applyTransform(*o);
                // Saved breakable: make it breakable again with the same
                // material (and seeds, so the same pieces).
                const int material = so.breakable.empty() ? -1 : breakMaterialFromName(so.breakable);
                if (material >= 0 && m_hasFemfx) {
                    const int keep = m_breakMaterial, keepPattern = m_patternOverride;
                    m_breakMaterial = material;
                    m_patternOverride = 0;
                    makeBreakable(*o);
                    m_breakMaterial = keep;
                    m_patternOverride = keepPattern;
                }
            }
    }
    m_status = "Loaded " + std::to_string(m_objects.size()) + " objects";
    if (gridsExpanded) m_status += " (" + std::to_string(gridsExpanded) + " grids split into single objects)";
    if (missing) m_status += " (" + std::to_string(missing) + " not found in these packs)";
}

// Reads a kke.scene, or a layout saved by an older sandbox
// ("kke-sandbox-layout": objects with asset/position/yaw/texture/
// fractureSeed/breakable, which is converted).
bool SandboxModule::loadLayout(const std::string& path) {
    std::ifstream f(path);
    if (!f) { m_status = "Could not open " + path; kke::log::get(name())->error("{}", m_status); return false; }
    std::stringstream text;
    text << f.rdbuf();
    try {
        nlohmann::json j = nlohmann::json::parse(text.str(), nullptr, true, /*ignore_comments=*/true);
        kke::SceneFile scene;
        if (j.value("format", std::string()) == "kke-sandbox-layout") {
            scene.worldSeed = j.value("worldSeed", 0u);
            if (!j.contains("objects") || !j["objects"].is_array()) throw std::runtime_error(path + ": no \"objects\" array");
            for (const auto& e : j["objects"]) {
                kke::SceneObject so;
                so.asset = e.value("asset", "");
                auto p = e.value("position", std::vector<float>{ 0, 0, 0 });
                if (so.asset.empty() || p.size() != 3) continue;
                so.position = glm::vec3(p[0], p[1], p[2]);
                so.yaw = e.value("yaw", 0.0f);
                so.texture = e.value("texture", "");
                so.fractureSeed = e.value("fractureSeed", 0u);
                const int m = breakMaterialFromName(e.value("breakable", ""));
                if (m >= 0) so.breakable = kBreakSceneNames[m];
                scene.objects.push_back(std::move(so));
            }
            scene.spawn = m_spawn;
            scene.spawnYaw = m_spawnYaw;
        } else {
            scene = kke::SceneFile::parse(text.str(), path);
        }
        fromScene(scene);
    } catch (const std::exception& e) {
        m_status = e.what();
        kke::log::get(name())->error("load failed: {}", e.what());
        return false;
    }
    m_status += " from " + path;
    kke::log::get(name())->info("{}", m_status);
    return true;
}

// ---------------------------------------------------------------- UI

void SandboxModule::folderNotFoundUi() {
    ImGui::TextWrapped("No asset packs found. Extract your packs (any layout, e.g. POLYGON_Town/{Characters,FBX,Textures}) "
                       "into assets/synty/, set KKE_ASSETS_DIR, or type a folder below. Assets are never committed to git: "
                       "they're licensed per user.");
    ImGui::InputText("##folder", m_folderInput, sizeof(m_folderInput));
    ImGui::SameLine();
    if (ImGui::Button("Use this folder")) openAssetFolder(m_folderInput);
    if (ImGui::TreeNode("Places searched")) {
        for (const std::string& p : m_searched) ImGui::BulletText("%s", p.c_str());
        ImGui::TreePop();
    }
}

void SandboxModule::assetBrowserUi() {
    const float s = ImGui::GetFontSize() / 13.0f; // scale fixed sizes with the UI font
    ImGui::SetNextWindowPos(ImVec2(10 * s, 10 * s), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(330 * s, 520 * s), ImGuiCond_FirstUseEver);
    ImGui::Begin("Assets");
    if (m_assetFolder.empty()) {
        folderNotFoundUi();
        if (!m_status.empty()) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "%s", m_status.c_str());
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("%s", m_status.c_str());
    auto combo = [&](const char* label, std::string& value, const std::vector<std::string>& options) {
        if (ImGui::BeginCombo(label, value.empty() ? "All" : value.c_str())) {
            if (ImGui::Selectable("All", value.empty())) { value.clear(); m_filterDirty = true; }
            for (const std::string& o : options) {
                if (ImGui::Selectable(o.c_str(), value == o)) { value = o; m_filterDirty = true; }
            }
            ImGui::EndCombo();
        }
    };
    std::vector<std::string> packNames;
    for (const kke::CatalogPack& p : m_catalog.packs) packNames.push_back(p.name);
    combo("Pack", m_filterPack, packNames);
    combo("Category", m_filterCategory, m_catalog.categories());
    if (ImGui::InputTextWithHint("##search", "Search...", m_search, sizeof(m_search))) m_filterDirty = true;
    if (m_filterDirty) {
        m_filtered = m_catalog.filter(m_filterPack, m_filterCategory, m_search);
        m_filterDirty = false;
    }
    ImGui::Text("%zu assets - click one, then click in the world", m_filtered.size());
    ImGui::BeginChild("list", ImVec2(0, 0), ImGuiChildFlags_Borders);
    // Clipper: only the visible rows are submitted, so a 3,000-asset
    // catalog costs the same as a 30-asset one.
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(m_filtered.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const kke::CatalogAsset* a = m_filtered[i];
            ImGui::PushID(i);
            bool active = m_tool == Tool::Place && !m_movingId && m_placeAsset == a->name && m_placePack == a->pack;
            if (ImGui::Selectable(a->name.c_str(), active)) beginPlacing(a->name, m_placeYaw, 0, a->pack);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s / %s", a->path.c_str(), a->pack.c_str(), a->category.c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

void SandboxModule::inspectorUi() {
    const float s = ImGui::GetFontSize() / 13.0f;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 340 * s, 10 * s), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(330 * s, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Sandbox");
    ImGui::Text("%zu objects, %zu draw calls (%zu culled), %.0f FPS", m_objects.size(), m_models->drawCallsLastFrame(), m_models->culledLastFrame(), io.Framerate);
    if (ImGui::Checkbox("Engine panels (F1)", &m_showEnginePanels))
        for (kke::Module* m : m_enginePanels) m->setUiVisible(m_showEnginePanels);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Camera: right-drag orbit, middle-drag pan, wheel zoom, WASD/QE move");
    ImGui::PopStyleColor();
    int tool = m_tool == Tool::Shoot ? 2 : (m_tool == Tool::Place ? 1 : 0);
    if (ImGui::RadioButton("Select (1)", tool == 0)) { cancelPlacing(); m_tool = Tool::Select; }
    ImGui::SameLine();
    ImGui::BeginDisabled(true);
    ImGui::RadioButton("Place", tool == 1);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_hasFemfx);
    if (ImGui::RadioButton("Shoot (2)", tool == 2)) { cancelPlacing(); m_tool = Tool::Shoot; }
    ImGui::EndDisabled();
    if (m_tool == Tool::Shoot) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0.35f, 1), "Click: fire a ball at the cursor   Esc: stop");
    } else if (m_tool == Tool::Place) {
        ImGui::TextColored(ImVec4(0.55f, 1, 0.55f, 1), m_movingId ? "Moving: %s" : "Placing: %s", m_placeAsset.c_str());
        ImGui::TextWrapped("Click: place (Shift+click: keep placing)   R / Ctrl+wheel: rotate   Esc: stop");
    } else {
        ImGui::TextWrapped("Click: select (Shift: add)   drag the gizmo   Tab: move/rotate/scale gizmo   G: move   R: rotate   "
                           "Ctrl+D: duplicate   Del: delete   Ctrl+A: all   Ctrl+Z / Ctrl+Y: undo / redo");
        int g = static_cast<int>(m_gizmo);
        ImGui::RadioButton("Move", &g, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Rotate", &g, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Scale", &g, 2);
        m_gizmo = static_cast<Gizmo>(g);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hold Shift while dragging for no snapping");
    }
    ImGui::BeginDisabled(m_undo.empty());
    if (ImGui::Button("Undo")) undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_redo.empty());
    if (ImGui::Button("Redo")) redo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%zu / %zu steps", m_undo.size(), m_redo.size());
    const char* snaps[] = { "Off", "0.25 m", "0.5 m", "1 m", "2.5 m", "5 m" };
    const float snapValues[] = { 0.0f, 0.25f, 0.5f, 1.0f, 2.5f, 5.0f };
    int snapIndex = 0;
    for (int i = 0; i < 6; ++i) if (std::fabs(m_snap - snapValues[i]) < 1e-4f) snapIndex = i;
    if (ImGui::Combo("Grid snap", &snapIndex, snaps, 6)) m_snap = snapValues[snapIndex];
    ImGui::SliderFloat("Rotate step", &m_rotateStep, 5.0f, 90.0f, "%.0f deg");

    ImGui::SeparatorText("Selected");
    if (m_selection.size() > 1) {
        ImGui::Text("%zu objects (the brighter box is the one shown below)", m_selection.size());
        if (ImGui::Button("Delete all selected")) deleteSelection();
    }
    if (Object* o = find(m_selected)) {
        ImGui::Text("%s", o->asset.c_str());
        ImGui::TextDisabled("at (%.2f, %.2f, %.2f), %.0f deg, x%.2f", o->position.x, o->position.y, o->position.z, o->yawDegrees, o->scale);
        if (!o->proxy && !o->ragdoll) {
            // Typed edits: one undo step per field edit, taken when it starts.
            glm::vec3 pos = o->position;
            float yaw = o->yawDegrees, scale = o->scale;
            bool changed = ImGui::DragFloat3("Position", &pos.x, 0.05f);
            if (ImGui::IsItemActivated()) pushUndo();
            changed |= ImGui::DragFloat("Yaw", &yaw, 1.0f, -360.0f, 360.0f, "%.0f deg");
            if (ImGui::IsItemActivated()) pushUndo();
            changed |= ImGui::DragFloat("Scale", &scale, 0.01f, 0.05f, 20.0f, "x%.2f");
            if (ImGui::IsItemActivated()) pushUndo();
            if (changed) {
                o->position = glm::vec3(pos.x, std::max(pos.y, 0.0f), pos.z);
                o->yawDegrees = yaw;
                o->scale = std::clamp(scale, 0.05f, 20.0f);
                applyTransform(*o);
            }
        }
        if (!o->character) {
            const char* collisions[] = { "Mesh (exact)", "Box (bounds, cheap)", "None" };
            int c = static_cast<int>(o->collision);
            if (ImGui::Combo("Collision", &c, collisions, 3)) {
                pushUndo();
                for (Object* s : selectedObjects()) if (!s->character) s->collision = static_cast<kke::SceneObject::Collision>(c);
                o->collision = static_cast<kke::SceneObject::Collision>(c);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("How the games that load this level collide with it (Jolt):\nmesh for buildings, floors and stairs; box for props;\n"
                                  "none for grass and decoration. Applies to every selected object.");
        }
        if (const kke::ModelData* d = m_models->model(o->model)) {
            glm::vec3 size = d->boundsMax - d->boundsMin;
            ImGui::TextDisabled("%.2f x %.2f x %.2f m, %zu tris, %zu bones", size.x, size.y, size.z, d->triangleCount(), d->bones.size());
        }
        if (const kke::CatalogPack* pack = packOf(o->asset, o->pack); pack && pack->textureVariants.size() > 1) {
            std::string current = o->texture.empty() ? "Model's own" : std::filesystem::path(o->texture).stem().string();
            if (ImGui::BeginCombo("Texture", current.c_str())) {
                if (ImGui::Selectable("Model's own", o->texture.empty())) { pushUndo(); o->texture.clear(); m_models->setTextureOverride(o->instance, ""); }
                for (const std::string& v : pack->textureVariants) {
                    if (ImGui::Selectable(std::filesystem::path(v).stem().string().c_str(), o->texture == v)) {
                        pushUndo();
                        o->texture = v;
                        m_models->setTextureOverride(o->instance, v);
                    }
                }
                ImGui::EndCombo();
            }
        }
        if (ImGui::Button("Move (G)")) beginPlacing(o->asset, o->yawDegrees, o->id, o->pack);
        ImGui::SameLine();
        if (ImGui::Button("Duplicate")) duplicateSelection();
        ImGui::SameLine();
        if (ImGui::Button("Delete")) deleteSelection();
        o = find(m_selected);
        if (o && o->character) {
            if (!m_ragdolls) ImGui::TextDisabled("Ragdolls need a physics module (FEMFX build)");
            else if (ImGui::Button(o->ragdoll ? "Stand up (K)" : "Ragdoll (K)")) {
                SDL_Event e{};
                e.type = SDL_EVENT_KEY_DOWN;
                e.key.key = SDLK_K;
                onEvent(e);
            }
        } else if (o) {
            if (!m_hasFemfx) ImGui::TextDisabled("Breakable props need FEMFX (-DKKE_ENABLE_FEMFX=ON)");
            else if (o->proxy) {
                if (ImGui::Button("Restore prop (X)")) { pushUndo(); restoreProp(*o); }
            } else if (ImGui::Button("Make breakable (X)")) {
                pushUndo();
                makeBreakable(*o);
            }
            if (m_hasFemfx) ImGui::TextDisabled("Breakable props are saved as breakable: games make them breakable too");
            if (m_hasFemfx) {
                // The object's own fracture seed: same seed, same pieces.
                ImGui::Text("Fracture seed %u", o->fractureSeed ? o->fractureSeed : o->id);
                ImGui::SameLine();
                if (ImGui::SmallButton("Reroll")) {
                    o->fractureSeed = (o->fractureSeed ? o->fractureSeed : o->id) * 747796405u + 2891336453u;
                    if (!o->fractureSeed) o->fractureSeed = 1;
                    if (o->proxy) { restoreProp(*o); makeBreakable(*o); } // re-bake with the new pieces
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("New pieces for this object. Mixed with the world seed below;\nsaved with the layout.");
            }
        }
    } else {
        ImGui::TextDisabled("nothing - click an object");
    }

    if (m_hasFemfx) {
        ImGui::SeparatorText("Physics toys");
        const char* names[kBreakMaterialCount];
        for (int i = 0; i < kBreakMaterialCount; ++i) names[i] = kBreakMaterials[i].name;
        ImGui::Combo("Breaks as", &m_breakMaterial, names, kBreakMaterialCount);
        const char* patterns[] = { "Material's own", "Shards", "Voronoi chunks", "Splinters", "Radial (glass)", "Solid (bends only)" };
        ImGui::Combo("Pattern", &m_patternOverride, patterns, 6);
        ImGui::SliderFloat("Chunk size", &m_chunkScale, 0.4f, 3.0f, "x%.1f");
        ImGui::SliderInt("Detail (cells)", &m_detailCells, 30, 400);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Voxel budget per prop, 6 tetrahedra per cell.\nMore = closer shape and cleaner piece edges, more CPU.\n~160 is fine on one core while it's moving.");
        ImGui::SliderFloat("Toughness", &m_toughness, 0.2f, 5.0f, "x%.1f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Multiplies the material's fracture strength.\nProps arm 0.75-2 s after spawning, once settled:\nonly stress added by a hit can break them.");
        {
            int seed = static_cast<int>(m_worldSeed);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
            if (ImGui::InputInt("World seed", &seed)) m_worldSeed = static_cast<uint32_t>(std::max(seed, 0));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mixed with each object's own seed: a new world seed = every prop\nbreaks differently; the same seeds = the same breaks every time\n(what a multiplayer game sends instead of the debris).");
        }
        if (!m_lastBreakStats.empty()) ImGui::TextDisabled("%s", m_lastBreakStats.c_str());
        if (ImGui::Button("Restore all props")) for (Object& o : m_objects) if (o.proxy) restoreProp(o);
        ImGui::SliderFloat("Ball speed", &m_ballSpeed, 5.0f, 40.0f, "%.0f m/s");
        ImGui::TextWrapped("Shoot tool (2) or F / Space: throw a ball at the cursor (max %zu, oldest removed)", kMaxBalls);
    }

    lookUi();

    lightsUi();

    ImGui::SeparatorText("Level");
    ImGui::InputText("Name", m_levelName, sizeof(m_levelName));
    ImGui::InputText("File", m_layoutPath, sizeof(m_layoutPath));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("A kke.scene file. Saved in scenes/, kke_demo lists it in its Scenes panel\n(walk it with collision). Older sandbox layouts load too.");
    if (ImGui::Button("Save (Ctrl+S)")) saveLayout(m_layoutPath);
    ImGui::SameLine();
    if (ImGui::Button("Load (Ctrl+L)")) loadLayout(m_layoutPath);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) { pushUndo(); clearAll(); clearSelection(); }
    if (!m_status.empty()) ImGui::TextWrapped("%s", m_status.c_str());
    ImGui::End();
}

void SandboxModule::lookUi() {
    // Variants/overlays of the first pack that has them.
    const kke::CatalogPack* pack = nullptr;
    for (const kke::CatalogPack& p : m_catalog.packs) if (!p.textureVariants.empty() || !p.overlayTextures.empty()) { pack = &p; break; }
    if (!pack) return;
    ImGui::SeparatorText("Look");
    if (pack->textureVariants.size() > 1) {
        auto label = [&](int i) { return std::filesystem::path(pack->textureVariants[i]).stem().string(); };
        m_variant = std::clamp(m_variant, 0, static_cast<int>(pack->textureVariants.size()) - 1);
        if (ImGui::BeginCombo("New objects", m_variant == 0 ? "Model's own" : label(m_variant).c_str())) {
            for (int i = 0; i < static_cast<int>(pack->textureVariants.size()); ++i) {
                if (ImGui::Selectable(i == 0 ? "Model's own" : label(i).c_str(), m_variant == i)) {
                    m_variant = i;
                    if (m_ghost && !m_movingId) m_models->setTextureOverride(m_ghost, i ? pack->textureVariants[i] : "");
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("Apply to all objects")) {
            pushUndo();
            for (Object& o : m_objects) {
                const kke::CatalogPack* op = packOf(o.asset, o.pack);
                if (op != pack) continue;
                o.texture = m_variant ? pack->textureVariants[m_variant] : "";
                m_models->setTextureOverride(o.instance, o.texture);
            }
        }
    }
    if (!pack->overlayTextures.empty()) {
        bool changed = false;
        std::string current = m_overlay == 0 ? "None" : std::filesystem::path(pack->overlayTextures[m_overlay - 1]).stem().string();
        if (ImGui::BeginCombo("World grid", current.c_str())) {
            if (ImGui::Selectable("None", m_overlay == 0)) { m_overlay = 0; changed = true; }
            for (int i = 0; i < static_cast<int>(pack->overlayTextures.size()); ++i) {
                if (ImGui::Selectable(std::filesystem::path(pack->overlayTextures[i]).stem().string().c_str(), m_overlay == i + 1)) {
                    m_overlay = i + 1;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        changed |= ImGui::SliderFloat("Grid tile", &m_overlayTile, 0.5f, 8.0f, "%.1f m");
        changed |= ImGui::SliderFloat("Grid strength", &m_overlayStrength, 0.0f, 1.0f);
        if (changed) applyLook();
    }
}

// Where the player starts and the level's lights (saved with the scene).
void SandboxModule::lightsUi() {
    ImGui::SeparatorText("Spawn and lights");
    if (ImGui::Button("Spawn here")) {
        const glm::vec3 t = m_app->camera().target;
        m_spawn = glm::vec3(t.x, 0.0f, t.z);
        const glm::vec3 d = t - m_app->camera().position;
        m_spawnYaw = glm::degrees(std::atan2(d.x, -d.z)); // face the way the camera looks
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Player start at the camera's focus point, facing the way the camera looks (cyan box)");
    ImGui::SameLine();
    ImGui::TextDisabled("(%.1f, %.1f, %.1f) %.0f deg", m_spawn.x, m_spawn.y, m_spawn.z, m_spawnYaw);
    kke::Lighting& lighting = m_app->lighting();
    glm::vec3 dir = lighting.lights[0].direction;
    float azimuth = glm::degrees(std::atan2(-dir.x, -dir.z)), elevation = glm::degrees(std::asin(std::clamp(-dir.y, -1.0f, 1.0f)));
    bool sun = ImGui::SliderFloat("Sun direction", &azimuth, -180.0f, 180.0f, "%.0f deg");
    sun |= ImGui::SliderFloat("Sun height", &elevation, 5.0f, 90.0f, "%.0f deg");
    if (sun) {
        const float az = glm::radians(azimuth), el = glm::radians(elevation);
        lighting.lights[0].direction = -glm::normalize(glm::vec3(std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az)));
    }
    ImGui::ColorEdit3("Sun colour", &lighting.lights[0].color.x, ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
    ImGui::SliderFloat("##sunI", &lighting.lights[0].intensity, 0.0f, 3.0f, "%.2f");
    ImGui::ColorEdit3("Ambient", &lighting.ambientColor.x, ImGuiColorEditFlags_NoInputs);
    for (int i = 0; i < static_cast<int>(m_pointLights.size()); ++i) {
        ImGui::PushID(i);
        kke::SceneLight& l = m_pointLights[i];
        if (ImGui::Selectable(("Light " + std::to_string(i + 1)).c_str(), m_selectedLight == i, 0, ImVec2(ImGui::GetFontSize() * 4.0f, 0))) m_selectedLight = i;
        ImGui::SameLine();
        ImGui::ColorEdit3("##c", &l.color.x, ImGuiColorEditFlags_NoInputs);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.0f);
        ImGui::SliderFloat("##i", &l.intensity, 0.0f, 10.0f, "%.1f");
        ImGui::SameLine();
        if (ImGui::SmallButton("here")) l.position = m_app->camera().target + glm::vec3(0.0f, 2.5f, 0.0f);
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            m_pointLights.erase(m_pointLights.begin() + i);
            m_selectedLight = -1;
            ImGui::PopID();
            break;
        }
        if (i == m_selectedLight) ImGui::DragFloat3("Position", &l.position.x, 0.05f);
        ImGui::PopID();
    }
    if (m_pointLights.size() < 2 && ImGui::Button("Add point light")) {
        kke::SceneLight l;
        l.position = m_app->camera().target + glm::vec3(0.0f, 2.5f, 0.0f);
        l.color = glm::vec3(1.0f, 0.8f, 0.55f);
        l.intensity = 2.0f;
        m_pointLights.push_back(l);
        m_selectedLight = static_cast<int>(m_pointLights.size()) - 1;
    }
}

void SandboxModule::renderUi() {
    assetBrowserUi();
    inspectorUi();
}

} // namespace kke_sandbox
