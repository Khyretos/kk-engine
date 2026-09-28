// The sport center (Court.h's layout): ten courts with their lines, nets,
// fences and benches, the promenade between the two rows, the FEMFX walls
// and roof that keep each court's ball in, and the cameras.

#include "TennisModule.h"

#include "kke/Log.h"
#include "kke/PhysicsBridge.h"
#include "kke/SphereImpostors.h"
#include "kke/Viewports.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/PhysicsModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <algorithm>
#include <cmath>

namespace tennis {

namespace {

using Verts = std::vector<kke::Vertex>;
using Indices = std::vector<uint32_t>;

// An axis-aligned box in court space, turned with the court.
void appendBox(const CourtPlace& place, const glm::vec3& center, const glm::vec3& half, const glm::vec3& color, Verts& v, Indices& idx,
               glm::vec2 uv = glm::vec2(0.0f)) {
    const glm::vec3 normals[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (const glm::vec3& n : normals) {
        const glm::vec3 u = std::abs(n.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        const glm::vec3 w = glm::cross(n, u);
        const uint32_t base = static_cast<uint32_t>(v.size());
        for (glm::vec2 k : { glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1) })
            v.push_back({ place.toWorld(center + (n + u * k.x + w * k.y) * half), color, place.dirToWorld(n), uv });
        if (glm::dot(glm::cross(u, w), n) >= 0.0f) idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        else idx.insert(idx.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
    }
}

// A flat panel (for the translucent windscreens and the net): both faces.
void appendPanel(const CourtPlace& place, const glm::vec3& a, const glm::vec3& b, float bottom, float top, const glm::vec3& color, float density,
                 float milk, Verts& v, Indices& idx) {
    const glm::vec3 along = b - a;
    const glm::vec3 n = glm::normalize(glm::cross(along, glm::vec3(0, 1, 0)));
    for (int face = 0; face < 2; ++face) {
        const glm::vec3 nn = face == 0 ? n : -n;
        const uint32_t base = static_cast<uint32_t>(v.size());
        const glm::vec3 p[4] = { { a.x, bottom, a.z }, { b.x, bottom, b.z }, { b.x, top, b.z }, { a.x, top, a.z } };
        for (const glm::vec3& q : p) v.push_back({ place.toWorld(q), color, place.dirToWorld(nn), glm::vec2(density, milk) });
        if (face == 0) idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        else idx.insert(idx.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
    }
}

} // namespace

void TennisModule::buildWorld() {
    kke::RigidWorld& w = m_rigid->world();
    auto solid = [&](const CourtPlace& place, const glm::vec3& c, const glm::vec3& h) {
        kke::RigidWorld::BodyDesc d;
        d.motion = kke::RigidWorld::Motion::Static;
        d.position = place.toWorld(c);
        d.rotation = glm::angleAxis(glm::radians(place.yawDegrees), glm::vec3(0, 1, 0));
        d.halfExtents = h;
        m_statics.push_back(w.add(d));
    };
    const CourtPlace world; // identity: world space
    Verts cv, sv, tv;
    Indices ci, si, ti;

    // The ground: the promenade's paving, and the Jolt floor under everything.
    const glm::vec3 hallC = (m_center.hallMin + m_center.hallMax) * 0.5f;
    const glm::vec3 hallH = (m_center.hallMax - m_center.hallMin) * 0.5f;
    appendBox(world, { hallC.x, -0.25f, hallC.z }, { hallH.x + 30.0f, 0.25f, hallH.z + 30.0f }, { 0.52f, 0.5f, 0.46f }, cv, ci);
    solid(world, { hallC.x, -0.25f, hallC.z }, { hallH.x + 30.0f, 0.25f, hallH.z + 30.0f });
    // Grass beyond the sport center's edge.
    for (int s : { -1, 1 }) {
        appendBox(world, { hallC.x, -0.24f, static_cast<float>(s) * (hallH.z + 18.0f) }, { hallH.x + 30.0f, 0.25f, 12.0f }, { 0.32f, 0.5f, 0.24f }, cv, ci);
        appendBox(world, { static_cast<float>(s) * (hallH.x + 18.0f), -0.24f, hallC.z }, { 12.0f, 0.25f, hallH.z + 30.0f }, { 0.32f, 0.5f, 0.24f }, cv, ci);
    }

    const glm::vec3 surround(0.24f, 0.47f, 0.33f), court(0.2f, 0.36f, 0.62f), line(0.95f, 0.95f, 0.95f);
    const glm::vec3 green(0.1f, 0.26f, 0.16f), bench(0.55f, 0.36f, 0.2f), post(0.2f, 0.22f, 0.24f);
    std::vector<kke::BridgeBox> femfxBoxes;
    for (int c = 0; c < SportCenter::kCourts; ++c) {
        const CourtPlace& p = m_center.courts[static_cast<size_t>(c)];
        // Surface: the green surround to the fence, the blue court, the lines.
        appendBox(p, { 0.0f, 0.005f, 0.0f }, { kFenceHalfX, 0.005f, kFenceHalfZ }, surround, cv, ci);
        appendBox(p, { 0.0f, 0.015f, 0.0f }, { kDoublesHalfWidth + 0.3f, 0.005f, kHalfLength + 0.3f }, court, cv, ci);
        // A centimetre per layer: thinner and the far lines flicker (depth precision).
        const float lw = kLineWidth * 0.5f, ly = 0.025f, lh = 0.005f;
        for (float z : { -kHalfLength, kHalfLength }) appendBox(p, { 0.0f, ly, z }, { kDoublesHalfWidth, lh, lw }, line, cv, ci);
        for (float x : { -kDoublesHalfWidth, kDoublesHalfWidth, -kSinglesHalfWidth, kSinglesHalfWidth })
            appendBox(p, { x, ly, 0.0f }, { lw, lh, kHalfLength }, line, cv, ci);
        for (float z : { -kServiceLine, kServiceLine }) appendBox(p, { 0.0f, ly, z }, { kSinglesHalfWidth, lh, lw }, line, cv, ci);
        appendBox(p, { 0.0f, ly, 0.0f }, { lw, lh, kServiceLine }, line, cv, ci);
        for (float z : { -kHalfLength, kHalfLength }) appendBox(p, { 0.0f, ly, z - 0.05f * (z > 0 ? 1.0f : -1.0f) }, { lw, lh, 0.05f }, line, cv, ci);
        // The net: posts, a white tape along the sagging top, and the mesh
        // (translucent, drawn after everything solid).
        for (float x : { -kPostX, kPostX }) appendBox(p, { x, kNetHeightPost * 0.5f + 0.02f, 0.0f }, { 0.04f, kNetHeightPost * 0.5f + 0.02f, 0.04f }, post, cv, ci);
        constexpr int kTape = 12;
        for (int i = 0; i < kTape; ++i) {
            const float x0 = -kPostX + 2.0f * kPostX * static_cast<float>(i) / kTape;
            const float x1 = -kPostX + 2.0f * kPostX * static_cast<float>(i + 1) / kTape;
            const float y0 = netHeight(x0), y1 = netHeight(x1);
            appendBox(p, { 0.5f * (x0 + x1), 0.5f * (y0 + y1) - 0.03f, 0.0f }, { 0.5f * (x1 - x0) + 0.005f, 0.03f, 0.012f }, line, cv, ci);
            appendPanel(p, { x0, 0.0f, 0.0f }, { x1, 0.0f, 0.0f }, 0.03f, std::min(y0, y1) - 0.05f, { 0.08f, 0.09f, 0.1f }, 3.5f, 0.15f, tv, ti);
        }
        // The fence: posts every 3 m, a top rail, green windscreens, and Jolt
        // walls so nobody walks through it. The ball's own walls and roof
        // are FEMFX boxes (Ball::courtBoxes), invisible, up to kLidHeight.
        const float fx = kFenceHalfX, fz = kFenceHalfZ, fh = kFenceHeight;
        auto fenceSide = [&](const glm::vec3& a, const glm::vec3& b) {
            const float len = glm::length(b - a);
            const int posts = std::max(1, static_cast<int>(std::round(len / 3.0f)));
            for (int i = 0; i <= posts; ++i) {
                const glm::vec3 q = a + (b - a) * (static_cast<float>(i) / posts);
                appendBox(p, { q.x, fh * 0.5f, q.z }, { 0.04f, fh * 0.5f, 0.04f }, post, sv, si);
            }
            const glm::vec3 mid = (a + b) * 0.5f;
            const glm::vec3 half = glm::abs(b - a) * 0.5f + glm::vec3(0.03f);
            appendBox(p, { mid.x, fh, mid.z }, { half.x, 0.03f, half.z }, post, sv, si);
            appendPanel(p, a, b, 0.05f, fh * 0.62f, green, 1.2f, 0.35f, tv, ti);
            solid(p, { mid.x, fh * 0.5f, mid.z }, { std::max(half.x, 0.06f), fh * 0.5f, std::max(half.z, 0.06f) });
        };
        fenceSide({ -fx, 0, -fz }, { fx, 0, -fz });
        fenceSide({ -fx, 0, fz }, { fx, 0, fz });
        fenceSide({ -fx, 0, -fz }, { -fx, 0, fz });
        fenceSide({ fx, 0, -fz }, { fx, 0, fz });
        // Benches along both long sides, outside the fence, and the umpire's chair.
        for (int sx : { -1, 1 }) {
            const float x = static_cast<float>(sx) * (fx + 0.75f);
            appendBox(p, { x, 0.22f, 0.0f }, { 0.22f, 0.03f, 11.0f }, bench, sv, si);
            for (float z : { -10.5f, -5.25f, 0.0f, 5.25f, 10.5f }) appendBox(p, { x, 0.1f, z }, { 0.2f, 0.1f, 0.05f }, post, sv, si);
            solid(p, { x, 0.22f, 0.0f }, { 0.22f, 0.25f, 11.0f });
        }
        appendBox(p, { kPostX + 1.1f, 0.9f, 0.0f }, { 0.35f, 0.9f, 0.35f }, { 0.25f, 0.35f, 0.5f }, sv, si);
        // Floodlights at the corners.
        for (int sx : { -1, 1 })
            for (int sz : { -1, 1 }) {
                const glm::vec3 base(static_cast<float>(sx) * (fx + 0.3f), 0.0f, static_cast<float>(sz) * (fz + 0.3f));
                appendBox(p, base + glm::vec3(0.0f, 5.0f, 0.0f), { 0.08f, 5.0f, 0.08f }, post, sv, si);
                appendBox(p, base + glm::vec3(0.0f, 10.1f, 0.0f), { 0.5f, 0.2f, 0.25f }, { 0.9f, 0.9f, 0.8f }, sv, si);
            }
        Ball::courtBoxes(p, static_cast<uint64_t>(c) * 64u, femfxBoxes);
    }
    // The promenade: benches down the middle and a low wall round the
    // sport center (Jolt too: nobody wanders off into the grass).
    for (int i = -3; i <= 3; ++i) {
        const float x = static_cast<float>(i) * 14.0f;
        appendBox(world, { x, 0.22f, 0.0f }, { 2.0f, 0.03f, 0.25f }, bench, sv, si);
        appendBox(world, { x, 0.1f, 0.0f }, { 1.8f, 0.1f, 0.2f }, post, sv, si);
        solid(world, { x, 0.22f, 0.0f }, { 2.0f, 0.25f, 0.25f });
    }
    const glm::vec3 mn = m_center.hallMin, mx = m_center.hallMax;
    const glm::vec3 wall(0.62f, 0.6f, 0.56f);
    for (int s : { -1, 1 }) {
        const float z = s > 0 ? mx.z : mn.z, x = s > 0 ? mx.x : mn.x;
        appendBox(world, { hallC.x, 0.5f, z }, { hallH.x, 0.5f, 0.25f }, wall, sv, si);
        solid(world, { hallC.x, 1.0f, z }, { hallH.x, 1.0f, 0.25f });
        appendBox(world, { x, 0.5f, hallC.z }, { 0.25f, 0.5f, hallH.z }, wall, sv, si);
        solid(world, { x, 1.0f, hallC.z }, { 0.25f, 1.0f, hallH.z });
    }
    m_courtMesh = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    m_courtMesh->upload(cv, ci);
    m_standMesh = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    m_standMesh->upload(sv, si);
    m_fenceMesh = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    m_fenceMesh->upload(tv, ti);
    m_physics->setExternalBoxes(femfxBoxes);
    kke::log::get(name())->info("sport center: {} courts, {} FEMFX walls and nets for the balls, {} Jolt bodies", SportCenter::kCourts,
                                femfxBoxes.size(), m_statics.size());
}

void TennisModule::renderCourts(const kke::RenderContext& ctx) {
    if (m_courtMesh) m_courtMesh->draw(ctx, glm::mat4(1.0f), 0.0f, 0.9f);
    if (m_standMesh) m_standMesh->draw(ctx, glm::mat4(1.0f), 0.1f, 0.7f);
}

void TennisModule::renderTranslucent(const kke::RenderContext& ctx) {
    if (m_fenceMesh) m_fenceMesh->drawTranslucent(ctx, glm::mat4(1.0f), 0.6f);
}

void TennisModule::updateBodies(float dt) {
    for (auto& m : m_matches) {
        const CourtPlace& place = m_center.courts[static_cast<size_t>(m->court)];
        for (int idx : m->players) {
            Player& p = player(idx);
            if (!p.look) continue;
            const glm::vec3 fw = place.dirToWorld(p.facing);
            const float yaw = glm::degrees(std::atan2(fw.x, fw.z));
            SwingPose sp;
            sp.kind = p.swingT < -1.5f && p.swingKind != SwingPose::Kind::Toss ? SwingPose::Kind::Ready : p.swingKind;
            if (p.tossAge >= 0.0f) sp.kind = SwingPose::Kind::Toss;
            sp.t = std::max(-1.0f, p.swingT);
            sp.contact = p.swingContact;
            Body::Mood mood = Body::Mood::Play;
            if (p.celebrate > 0.0f) mood = p.cheer ? Body::Mood::Cheer : Body::Mood::Groan;
            if (m->phase == Match::Phase::MatchOver) mood = m->score.winner() == p.team ? Body::Mood::Cheer : Body::Mood::Stand;
            p.look->update(m_rigid->world().characterPosition(p.body), yaw, m_rigid->world().characterVelocity(p.body), sp, mood, dt);
        }
    }
}

void TennisModule::updateCameras(float dt) {
    std::vector<kke::Application::View>& views = m_app->views();
    views.clear();
    kke::Camera& main = m_app->camera();
    const Match* m = m_matches.empty() ? nullptr : m_matches.front().get();
    const CourtPlace& place = m_center.courts[static_cast<size_t>(m ? m->court : 0)];
    const float k = 1.0f - std::exp(-3.5f * dt);
    // Nobody at this screen plays (the menu, a CPU match, the ball test):
    // the TV camera.
    std::vector<Player*> humans;
    if (m && !m_inMenu)
        for (int idx : m->players)
            if (!player(idx).cpu && !player(idx).remote) humans.push_back(&player(idx));
    std::sort(humans.begin(), humans.end(), [](const Player* a, const Player* b) { return a->input < b->input; });
    if (humans.empty()) {
        glm::vec3 ball = m && m->ball ? m->ball->position() : glm::vec3(0.0f);
        if (m_testBall) ball = m_testBall->position();
        // The TV view: high behind one end, the whole court in sight over
        // the near fence, drifting a little with the ball.
        const glm::vec3 want = place.toWorld({ ball.x * 0.25f, 13.0f, kHalfLength + 10.0f });
        const glm::vec3 look = place.toWorld({ ball.x * 0.2f, 0.0f, 6.0f }); // aimed so both baselines and a few steps behind them fit
        main.position = m_broadcastInit ? main.position + (want - main.position) * k : want;
        main.target = m_broadcastInit ? main.target + (look - main.target) * k : look;
        m_broadcastInit = true;
        return;
    }
    const std::vector<kke::ViewRect> rects = kke::splitScreen(static_cast<int>(humans.size()), true);
    for (size_t i = 0; i < humans.size(); ++i) {
        Player& p = *humans[i];
        const int side = m->score.sideOf(p.team);
        const float s = static_cast<float>(side);
        // Behind the player's own baseline, high enough to see over the net.
        const float depth = humans.size() > 2 ? 7.5f : 8.5f;
        const glm::vec3 wantL(p.feet.x * 0.55f, 4.6f, s * (kHalfLength + depth));
        const glm::vec3 lookL(p.feet.x * 0.3f + m->ball->position().x * 0.15f, 0.4f, -s * 2.0f);
        kke::Camera& cam = i == 0 ? main : p.camera;
        if (&cam != &main) {
            cam.fovDegrees = main.fovDegrees;
            cam.nearPlane = main.nearPlane;
            cam.farPlane = main.farPlane;
        }
        // Changing ends: swing round rather than cut (a cut after "change ends" confuses).
        const bool snap = !p.cameraInit;
        const float kk = p.camSide != side ? 1.0f - std::exp(-2.0f * dt) : k;
        const glm::vec3 wantW = place.toWorld(wantL), lookW = place.toWorld(lookL);
        cam.position = snap ? wantW : cam.position + (wantW - cam.position) * kk;
        cam.target = snap ? lookW : cam.target + (lookW - cam.target) * kk;
        if (glm::length(cam.position - wantW) < 0.3f) p.camSide = side;
        p.cameraInit = true;
        if (humans.size() > 1) views.push_back({ cam, rects[i] });
    }
}

} // namespace tennis
