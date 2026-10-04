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
#include <cstdlib>
#include <string>

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
        // The net: posts and a white tape along the sagging top (the net
        // itself is cloth: NetCloth.cpp).
        for (float x : { -kPostX, kPostX }) appendBox(p, { x, kNetHeightPost * 0.5f + 0.02f, 0.0f }, { 0.04f, kNetHeightPost * 0.5f + 0.02f, 0.04f }, post, cv, ci);
        constexpr int kTape = 12;
        for (int i = 0; i < kTape; ++i) {
            const float x0 = -kPostX + 2.0f * kPostX * static_cast<float>(i) / kTape;
            const float x1 = -kPostX + 2.0f * kPostX * static_cast<float>(i + 1) / kTape;
            const float y0 = netHeight(x0), y1 = netHeight(x1);
            appendBox(p, { 0.5f * (x0 + x1), 0.5f * (y0 + y1) - 0.03f, 0.0f }, { 0.5f * (x1 - x0) + 0.005f, 0.03f, 0.012f }, line, cv, ci);
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
                solid(p, base + glm::vec3(0.0f, 5.0f, 0.0f), { 0.1f, 5.0f, 0.1f }); // nobody walks through it
                appendBox(p, base + glm::vec3(0.0f, 10.1f, 0.0f), { 0.5f, 0.2f, 0.25f }, { 0.9f, 0.9f, 0.8f }, sv, si);
            }
        // The spectators' benches, behind the seats where the crowd sits
        // (to look at: the seat itself is where their feet go).
        for (const SportCenter::Seat& seat : m_center.seats(c)) {
            if (!seat.sitting) continue;
            const float yr = glm::radians(seat.yawDegrees);
            const glm::vec3 back = -glm::vec3(std::sin(yr), 0.0f, std::cos(yr));
            const glm::vec3 at = p.toLocal(seat.pos + back * 0.32f);
            appendBox(p, { at.x, 0.21f, at.z }, { 0.2f, 0.21f, 0.55f }, bench, sv, si);
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
    if (m_markMesh && !m_markIdx.empty()) m_markMesh->draw(ctx, glm::mat4(1.0f), 0.0f, 0.8f);
    if (m_ballMesh && !m_ballIdx.empty()) m_ballMesh->draw(ctx, glm::mat4(1.0f), 0.0f, 0.85f); // felt: rough
    if (m_netMesh && !m_netIdx.empty()) m_netMesh->draw(ctx, glm::mat4(1.0f), 0.0f, 0.8f);
}

void TennisModule::renderTranslucent(const kke::RenderContext& ctx) {
    if (m_shadowMesh && !m_shadowIdx.empty()) m_shadowMesh->drawTranslucent(ctx, glm::mat4(1.0f), 0.95f);
    if (m_fenceMesh) m_fenceMesh->drawTranslucent(ctx, glm::mat4(1.0f), 0.6f);
}

