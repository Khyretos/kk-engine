// What helps you read the ball: a soft shadow on the court straight under
// it (smaller and darker the lower it is), and, while a shot comes at
// someone at this screen, a ring where it will land (it stays a moment
// after the bounce, shrinking). The ball's own shadow from the sun is a
// few pixels in the shadow map, too small to judge a bounce by.

#include "TennisModule.h"

#include "kke/SphereImpostors.h"

#include <glm/gtc/constants.hpp>

#include <cmath>

namespace tennis {

namespace {
constexpr int kSegments = 24;
constexpr float kLift = 0.035f;     // over the court and its lines (Scene.cpp: lines top out at 3 cm)
constexpr float kRing = 0.24f;      // the landing ring's radius, m
constexpr float kRingWidth = 0.035f;
constexpr float kRingAfter = 0.6f;  // s it stays after the bounce

// A disc of `radius` around `centre` (world), darkest in the middle.
void addShadow(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx, const glm::vec3& centre, float radius, float density) {
    const uint32_t base = static_cast<uint32_t>(v.size());
    const glm::vec3 up(0.0f, 1.0f, 0.0f), grey(0.18f);
    v.push_back({ centre, grey, up, { density, 0.0f } });
    for (int i = 0; i < kSegments; ++i) {
        const float a = glm::two_pi<float>() * static_cast<float>(i) / kSegments;
        v.push_back({ centre + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * radius, grey, up, { 0.0f, 0.0f } });
    }
    for (int i = 0; i < kSegments; ++i) {
        idx.push_back(base);
        idx.push_back(base + 1 + static_cast<uint32_t>((i + 1) % kSegments));
        idx.push_back(base + 1 + static_cast<uint32_t>(i));
    }
}

void addRing(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx, const glm::vec3& centre, float radius) {
    const uint32_t base = static_cast<uint32_t>(v.size());
    const glm::vec3 up(0.0f, 1.0f, 0.0f), colour(1.0f, 0.92f, 0.3f);
    for (int i = 0; i < kSegments; ++i) {
        const float a = glm::two_pi<float>() * static_cast<float>(i) / kSegments;
        const glm::vec3 d(std::cos(a), 0.0f, std::sin(a));
        v.push_back({ centre + d * (radius - kRingWidth), colour, up, { 0.6f, 0.0f } });
        v.push_back({ centre + d * radius, colour, up, { 0.6f, 0.0f } });
    }
    for (int i = 0; i < kSegments; ++i) {
        const uint32_t a = base + static_cast<uint32_t>(2 * i), b = base + static_cast<uint32_t>(2 * ((i + 1) % kSegments));
        for (uint32_t k : { a, b + 1, a + 1, a, b, b + 1 }) idx.push_back(k);
    }
}
} // namespace

void TennisModule::updateMarks() {
    m_shadowVerts.clear();
    m_shadowIdx.clear();
    m_markVerts.clear();
    m_markIdx.clear();
    m_ballVerts.clear();
    m_ballIdx.clear();
    if (m_testBall) m_testBall->appendLook(m_ballVerts, m_ballIdx);
    for (auto& mp : m_matches) {
        Match& m = *mp;
        if (!m.ball) continue;
        m.ball->appendLook(m_ballVerts, m_ballIdx);
        const CourtPlace& place = m_center.courts[static_cast<size_t>(m.court)];
        const glm::vec3 b = m.ball->position();
        const float h = std::max(0.0f, b.y - kBallRadius);
        if (h < 12.0f) addShadow(m_shadowVerts, m_shadowIdx, place.toWorld({ b.x, kLift, b.z }), 0.06f + 0.035f * h, 3.0f / (1.0f + 0.8f * h));

        // The ring: only for a shot coming at someone at this screen.
        if (m.lastHitter < 0 || m.phase != Match::Phase::Rally) continue;
        bool here = false;
        for (int i : m.players) {
            const Player& p = player(i);
            if (p.remote || p.team == player(m.lastHitter).team) continue;
            if (!p.cpu || (m_autoplay && p.input >= 0)) here = true;
        }
        if (!here) continue;
        float size = 1.0f;
        if (m.sinceBounce < 0.0f) {
            const float t = m.ball->flight().timeDownTo(kBallRadius);
            if (t < 0.0f) continue;
            m.landingAt = m.ball->flight().at(t);
            m.landingShot = m.rallyShots;
        } else if (m.landingShot == m.rallyShots && m.sinceBounce < kRingAfter) {
            size = 1.0f - m.sinceBounce / kRingAfter;
        } else {
            continue;
        }
        addRing(m_markVerts, m_markIdx, place.toWorld({ m.landingAt.x, kLift + 0.005f, m.landingAt.z }), kRing * size);
    }
    if (!m_shadowMesh) {
        m_shadowMesh = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
        m_markMesh = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
        m_ballMesh = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    }
    m_ballMesh->upload(m_ballVerts, m_ballIdx);
    m_shadowMesh->upload(m_shadowVerts, m_shadowIdx);
    m_markMesh->upload(m_markVerts, m_markIdx);
}

} // namespace tennis
