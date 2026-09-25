#include "SandboxModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/Material.h"
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
#include <typeindex>

namespace kke_sandbox {

namespace {

// What "Make breakable" turns a prop into. Values are the measured
// presets from MaterialGridModule (see its comment and BUGS.md BUG-020
// for how the fracture thresholds were found); textureId picks
// PhysicsModule's matching procedural texture.
struct BreakMaterial { const char* name; kke::Material material; bool plastic; };
const BreakMaterial kBreakMaterials[] = {
    { "Wood (splinters)", []{ kke::Material m; m.density=600.0f;  m.stiffness=1.0e7f; m.poissonsRatio=0.30f; m.fractureStressThreshold=8000.0f; m.plasticYieldThreshold=6000.0f; m.plasticCreep=0.3f; m.metallic=0.0f; m.roughness=0.75f; m.textureId=0; return m; }(), false },
    { "Stone (crumbles)", []{ kke::Material m; m.density=2500.0f; m.stiffness=3.0e7f; m.poissonsRatio=0.25f; m.fractureStressThreshold=4000.0f; m.plasticYieldThreshold=3000.0f; m.plasticCreep=0.1f; m.metallic=0.0f; m.roughness=0.9f; m.textureId=1; return m; }(), false },
    { "Glass (shatters)", []{ kke::Material m; m.density=2500.0f; m.stiffness=7.0e7f; m.poissonsRatio=0.22f; m.fractureStressThreshold=2000.0f; m.plasticYieldThreshold=1800.0f; m.plasticCreep=0.02f; m.metallic=0.0f; m.roughness=0.05f; m.textureId=4; return m; }(), false },
    // Metal dents instead of breaking: FEMFX plasticity, no fracture.
    { "Metal (dents)",    []{ kke::Material m; m.density=7870.0f; m.stiffness=2.0e7f; m.poissonsRatio=0.30f; m.fractureStressThreshold=1.0e9f; m.plasticYieldThreshold=2000.0f; m.plasticCreep=0.5f; m.metallic=0.9f; m.roughness=0.35f; m.textureId=2; return m; }(), true },
};
constexpr int kBreakMaterialCount = static_cast<int>(sizeof(kBreakMaterials) / sizeof(kBreakMaterials[0]));

// Budgets (OPTIMIZATION.md rule 5: everything that can pile up gets a cap
// and a policy). Past the cap the oldest ball is removed — a thrown ball
// that's been lying around is the least interesting object in the scene.
constexpr size_t kMaxBalls = 6;
// Breakable proxies: at most this many cells (6 tets each) per prop, so
// one big prop can't cost more than a Glass Sheet (~36 cells) on min-spec.
constexpr int kMaxProxyCells = 48;
constexpr float kProxyCellSize = 0.3f; // metres; smaller = more, finer pieces

const glm::vec3 kSelectColor(1.0f, 0.75f, 0.1f);
const glm::vec3 kHoverColor(0.55f, 0.8f, 1.0f);
const glm::vec3 kGhostColor(0.55f, 1.0f, 0.55f); // placement outline

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
    else kke::log::get(name())->warn("no asset folder found - the Assets panel lists where it looked and takes a path");
    // Optional: KKE_SANDBOX_LAYOUT=file.json loads a layout at startup
    // (used by the automated screenshot tests, handy for sharing scenes).
    if (const char* layout = std::getenv("KKE_SANDBOX_LAYOUT")) {
        std::snprintf(m_layoutPath, sizeof(m_layoutPath), "%s", layout);
        loadLayout(layout);
    }
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

const kke::CatalogPack* SandboxModule::packOf(const std::string& asset) const {
    const kke::CatalogAsset* a = m_catalog.find(asset);
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

kke::ModelModule::ModelId SandboxModule::loadAsset(const std::string& assetName) {
    const kke::CatalogAsset* asset = m_catalog.find(assetName);
    if (!asset) return 0;
    const kke::CatalogPack* pack = m_catalog.pack(asset->pack);
    kke::ModelLoadOptions opts;
    if (pack) {
        opts.textureSearchPaths = pack->textureDirs;
        opts.fallbackTexture = pack->defaultTexture;
    }
    // Synchronous: a Synty FBX loads in a few ms and ModelModule caches
    // it. Streaming/time-sliced loading is in OPTIMIZATION.md's backlog.
    return m_models->load(asset->path, opts);
}

// Synty pivots differ per piece (corner for walls, center for props), so
// objects are positioned by their bounds: `position` is the bottom-center.
glm::mat4 SandboxModule::objectTransform(const kke::ModelData& d, const glm::vec3& position, float yawDegrees) const {
    glm::vec3 center = (d.boundsMin + d.boundsMax) * 0.5f;
    glm::mat4 t = glm::translate(glm::mat4(1.0f), position);
    t = glm::rotate(t, glm::radians(yawDegrees), glm::vec3(0, 1, 0));
    return glm::translate(t, glm::vec3(-center.x, -d.boundsMin.y, -center.z));
}

SandboxModule::Object* SandboxModule::spawnObject(const std::string& asset, const glm::vec3& position, float yawDegrees) {
    kke::ModelModule::ModelId model = loadAsset(asset);
    const kke::ModelData* d = model ? m_models->model(model) : nullptr;
    if (!d) {
        m_status = "Could not load '" + asset + "'";
        return nullptr;
    }
    Object o;
    o.id = m_nextId++;
    o.asset = asset;
    o.position = position;
    o.yawDegrees = yawDegrees;
    o.model = model;
    o.instance = m_models->spawn(model, objectTransform(*d, position, yawDegrees));
    o.character = !d->bones.empty() && d->meshes.size() > 0 && m_catalog.find(asset) && m_catalog.find(asset)->skinned;
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
    if (m_selected == id) m_selected = 0;
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
    kke::transformAabb(d->boundsMin, d->boundsMax, objectTransform(*d, o.position, o.yawDegrees), mn, mx);
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

// ---------------------------------------------------------------- placing

void SandboxModule::beginPlacing(const std::string& asset, float yawDegrees, uint32_t movingId) {
    cancelPlacing();
    kke::ModelModule::ModelId model = loadAsset(asset);
    if (!model) { m_status = "Could not load '" + asset + "'"; return; }
    m_tool = Tool::Place;
    m_placeAsset = asset;
    m_placeYaw = yawDegrees;
    m_movingId = movingId;
    if (Object* moving = find(movingId)) {
        // Move the real object instead of spawning a ghost.
        m_ghost = moving->instance;
    } else {
        // The preview is the real model with its real texture; the green
        // outline (drawn in update()) marks it as not placed yet.
        m_ghost = m_models->spawn(model);
        if (const kke::CatalogPack* pack = packOf(asset); pack && m_variant > 0 && m_variant < static_cast<int>(pack->textureVariants.size()))
            m_models->setTextureOverride(m_ghost, pack->textureVariants[m_variant]);
    }
    m_ghostModel = model;
    m_ghostValid = false;
}

void SandboxModule::cancelPlacing() {
    if (m_tool != Tool::Place) return;
    if (Object* moving = find(m_movingId)) {
        // Put a moved object back where it was.
        if (const kke::ModelData* d = m_models->model(moving->model))
            m_models->setTransform(moving->instance, objectTransform(*d, moving->position, moving->yawDegrees));
        m_models->setVisible(moving->instance, true);
    } else if (m_ghost) {
        m_models->remove(m_ghost);
    }
    m_ghost = 0;
    m_movingId = 0;
    m_tool = Tool::Select;
}

void SandboxModule::commitPlacement(bool keepPlacing) {
    if (!m_ghostValid) return;
    if (Object* moving = find(m_movingId)) {
        moving->position = m_ghostPos;
        moving->yawDegrees = m_placeYaw;
        m_selected = moving->id;
        m_ghost = 0;
        m_movingId = 0;
        m_tool = Tool::Select;
        return;
    }
    if (Object* o = spawnObject(m_placeAsset, m_ghostPos, m_placeYaw)) {
        m_selected = o->id;
        o->texture = m_models->textureOverride(m_ghost);
        m_models->setTextureOverride(o->instance, o->texture);
    }
    if (!keepPlacing) cancelPlacing();
}

// ---------------------------------------------------------------- frame

void SandboxModule::update(const kke::UpdateContext&) {
    bool mouseFree = !ImGui::GetIO().WantCaptureMouse && !m_app->uiCapturesMouse();

    // Ground grid around the camera target, snapped so it doesn't swim.
    glm::vec3 target = m_app->camera().target;
    float gridStep = m_snap > 0.0f ? std::max(m_snap, 0.5f) : 1.0f;
    glm::vec3 gridCenter(kke::snapTo(target.x, gridStep * 2), 0.002f, kke::snapTo(target.z, gridStep * 2));
    m_debug->gridXZ(gridCenter, 12.0f, gridStep, glm::vec3(0.32f, 0.34f, 0.4f), 0.012f);

    // Ragdolls drive their characters' skeletons.
    std::vector<glm::mat4> bodies;
    for (Object& o : m_objects) {
        if (o.ragdoll && m_ragdolls && m_ragdolls->ragdollBodyTransforms(o.ragdoll, bodies)) {
            const kke::ModelData* d = m_models->model(o.model);
            glm::mat4 worldToModel = glm::inverse(m_models->transform(o.instance));
            m_models->setBoneWorldOverride(o.instance, kke::poseFromRagdoll(*d, o.binding, bodies, worldToModel));
        }
    }

    if (m_tool == Tool::Place) {
        m_ghostValid = mouseFree && placementPoint(m_ghostPos, m_movingId);
        m_models->setVisible(m_ghost, m_ghostValid);
        if (m_ghostValid) {
            if (const kke::ModelData* d = m_models->model(m_ghostModel)) {
                glm::mat4 t = objectTransform(*d, m_ghostPos, m_placeYaw);
                m_models->setTransform(m_ghost, t);
                m_debug->box(t, d->boundsMin, d->boundsMax, kGhostColor, 0.02f, true);
            }
            m_debug->cross(m_ghostPos, 0.25f, kGhostColor);
        }
        m_hovered = 0;
    } else if (m_tool == Tool::Shoot) {
        m_hovered = 0;
        // Aim marker where the ball is heading (first hit: ground or object).
        glm::vec3 aim;
        if (mouseFree && placementPoint(aim, 0)) m_debug->cross(aim, 0.3f, glm::vec3(1.0f, 0.35f, 0.25f));
    } else {
        m_hovered = mouseFree ? pickObject() : 0;
    }

    for (const Object& o : m_objects) {
        if (o.id != m_selected && o.id != m_hovered) continue;
        if (o.proxy || o.ragdoll) continue;
        const kke::ModelData* d = m_models->model(o.model);
        if (!d) continue;
        bool selected = o.id == m_selected;
        m_debug->box(objectTransform(*d, o.position, o.yawDegrees), d->boundsMin, d->boundsMax,
                     selected ? kSelectColor : kHoverColor, selected ? 0.03f : 0.015f, true);
    }
}

void SandboxModule::onEvent(const SDL_Event& event) {
    ImGuiIO& io = ImGui::GetIO();
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
        if (io.WantCaptureMouse || m_app->uiCapturesMouse()) return;
        bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
        if (m_tool == Tool::Place) commitPlacement(/*keepPlacing=*/shift);
        else if (m_tool == Tool::Shoot) throwBall();
        else m_selected = pickObject();
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
        else m_selected = 0;
        break;
    case SDLK_1:
        cancelPlacing();
        m_tool = Tool::Select;
        break;
    case SDLK_2:
        cancelPlacing();
        if (m_hasFemfx) m_tool = Tool::Shoot;
        break;
    case SDLK_SPACE:
        throwBall();
        break;
    case SDLK_R:
        if (m_tool == Tool::Place) m_placeYaw += shift ? -m_rotateStep : m_rotateStep;
        else if (sel && !sel->ragdoll && !sel->proxy) {
            sel->yawDegrees += shift ? -m_rotateStep : m_rotateStep;
            if (const kke::ModelData* d = m_models->model(sel->model))
                m_models->setTransform(sel->instance, objectTransform(*d, sel->position, sel->yawDegrees));
        }
        break;
    case SDLK_DELETE:
    case SDLK_BACKSPACE:
        if (sel) removeObject(sel->id);
        break;
    case SDLK_G:
        if (sel && !sel->ragdoll && !sel->proxy) beginPlacing(sel->asset, sel->yawDegrees, sel->id);
        break;
    case SDLK_D:
        if (ctrl && sel) beginPlacing(sel->asset, sel->yawDegrees);
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

// Turns a placed prop into a physics object of the same size: the Synty
// mesh is hidden and a FEMFX box proxy (textured with the chosen
// material) takes its place, ready to be knocked over, dented or broken.
//
// Honest limitation: the proxy is a box of the prop's bounds, not its
// shape, and it wears a material texture, not the prop's. Shape-matched
// proxies coloured from the prop's own texture are the next step
// (ROADMAP "destructible props").
void SandboxModule::makeBreakable(Object& o) {
#if KKE_ENABLE_FEMFX
    auto* physics = m_app->getModule<kke::PhysicsModule>();
    const kke::ModelData* d = m_models->model(o.model);
    if (!physics || !d || o.proxy) return;
    glm::vec3 size = glm::max(d->boundsMax - d->boundsMin, glm::vec3(0.05f));
    // Cells: ~kProxyCellSize each, at least 1 per axis, then scaled down
    // uniformly until the total fits the budget.
    glm::ivec3 cells = glm::max(glm::ivec3(glm::round(size / kProxyCellSize)), glm::ivec3(1));
    while (cells.x * cells.y * cells.z > kMaxProxyCells) cells = glm::max(cells * 3 / 4, glm::ivec3(1));
    const BreakMaterial& bm = kBreakMaterials[std::clamp(m_breakMaterial, 0, kBreakMaterialCount - 1)];
    glm::vec3 center = o.position + glm::vec3(0.0f, size.y * 0.5f + 0.005f, 0.0f);
    if (bm.plastic) {
        kke::TetMeshData box = kke::PhysicsModule::buildGridBox(cells.x, cells.y, cells.z, size.x, size.y, size.z);
        glm::mat3 yaw = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(o.yawDegrees), glm::vec3(0, 1, 0)));
        for (glm::vec3& v : box.vertices) v = yaw * v;
        o.proxy = physics->spawnPlasticTetMesh(box, center, bm.material);
    } else {
        o.proxy = physics->spawnFracturableBox(cells, size, center, bm.material, o.yawDegrees);
    }
    if (!o.proxy) {
        m_status = "Physics is full (object limit reached) - delete something first";
        return;
    }
    m_models->setVisible(o.instance, false);
    m_status = o.asset + " is now " + bm.name + " (" + std::to_string(cells.x * cells.y * cells.z * 6) + " tets) - F throws a ball";
#else
    (void)o;
#endif
}

void SandboxModule::restoreProp(Object& o) {
#if KKE_ENABLE_FEMFX
    if (auto* physics = m_app->getModule<kke::PhysicsModule>()) physics->removeObject(o.proxy);
#endif
    o.proxy = 0;
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

bool SandboxModule::saveLayout(const std::string& path) {
    nlohmann::json j;
    j["format"] = "kke-sandbox-layout";
    j["version"] = 1;
    j["objects"] = nlohmann::json::array();
    for (const Object& o : m_objects) {
        nlohmann::json e = { { "asset", o.asset }, { "position", { o.position.x, o.position.y, o.position.z } }, { "yaw", o.yawDegrees } };
        // File name only, so a layout works on another machine's pack folder.
        if (!o.texture.empty()) e["texture"] = std::filesystem::path(o.texture).filename().string();
        j["objects"].push_back(e);
    }
    std::ofstream f(path);
    if (!f) { m_status = "Could not write " + path; return false; }
    f << j.dump(2) << "\n";
    m_status = "Saved " + std::to_string(m_objects.size()) + " objects to " + path;
    kke::log::get(name())->info("{}", m_status);
    return true;
}

bool SandboxModule::loadLayout(const std::string& path) {
    std::ifstream f(path);
    if (!f) { m_status = "Could not open " + path; return false; }
    nlohmann::json j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.contains("objects") || !j["objects"].is_array()) {
        m_status = path + " is not a sandbox layout";
        return false;
    }
    clearAll();
    size_t missing = 0;
    for (const auto& e : j["objects"]) {
        std::string asset = e.value("asset", "");
        auto p = e.value("position", std::vector<float>{ 0, 0, 0 });
        Object* o = p.size() == 3 ? spawnObject(asset, { p[0], p[1], p[2] }, e.value("yaw", 0.0f)) : nullptr;
        if (!o) { ++missing; continue; }
        std::string tex = e.value("texture", "");
        if (const kke::CatalogPack* pack = packOf(asset); pack && !tex.empty()) {
            for (const std::string& v : pack->textureVariants)
                if (std::filesystem::path(v).filename() == tex) { o->texture = v; m_models->setTextureOverride(o->instance, v); }
        }
    }
    m_selected = 0;
    m_status = "Loaded " + std::to_string(m_objects.size()) + " objects from " + path;
    if (missing) m_status += " (" + std::to_string(missing) + " not found in these packs)";
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
            bool active = m_tool == Tool::Place && !m_movingId && m_placeAsset == a->name;
            if (ImGui::Selectable(a->name.c_str(), active)) beginPlacing(a->name, m_placeYaw);
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
    ImGui::Text("%zu objects, %zu draw calls, %.0f FPS", m_objects.size(), m_models->drawCallsLastFrame(), io.Framerate);
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
        ImGui::TextWrapped("Click: select   G: move   R: rotate   Ctrl+D: duplicate   Del: delete");
    }
    const char* snaps[] = { "Off", "0.25 m", "0.5 m", "1 m", "2.5 m", "5 m" };
    const float snapValues[] = { 0.0f, 0.25f, 0.5f, 1.0f, 2.5f, 5.0f };
    int snapIndex = 0;
    for (int i = 0; i < 6; ++i) if (std::fabs(m_snap - snapValues[i]) < 1e-4f) snapIndex = i;
    if (ImGui::Combo("Grid snap", &snapIndex, snaps, 6)) m_snap = snapValues[snapIndex];
    ImGui::SliderFloat("Rotate step", &m_rotateStep, 5.0f, 90.0f, "%.0f deg");

    ImGui::SeparatorText("Selected");
    if (Object* o = find(m_selected)) {
        ImGui::Text("%s", o->asset.c_str());
        ImGui::TextDisabled("at (%.2f, %.2f, %.2f), %.0f deg", o->position.x, o->position.y, o->position.z, o->yawDegrees);
        if (const kke::ModelData* d = m_models->model(o->model)) {
            glm::vec3 size = d->boundsMax - d->boundsMin;
            ImGui::TextDisabled("%.2f x %.2f x %.2f m, %zu tris, %zu bones", size.x, size.y, size.z, d->triangleCount(), d->bones.size());
        }
        if (const kke::CatalogPack* pack = packOf(o->asset); pack && pack->textureVariants.size() > 1) {
            std::string current = o->texture.empty() ? "Model's own" : std::filesystem::path(o->texture).stem().string();
            if (ImGui::BeginCombo("Texture", current.c_str())) {
                if (ImGui::Selectable("Model's own", o->texture.empty())) { o->texture.clear(); m_models->setTextureOverride(o->instance, ""); }
                for (const std::string& v : pack->textureVariants) {
                    if (ImGui::Selectable(std::filesystem::path(v).stem().string().c_str(), o->texture == v)) {
                        o->texture = v;
                        m_models->setTextureOverride(o->instance, v);
                    }
                }
                ImGui::EndCombo();
            }
        }
        if (ImGui::Button("Move (G)")) beginPlacing(o->asset, o->yawDegrees, o->id);
        ImGui::SameLine();
        if (ImGui::Button("Duplicate")) beginPlacing(o->asset, o->yawDegrees);
        ImGui::SameLine();
        if (ImGui::Button("Delete")) removeObject(o->id);
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
                if (ImGui::Button("Restore prop (X)")) restoreProp(*o);
            } else if (ImGui::Button("Make breakable (X)")) {
                makeBreakable(*o);
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
        ImGui::SliderFloat("Ball speed", &m_ballSpeed, 5.0f, 40.0f, "%.0f m/s");
        ImGui::TextWrapped("Shoot tool (2) or F / Space: throw a ball at the cursor (max %zu, oldest removed)", kMaxBalls);
    }

    lookUi();

    ImGui::SeparatorText("Layout");
    ImGui::InputText("File", m_layoutPath, sizeof(m_layoutPath));
    if (ImGui::Button("Save (Ctrl+S)")) saveLayout(m_layoutPath);
    ImGui::SameLine();
    if (ImGui::Button("Load (Ctrl+L)")) loadLayout(m_layoutPath);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) clearAll();
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
            for (Object& o : m_objects) {
                const kke::CatalogPack* op = packOf(o.asset);
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

void SandboxModule::renderUi() {
    assetBrowserUi();
    inspectorUi();
}

} // namespace kke_sandbox