void TennisModule::updateBodies(float dt) {
    for (auto& m : m_matches) {
        const CourtPlace& place = m_center.courts[static_cast<size_t>(m->court)];
        const Replay* replay = m->replay && m->replay->playing ? m->replay.get() : nullptr;
        for (size_t i = 0; i < m->players.size(); ++i) {
            const int idx = m->players[i];
            Player& p = player(idx);
            if (!p.look) continue;
            if (replay && i < replay->frames[0].who.size()) {
                // A replay: everyone as they were at that moment.
                // (Between two frames: a slow replay draws four times for each.)
                const Replay::Who& w = replay->frame(replay->at).who[i];
                const Replay::Who& n = replay->frame(std::min(replay->at + 1, replay->to)).who[i];
                const float u = replay->part;
                const float yaw = w.yaw + std::remainder(n.yaw - w.yaw, 360.0f) * u;
                SwingPose sp{ w.stroke, w.backhand, w.swingT, w.contact, w.tossing };
                if (n.stroke == w.stroke && n.backhand == w.backhand) sp.t += (n.swingT - w.swingT) * u;
                p.look->update(glm::mix(w.feet, n.feet, u), yaw, glm::mix(w.vel, n.vel, u), sp, Body::Mood::Play, dt);
                continue;
            }
            const glm::vec3 fw = place.dirToWorld(p.facing);
            const float yaw = glm::degrees(std::atan2(fw.x, fw.z));
            SwingPose sp;
            sp.stroke = p.stroke;
            sp.backhand = p.backhand;
            sp.t = p.swingT;
            sp.contact = p.swingContact;
            sp.tossing = p.tossAge >= 0.0f;
            if (!m_poseTest.empty() && m->players[static_cast<size_t>(std::max(0, m_closeUp))] == idx) {
                // KKE_TENNIS_POSE=<stroke>[,b],<t>: this player held in one moment of a stroke (screenshots).
                const size_t c1 = m_poseTest.find(','), c2 = m_poseTest.rfind(',');
                const std::string name = m_poseTest.substr(0, c1);
                for (int k = 0; k < static_cast<int>(Stroke::Count); ++k)
                    if (name == strokeName(static_cast<Stroke>(k))) sp.stroke = static_cast<Stroke>(k);
                sp.backhand = m_poseTest.find(",b,") != std::string::npos;
                sp.t = c2 != std::string::npos ? std::strtof(m_poseTest.c_str() + c2 + 1, nullptr) : 0.0f;
                sp.contact = glm::vec3(0.0f);
                sp.tossing = false;
            }
            Body::Mood mood = Body::Mood::Play;
            if (p.celebrate > 0.0f) mood = p.cheer ? Body::Mood::Cheer : Body::Mood::Groan;
            if (m->phase == Match::Phase::MatchOver) mood = m->score.winner() == p.team ? Body::Mood::Cheer : Body::Mood::Stand;
            // Drawn between the last two physics steps (no 60 Hz shake).
            p.look->update(m_rigid->world().characterDrawPosition(p.body, m_app->fixedAlpha()), yaw, m_rigid->world().characterVelocity(p.body), sp, mood, dt);
            if (m_stringTest && p.look->stringDepth() > 0.0f)
                kke::log::get(name())->info("strings: player {} pocket {:.1f} mm", idx, p.look->stringDepth() * 1000.0f);
        }
    }
    updateWalkerBodies(dt);
}

namespace {
constexpr float kLookSpeed = 200.0f; // degrees a second, the right stick held over
constexpr float kCamPitch = -11.0f;  // looking a little down, over the net
} // namespace

