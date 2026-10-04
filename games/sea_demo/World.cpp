// The world around the ships (README.md "The world"): a fort island near
// the start, big islands and sea stacks further out, from the Pirate Pack
// (sandy mounds without it). Islands are circles a ship can't sail into.
//
// The fort's palisade (FEMFX builds, KKE_ENABLE_FEMFX): five wooden wall
// panels baked with the Splinters fracture pattern. A cannonball that
// reaches them becomes a FEMFX iron ball for the last metre, so the wood
// breaks where it was hit, along the grain, and the pieces tumble onto the
// beach. FEMFX costs ~0.15-0.2 ms per awake piece per step (docs/SCALING.md),
// which is why only the fort is FEMFX: the panel shows the live step time.

#include "SeaDemoModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/FracturePattern.h"
#include "kke/Material.h"
#include "kke/modules/PhysicsModule.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace kke_sea {

namespace {

// A frustum, 1 m radius at the bottom, 1 m tall: a sandy mound with a
// green top (islands with no pack).
void moundMesh(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx) {
    const int sides = 16;
    const glm::vec3 sand(0.86f, 0.76f, 0.55f), grass(0.36f, 0.56f, 0.25f);
    for (int i = 0; i < sides; ++i) {
        const float a0 = static_cast<float>(i) / sides * 6.2831853f, a1 = static_cast<float>(i + 1) / sides * 6.2831853f;
        const glm::vec3 b0(std::cos(a0), 0.0f, std::sin(a0)), b1(std::cos(a1), 0.0f, std::sin(a1));
        const glm::vec3 t0 = b0 * 0.7f + glm::vec3(0, 1, 0), t1 = b1 * 0.7f + glm::vec3(0, 1, 0);
        glm::vec3 n = glm::normalize(glm::cross(t0 - b0, b1 - b0));
        if (n.y < 0.0f) n = -n;
        const uint32_t base = static_cast<uint32_t>(v.size());
        v.push_back({ b0, sand, n, {} });
        v.push_back({ b1, sand, n, {} });
        v.push_back({ t1, sand, n, {} });
        v.push_back({ t0, sand, n, {} });
        idx.insert(idx.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
        const uint32_t top = static_cast<uint32_t>(v.size());
        v.push_back({ glm::vec3(0, 1, 0), grass, { 0, 1, 0 }, {} });
        v.push_back({ t0, grass, { 0, 1, 0 }, {} });
        v.push_back({ t1, grass, { 0, 1, 0 }, {} });
        idx.insert(idx.end(), { top, top + 2, top + 1 });
    }
}

} // namespace

void SeaDemoModule::buildWorld() {
    m_physics = m_app->getModule<kke::PhysicsModule>();
    {
        std::vector<kke::Vertex> v;
        std::vector<uint32_t> idx;
        moundMesh(v, idx);
        m_islandMesh = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
        m_islandMesh->upload(v, idx);
    }
    struct Spot { const char* asset; glm::vec3 at; float radius; float yaw; float height; };
    // Radius: what a ship can't sail into. Height: the mound without the pack.
    const Spot spots[] = {
        { "SM_Env_Background_Island_01", { -280.0f, -1.0f, 190.0f }, 24.0f, 0.4f, 6.0f },
        { "SM_Env_Background_Island_03", { 320.0f, -1.0f, -230.0f }, 12.0f, 2.1f, 6.0f },
        { "SM_Env_Background_Island_02", { 60.0f, -1.0f, -420.0f }, 20.0f, 1.0f, 6.0f },
        { "SM_Env_Rock_Huge_01", { -130.0f, -2.0f, -150.0f }, 16.0f, 0.7f, 20.0f },
        { "SM_Env_Rock_Huge_03", { 190.0f, -2.0f, 230.0f }, 15.0f, 2.6f, 20.0f },
    };
    for (const Spot& s : spots) {
        m_islands.push_back({ glm::vec3(s.at.x, 0.0f, s.at.z), s.radius });
        kke::ModelModule::ModelId id = m_library.prop(s.asset);
        if (id && m_models) {
            const float k = m_library.propScale(id);
            m_worldInstances.push_back(m_models->spawn(
                id, glm::translate(glm::mat4(1.0f), s.at) * glm::rotate(glm::mat4(1.0f), s.yaw, glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(k))));
        } else {
            m_islandBoxes.push_back(glm::translate(glm::mat4(1.0f), glm::vec3(s.at.x, -1.5f, s.at.z)) *
                                    glm::scale(glm::mat4(1.0f), glm::vec3(s.radius, s.height, s.radius)));
        }
    }
    // The fort island: a low sandy island with palms, close to the start.
    m_fortCenter = glm::vec3(95.0f, 0.0f, 70.0f);
    m_fortFacing = std::atan2(-m_fortCenter.x, -m_fortCenter.z); // the walls face where the ships start
    const float fortRadius = 17.0f;
    m_islands.push_back({ m_fortCenter, fortRadius });
    m_islandBoxes.push_back(glm::translate(glm::mat4(1.0f), m_fortCenter + glm::vec3(0.0f, -1.6f, 0.0f)) *
                            glm::scale(glm::mat4(1.0f), glm::vec3(fortRadius * 1.15f, 1.6f, fortRadius * 1.15f)));
    if (m_models) {
        const char* palms[] = { "SM_Env_PalmTree_01", "SM_Env_PalmTree_02", "SM_Env_PalmTree_03" };
        for (int i = 0; i < 6; ++i) {
            const kke::ModelModule::ModelId id = m_library.prop(palms[i % 3]);
            if (!id) break;
            const float a = m_fortFacing + glm::pi<float>() + (static_cast<float>(i) - 2.5f) * 0.45f;
            const glm::vec3 at = m_fortCenter + glm::vec3(std::sin(a), 0.0f, std::cos(a)) * (fortRadius * (0.45f + 0.08f * static_cast<float>(i % 3)));
            m_worldInstances.push_back(m_models->spawn(id, glm::translate(glm::mat4(1.0f), at) * glm::rotate(glm::mat4(1.0f), a * 3.0f, glm::vec3(0, 1, 0)) *
                                                               glm::scale(glm::mat4(1.0f), glm::vec3(m_library.propScale(id)))));
        }
        // A stone tower behind the palisade (or on its own, without FEMFX).
        if (kke::ModelModule::ModelId id = m_library.prop("SM_Bld_Fort_Tower_01")) {
            const glm::vec3 at = m_fortCenter - glm::vec3(std::sin(m_fortFacing), 0.0f, std::cos(m_fortFacing)) * 6.0f;
            m_worldInstances.push_back(m_models->spawn(id, glm::translate(glm::mat4(1.0f), at + glm::vec3(0.0f, -0.2f, 0.0f)) *
                                                               glm::rotate(glm::mat4(1.0f), m_fortFacing, glm::vec3(0, 1, 0)) *
                                                               glm::scale(glm::mat4(1.0f), glm::vec3(m_library.propScale(id) * 0.6f))));
        }
    }
    buildFort();
}

void SeaDemoModule::buildFort() {
#if KKE_ENABLE_FEMFX
    if (!m_physics) return;
    for (uint32_t h : m_fortWalls) m_physics->removeObject(h);
    for (const FortBall& b : m_fortBalls) m_physics->removeObject(b.handle);
    m_fortWalls.clear();
    m_fortBalls.clear();
    // Oak planks: tools/physics_lab "shoot" wood, a bit tougher so a wall
    // stands up to the wind and takes a few balls.
    kke::Material wood;
    wood.density = 650.0f;
    wood.stiffness = 1.0e7f;
    wood.poissonsRatio = 0.3f;
    wood.fractureStressThreshold = 1.5e5f;
    wood.roughness = 0.75f;
    wood.textureId = 0;
    // Five panels in a straight palisade facing the sea, standing on
    // FEMFX's floor (y = 0, the beach): 3.2 m wide, 2.6 m tall, 0.3 m
    // thick, 0.2 m apart. Patterned boxes are axis-aligned, so the wall
    // faces along whichever axis is closer to the sea side; panels must
    // never overlap at spawn (FEMFX would push them apart hard enough to
    // break them before a ball arrives). The cells run along the planks,
    // so they splinter top to bottom.
    const glm::vec3 facing(std::sin(m_fortFacing), 0.0f, std::cos(m_fortFacing));
    const bool facesZ = std::abs(facing.z) >= std::abs(facing.x);
    const glm::vec3 out = facesZ ? glm::vec3(0.0f, 0.0f, facing.z < 0.0f ? -1.0f : 1.0f) : glm::vec3(facing.x < 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f);
    const glm::vec3 along(out.z, 0.0f, -out.x);
    m_fortOut = out;
    // FEMFX costs ~0.15-0.2 ms per moving piece (docs/SCALING.md): 60
    // splinters at most keeps a wall breaking inside ~5-10 ms a step (the
    // oldest resting splinter goes first).
    m_physics->setDebrisBudget(60);
    const glm::ivec3 cells = facesZ ? glm::ivec3(10, 8, 1) : glm::ivec3(1, 8, 10);
    const glm::vec3 size = facesZ ? glm::vec3(3.2f, 2.6f, 0.3f) : glm::vec3(0.3f, 2.6f, 3.2f);
    for (int i = 0; i < 5; ++i) {
        const glm::vec3 at = m_fortCenter + out * 9.0f + along * ((static_cast<float>(i) - 2.0f) * 3.4f) + glm::vec3(0.0f, 1.31f, 0.0f);
        m_fortWalls.push_back(m_physics->spawnPatternedBox(cells, size, at, wood, static_cast<int>(kke::FracturePattern::Splinters), 0.6f, 0,
                                                            glm::vec3(0.0f), 2.5f));
    }
    kke::log::get("SeaDemo")->info("fort: {} FEMFX wall panels", m_fortWalls.size());
#endif
}

bool SeaDemoModule::fortHit(const Ball& b, const glm::vec3& from, const glm::vec3& to) {
#if KKE_ENABLE_FEMFX
    if (!m_physics || m_fortWalls.empty()) return false;
    // Close to the palisade and low: hand the ball to FEMFX for the impact.
    // The wall is 17 m long and 9 m out from the fort's centre (buildFort).
    const glm::vec3 d = to - (m_fortCenter + m_fortOut * 9.0f);
    const glm::vec3 along(m_fortOut.z, 0.0f, -m_fortOut.x);
    if (std::abs(glm::dot(d, m_fortOut)) > 2.0f || std::abs(glm::dot(d, along)) > 9.0f || to.y > 3.2f || to.y < -0.5f) return false;
    const glm::vec3 dir = glm::normalize(to - from);
    // FEMFX steps 60 times a second: at 80 m/s a ball would jump through
    // a 0.3 m plank, so it arrives slower and heavier (same momentum).
    const float speed = std::min(glm::length(b.vel), 30.0f);
    kke::Material iron;
    iron.density = 7800.0f * std::max(1.0f, glm::length(b.vel) / speed) * std::max(0.5f, b.mass / 5.4f);
    iron.stiffness = 2.0e8f;
    iron.poissonsRatio = 0.28f;
    iron.fractureStressThreshold = 1.0e12f;
    iron.metallic = 0.9f;
    iron.roughness = 0.4f;
    kke::PhysicsModule::TetSpawnOptions opts;
    opts.velocity = dir * speed;
    const glm::vec3 at = to - dir * 1.2f;
    m_fortBalls.push_back({ m_physics->spawnTetMeshWithOptions(kke::PhysicsModule::buildSphere(3, 0.18f), at, iron, opts), 0.0f });
    if (m_fx) m_fx->sparks(to, -dir, 12, 6.0f);
    return true;
#else
    (void)b;
    (void)from;
    (void)to;
    return false;
#endif
}

void SeaDemoModule::stepFort(float dt) {
#if KKE_ENABLE_FEMFX
    if (!m_physics) return;
    for (size_t i = 0; i < m_fortBalls.size();) {
        m_fortBalls[i].age += dt;
        if (m_fortBalls[i].age > 6.0f) {
            m_physics->removeObject(m_fortBalls[i].handle);
            m_fortBalls.erase(m_fortBalls.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
#else
    (void)dt;
#endif
}

// Ships (and anything else floating) can't sail into an island: pushed
// back out, losing the speed they had toward it, scraping the hull.
void SeaDemoModule::keepOffIslands() {
    for (size_t i = 0; i < m_ships.size(); ++i) {
        Ship& s = m_ships[i];
        if (!s.alive || s.remote) continue;
        kke::FloatingBody& b = m_bodies.bodies()[s.body];
        const ShipArt& art = m_library.art(s.cls);
        for (const Island& isl : m_islands) {
            glm::vec2 d(b.position.x - isl.center.x, b.position.z - isl.center.z);
            const float dist = glm::length(d);
            const float reach = isl.radius + art.hullHalf.x + art.hullHalf.z * 0.5f;
            if (dist >= reach || dist < 1e-3f) continue;
            const glm::vec2 n = d / dist;
            b.position.x += n.x * (reach - dist);
            b.position.z += n.y * (reach - dist);
            const float into = glm::dot(glm::vec2(b.velocity.x, b.velocity.z), n);
            if (into < 0.0f) {
                b.velocity.x -= n.x * into;
                b.velocity.z -= n.y * into;
                if (into < -1.5f && !s.sinking) {
                    const glm::vec3 at(b.position.x - n.x * art.hullHalf.x, b.position.y, b.position.z - n.y * art.hullHalf.x);
                    damageShip(i, at, glm::vec3(-n.x, 0.0f, -n.y), -into * 6.0f, false);
                }
            }
        }
    }
    for (const Floater& f : m_floaters) {
        kke::FloatingBody& b = m_bodies.bodies()[f.body];
        if (!b.alive) continue;
        for (const Island& isl : m_islands) {
            glm::vec2 d(b.position.x - isl.center.x, b.position.z - isl.center.z);
            const float dist = glm::length(d);
            if (dist >= isl.radius || dist < 1e-3f) continue;
            const glm::vec2 n = d / dist;
            b.position.x += n.x * (isl.radius - dist);
            b.position.z += n.y * (isl.radius - dist);
            b.velocity.x *= 0.3f;
            b.velocity.z *= 0.3f;
        }
    }
}

void SeaDemoModule::drawWorld(const kke::RenderContext& ctx) {
    for (const glm::mat4& m : m_islandBoxes) m_islandMesh->draw(ctx, m, 0.0f, 0.95f);
}

} // namespace kke_sea
