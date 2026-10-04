// The nets: the cloth demo's tennis net (games/cloth_demo, "Nets"), a Jolt
// soft-body net on every court, held along the top cable and at the
// posts, sagging to the centre strap. The ball's flight still decides a
// let or a net ball (Ball.cpp); a proxy sphere that only cloth feels
// follows each court's ball, so a ball into the net pushes it back and
// it swings. Simulated at 10 cm, drawn at 5 cm (the threads between are
// laid in between the simulated ones), each thread a ribbon facing the
// camera, at least a pixel wide so a far net doesn't break into dashes.

#include "TennisModule.h"

#include "kke/Cloth.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/RigidBodyModule.h"

#include <algorithm>
#include <cmath>

namespace tennis {

namespace {
constexpr int kColumns = 129;  // 10 cm across the 12.8 m between the posts
constexpr int kRows = 11;      // 10 cm down
constexpr float kBottom = 0.04f;
constexpr float kThread = 0.0025f;
const glm::vec3 kNetColour(0.1f, 0.1f, 0.1f);
} // namespace

void TennisModule::buildNets() {
    kke::RigidWorld& world = m_rigid->world();
    m_nets.clear();
    for (int c = 0; c < SportCenter::kCourts; ++c) {
        const CourtPlace& place = m_center.courts[static_cast<size_t>(c)];
        CourtNet n;
        kke::ClothDesc d;
        d.mesh = kke::clothNet(place.toWorld({ 0.0f, 0.5f, 0.0f }), 2.0f * kPostX, 1.0f, kColumns, kRows, place.dirToWorld({ 1.0f, 0.0f, 0.0f }),
                               glm::vec3(0.0f, -1.0f, 0.0f));
        // Down from the cable (under the tape) to just above the court.
        for (int x = 0; x < kColumns; ++x) {
            const float lx = -kPostX + 2.0f * kPostX * static_cast<float>(x) / (kColumns - 1);
            const float top = netHeight(lx) - 0.03f;
            for (int y = 0; y < kRows; ++y)
                d.mesh.positions[kke::clothGridIndex(d.mesh, x, y)] = place.toWorld({ lx, top - (top - kBottom) * static_cast<float>(y) / (kRows - 1), 0.0f });
        }
        d.fabric = kke::clothFabric("net");
        d.fabric.color = kNetColour;
        d.contactMass = 5.0f; // as in the cloth demo: stops a 20 m/s shot
        d.protection = kke::ClothProtection::Basic; // one layer, nothing to fold through
        for (int x = 0; x < kColumns; ++x) d.pinned.push_back(kke::clothGridIndex(d.mesh, x, 0));
        for (int y = 1; y < kRows; ++y) {
            d.pinned.push_back(kke::clothGridIndex(d.mesh, 0, y));
            d.pinned.push_back(kke::clothGridIndex(d.mesh, kColumns - 1, y));
        }
        n.lines = d.mesh.lines;
        n.cloth = world.addCloth(d);
        kke::RigidWorld::BodyDesc b;
        b.shape = kke::RigidWorld::Shape::Sphere;
        b.motion = kke::RigidWorld::Motion::Kinematic;
        b.clothOnly = true;
        b.radius = kBallRadius;
        b.position = place.toWorld({ 0.0f, -2.0f, 0.0f }); // parked under the court until a ball plays
        n.ball = world.add(b);
        m_nets.push_back(std::move(n));
    }
    m_netMesh = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
}

void TennisModule::updateNets(float dt) {
    if (m_nets.empty() || !m_netMesh) return;
    kke::RigidWorld& world = m_rigid->world();
    // The proxies: each court's ball, or parked under its court.
    for (size_t c = 0; c < m_nets.size(); ++c) {
        const CourtPlace& place = m_center.courts[c];
        glm::vec3 at = place.toWorld({ 0.0f, -2.0f, 0.0f });
        if (const Match* m = matchOn(static_cast<int>(c)); m && m->ball) at = m->ball->worldPosition();
        else if (c == 0 && m_testBall) at = m_testBall->worldPosition();
        if (dt > 0.0f) world.moveKinematic(m_nets[c].ball, at, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), dt);
    }
    // The threads.
    const kke::Camera& cam = m_app->camera();
    int w = 0, h = 0;
    m_app->window().getFramebufferSize(w, h);
    const float pixel = 2.0f * std::tan(glm::radians(cam.fovDegrees) * 0.5f) / static_cast<float>(std::max(h, 1));
    m_netVerts.clear();
    m_netIdx.clear();
    auto ribbon = [&](const glm::vec3& a, const glm::vec3& b) {
        const glm::vec3 mid = (a + b) * 0.5f;
        const glm::vec3 toCam = cam.position - mid;
        const float dist = glm::length(toCam);
        glm::vec3 side = glm::cross(b - a, toCam);
        const float sl = glm::length(side);
        if (sl < 1e-9f || dist < 1e-6f) return;
        side *= std::max(kThread, dist * pixel * 0.6f) / sl;
        const glm::vec3 n = toCam / dist;
        const uint32_t base = static_cast<uint32_t>(m_netVerts.size());
        m_netVerts.push_back({ a - side, kNetColour, n, { 0.0f, 0.0f } });
        m_netVerts.push_back({ a + side, kNetColour, n, { 0.0f, 0.0f } });
        m_netVerts.push_back({ b + side, kNetColour, n, { 0.0f, 0.0f } });
        m_netVerts.push_back({ b - side, kNetColour, n, { 0.0f, 0.0f } });
        m_netIdx.insert(m_netIdx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    };
    for (CourtNet& n : m_nets) {
        if (!world.clothPositions(n.cloth, n.pos) || n.pos.size() != static_cast<size_t>(kColumns * kRows)) continue;
        auto at = [&](int x, int y) { return n.pos[static_cast<size_t>(y * kColumns + x)]; };
        // The simulated threads, and one laid halfway between each pair (both ways).
        for (size_t i = 0; i + 1 < n.lines.size(); i += 2) ribbon(n.pos[n.lines[i]], n.pos[n.lines[i + 1]]);
        for (int y = 0; y + 1 < kRows; ++y)
            for (int x = 0; x + 1 < kColumns; ++x) {
                const glm::vec3 l = (at(x, y) + at(x, y + 1)) * 0.5f, r = (at(x + 1, y) + at(x + 1, y + 1)) * 0.5f;
                const glm::vec3 t = (at(x, y) + at(x + 1, y)) * 0.5f, b = (at(x, y + 1) + at(x + 1, y + 1)) * 0.5f;
                ribbon(l, r);
                ribbon(t, b);
            }
    }
    m_netMesh->upload(m_netVerts, m_netIdx);
}

} // namespace tennis