void TennisModule::updateCameras(float dt) {
    std::vector<kke::Application::View>& views = m_app->views();
    views.clear();
    kke::Camera& main = m_app->camera();
    // A replay fills the screen, whoever plays: the ball camera.
    for (auto& m : m_matches)
        if (replayCamera(*m, dt / m_app->timeScale(), main)) return;
    const float k = 1.0f - std::exp(-3.5f * dt);
    // Everyone at this screen: in a match (behind their baseline), or
    // walking the sport center (behind them), in the order of their controllers.
    struct Seen { int input; Player* p; Match* m; kke::Camera* walkerCam; };
    std::vector<Seen> seen;
    if (!m_inMenu) {
        for (auto& m : m_matches)
            for (int idx : m->players)
                // (KKE_TENNIS_AUTOPLAY: the CPU plays for the person here, who keeps their camera.)
                if ((!player(idx).cpu || (m_autoplay && player(idx).input >= 0)) && !player(idx).remote) seen.push_back({ player(idx).input, &player(idx), m.get(), nullptr });
        std::vector<kke::Camera*> walkerCams;
        updateWalkerCameras(dt, walkerCams);
        size_t wc = 0;
        for (const Walker& w : m_walkers)
            if (!w.cpu && !w.remote && !w.gone && w.playing < 0 && w.body && wc < walkerCams.size()) seen.push_back({ w.input, nullptr, nullptr, walkerCams[wc++] });
    }
    std::sort(seen.begin(), seen.end(), [](const Seen& a, const Seen& b) { return a.input < b.input; });
    if (seen.empty()) {
        const Match* m = m_matches.empty() ? nullptr : m_matches.front().get();
        if (m_inCenter && !m_inMenu && m_bench) {
            benchCamera(dt, main);
            return;
        }
        if (m_inCenter && !m_inMenu) {
            // Nobody here plays: a slow turn above the whole sport center.
            m_overviewYaw += dt * 4.0f;
            const float a = glm::radians(m_overviewYaw);
            const glm::vec3 want(std::sin(a) * 62.0f, 34.0f, std::cos(a) * 62.0f);
            main.position = m_broadcastInit ? main.position + (want - main.position) * k : want;
            main.target = glm::vec3(0.0f, 0.0f, 0.0f);
            m_broadcastInit = true;
            return;
        }
        const CourtPlace& place = m_center.courts[static_cast<size_t>(m ? m->court : 0)];
        if (m && m_closeUp >= 0 && m_closeUp < static_cast<int>(m->players.size())) {
            // KKE_TENNIS_CLOSEUP: side on to one player, close, for looking at the strokes.
            const Player& p = player(m->players[static_cast<size_t>(m_closeUp)]);
            const float s = static_cast<float>(m->score.sideOf(p.team));
            const float d = m_closeUpDistance / 3.8f;
            main.position = place.toWorld(p.feet + glm::vec3(-3.6f * s * d, 1.1f + 0.4f * std::abs(d), -1.2f * s * d));
            main.target = place.toWorld(p.feet + glm::vec3(0.0f, 1.1f, 0.0f));
            return;
        }
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
    const std::vector<kke::ViewRect> rects = kke::splitScreen(static_cast<int>(seen.size()), true);
    int playing = 0;
    for (const Seen& s : seen) playing += s.p ? 1 : 0;
    for (size_t i = 0; i < seen.size(); ++i) {
        kke::Camera* src = seen[i].walkerCam;
        if (seen[i].p) {
            Player& p = *seen[i].p;
            const Match& m = *seen[i].m;
            const CourtPlace& place = m_center.courts[static_cast<size_t>(m.court)];
            const int side = m.score.sideOf(p.team);
            const float s = static_cast<float>(side);
            // Close behind the player, a little above (kke::CameraRig, like
            // Climb Race): the ball comes toward you, so you see it come and
            // can get the racket back in time. The right stick looks round;
            // let go and it settles back to looking over the net (and swings
            // round when ends change).
            const glm::vec3 toNet = place.dirToWorld({ 0.0f, 0.0f, -s });
            const float home = glm::degrees(std::atan2(toNet.x, -toNet.z));
            glm::vec2 look(0.0f);
            if (p.input >= 0) look = m_input->map(p.input).axis2("look.rate");
            p.idleLook = glm::length(look) > 0.05f ? 0.0f : p.idleLook + dt;
            if (p.camSide != side) p.idleLook = 10.0f;
            p.camSide = side;
            p.rig.addLook(look.x * kLookSpeed * dt, look.y * kLookSpeed * 0.7f * dt);
            if (!p.cameraInit) {
                p.rig.yaw = home;
                p.rig.pitch = kCamPitch;
            } else if (p.idleLook > 0.8f) {
                const float back = 1.0f - std::exp(-2.5f * dt);
                p.rig.yaw += std::remainder(home - p.rig.yaw, 360.0f) * back;
                p.rig.pitch += (kCamPitch - p.rig.pitch) * back;
            }
            p.rig.settings.armLength = playing > 2 ? 4.8f : 4.2f;
            p.rig.settings.pivotHeight = 2.0f;
            p.rig.settings.shoulderOffset = 0.45f; // over the shoulder: the ball coming in isn't behind you
            p.rig.settings.positionLag = 8.0f;
            p.rig.settings.pitchMin = -45.0f;
            p.rig.settings.pitchMax = 20.0f;
            p.rig.settings.fovDegrees = main.fovDegrees;
            kke::RigidWorld& world = m_rigid->world();
            p.rig.update(dt, world.characterDrawPosition(p.body, m_app->fixedAlpha()),
                         [&world](const glm::vec3& from, const glm::vec3& dir, float maxDist) {
                             const auto hit = world.raycast(from, dir, maxDist);
                             return hit.hit ? hit.distance : maxDist;
                         },
                         p.camera);
            p.cameraInit = true;
            src = &p.camera;
        }
        kke::Camera& cam = i == 0 ? main : *src;
        if (&cam != &main) {
            cam.fovDegrees = main.fovDegrees;
            cam.nearPlane = main.nearPlane;
            cam.farPlane = main.farPlane;
        } else {
            main.position = src->position;
            main.target = src->target;
        }
        if (seen.size() > 1) views.push_back({ cam, rects[i] });
    }
}

} // namespace tennis
