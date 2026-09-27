#include "Scenery.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/SceneLoader.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/RigidBodyModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace command_kit {

void appendBox(const glm::vec3& center, const glm::vec3& half, const glm::vec3& color, std::vector<kke::Vertex>& v,
               std::vector<uint32_t>& idx) {
    const glm::vec3 normals[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (const glm::vec3& n : normals) {
        const glm::vec3 u = std::abs(n.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        const glm::vec3 w = glm::cross(n, u);
        const uint32_t base = static_cast<uint32_t>(v.size());
        for (glm::vec2 k : { glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1) })
            v.push_back({ center + (n + u * k.x + w * k.y) * half, color, n, glm::vec2(0.0f) });
        if (glm::dot(glm::cross(u, w), n) >= 0.0f) idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        else idx.insert(idx.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
    }
}

Scenery::Scenery(kke::Application& app, kke::ModelModule& models, kke::RigidBodyModule& rigid)
    : m_app(app), m_models(models), m_rigid(rigid) {
    const char* base = SDL_GetBasePath();
    const std::string folder = kke::findAssetFolder("assets/synty", { "KKE_ASSETS_DIR", "KKE_SYNTY_DIR" }, base ? base : "");
    if (!folder.empty()) m_catalog = kke::AssetCatalog::scan(folder);
    if (m_catalog.assets.empty())
        kke::log::get("Scenery")->info("no asset packs found (assets/synty or KKE_ASSETS_DIR): the scene is blocks");
}

Scenery::~Scenery() {
    kke::RigidWorld& w = m_rigid.world();
    for (kke::RigidWorld::BodyId b : m_bodies) w.remove(b);
}

void Scenery::ground(float half, const glm::vec3& color) {
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    const glm::vec3 h(half, 0.5f, half), c(0.0f, -0.5f, 0.0f);
    appendBox(c, h, color, v, idx);
    m_ground = std::make_unique<kke::DynamicMeshRenderer>(m_app);
    m_ground->upload(v, idx);
    kke::RigidWorld::BodyDesc d;
    d.motion = kke::RigidWorld::Motion::Static;
    d.halfExtents = h;
    d.position = c;
    m_bodies.push_back(m_rigid.world().add(d));
}

kke::ModelModule::ModelId Scenery::model(const std::string& name, const std::vector<std::string>& packs, bool animations) {
    const kke::CatalogAsset* a = m_catalog.find(name, packs);
    if (!a) return 0;
    kke::ModelLoadOptions opts = kke::packLoadOptions(m_catalog, *a);
    opts.loadAnimations = animations;
    const kke::ModelModule::ModelId id = m_models.load(a->path, opts);
    if (id && std::find(m_used.begin(), m_used.end(), name) == m_used.end()) m_used.push_back(name);
    return id;
}

kke::ModelModule::InstanceId Scenery::place(const std::string& name, const glm::vec3& pos, float yawDegrees, float scale, bool collide,
                                            const std::vector<std::string>& packs) {
    const kke::ModelModule::ModelId id = model(name, packs);
    const kke::ModelData* d = id ? m_models.model(id) : nullptr;
    if (!d) return 0;
    const glm::mat4 xf = glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), pos), glm::radians(-yawDegrees), glm::vec3(0, 1, 0)),
                                    glm::vec3(scale));
    const kke::ModelModule::InstanceId inst = m_models.spawn(id, xf);
    m_models.setOverlayEnabled(inst, false);
    if (collide) {
        const glm::vec3 mn = d->boundsMin * scale, mx = d->boundsMax * scale;
        kke::RigidWorld::BodyDesc b;
        b.motion = kke::RigidWorld::Motion::Static;
        b.halfExtents = glm::max((mx - mn) * 0.5f, glm::vec3(0.02f));
        const glm::quat q = glm::angleAxis(glm::radians(-yawDegrees), glm::vec3(0, 1, 0));
        b.position = pos + q * ((mn + mx) * 0.5f);
        b.rotation = q;
        m_bodies.push_back(m_rigid.world().add(b));
    }
    return inst;
}

void Scenery::block(const glm::vec3& center, const glm::vec3& half, const glm::vec3& color, bool collide) {
    appendBox(center, half, color, m_blockVerts, m_blockIdx);
    m_blocksDirty = true;
    if (!collide) return;
    kke::RigidWorld::BodyDesc b;
    b.motion = kke::RigidWorld::Motion::Static;
    b.halfExtents = half;
    b.position = center;
    m_bodies.push_back(m_rigid.world().add(b));
}

void Scenery::logUsed(const char* who) const {
    if (m_used.empty()) return;
    std::string list;
    for (const std::string& n : m_used) list += (list.empty() ? "" : ", ") + n;
    kke::log::get(who)->info("pack assets used: {}", list);
}

void Scenery::render(const kke::RenderContext& ctx) {
    if (m_blocksDirty) {
        if (!m_blocks) m_blocks = std::make_unique<kke::DynamicMeshRenderer>(m_app);
        m_blocks->upload(m_blockVerts, m_blockIdx);
        m_blocksDirty = false;
    }
    if (m_ground) m_ground->draw(ctx, glm::mat4(1.0f), 0.0f, 0.95f);
    if (m_blocks) m_blocks->draw(ctx, glm::mat4(1.0f), 0.0f, 0.8f);
}

void Scenery::renderShadow(const kke::ShadowRenderContext& ctx) {
    if (m_blocks) m_blocks->drawShadow(ctx);
}

} // namespace command_kit
