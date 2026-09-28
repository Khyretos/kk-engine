// Replays: the last seconds of a match are kept, one frame per fixed step
// (the ball's flight and every player's feet, facing and swing), and a
// winner or an ace is shown again in slow motion from beside the ball: the
// hit, the flight and the landing. The ball flies again from where it was
// (its flight is our own maths, so it goes exactly the same way) and is
// struck again at the same moment, so FEMFX squashes the ball and the
// strings again, now slow enough to see (Application::setTimeScale).
//
// Only offline, with one match: slowing the game down slows every court
// and every other screen's game with it.

#include "TennisModule.h"

#include "kke/Log.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <algorithm>
#include <cmath>

namespace tennis {

namespace {
constexpr float kSlow = 0.25f;         // the replay's speed
constexpr float kBefore = 0.7f;        // s before the hit it starts
constexpr float kAfterBounce = 0.6f;   // s after the landing it ends
} // namespace

bool TennisModule::replaysOn(const Match& m) const {
    (void)m;
    return m_replays && !online() && !m_inCenter && m_matches.size() == 1 && m_poseTest.empty() && m_closeUp < 0 && !m_ballTest;
}

void TennisModule::recordReplay(Match& m) {
    if (!replaysOn(m)) {
        m.replay.reset();
        return;
    }
    if (!m.replay) m.replay = std::make_unique<Replay>();
    Replay& r = *m.replay;
    Replay::Frame& f = r.frames[r.tick % Replay::kFrames];
    f.ball = m.ball->state();
    const CourtPlace& place = m_center.courts[static_cast<size_t>(m.court)];
    for (size_t i = 0; i < f.who.size() && i < m.players.size(); ++i) {
        const Player& p = player(m.players[i]);
        Replay::Who& w = f.who[i];
        w.feet = m_rigid->world().characterPosition(p.body);
        w.vel = m_rigid->world().characterVelocity(p.body);
        const glm::vec3 fw = place.dirToWorld(p.facing);
        w.yaw = glm::degrees(std::atan2(fw.x, fw.z));
        w.stroke = p.stroke;
        w.backhand = p.backhand;
        w.swingT = p.swingT;
        w.contact = p.swingContact;
        w.tossing = p.tossAge >= 0.0f;
    }
    ++r.tick;
}

void TennisModule::queueReplay(Match& m, int winningTeam) {
    if (!m.replay || m.replay->hits.empty() || m.lastHitter < 0 || player(m.lastHitter).team != winningTeam) return;
    Replay& r = *m.replay;
    if (r.tick < 2) return;
    // The point was won by the last shot: a winner, or an ace.
    const uint32_t last = r.tick - 1; // the latest frame kept
    const uint32_t oldest = r.tick > Replay::kFrames ? r.tick - Replay::kFrames + 1 : 0;
    const uint32_t hit = r.hits.back().tick;
    const uint32_t before = static_cast<uint32_t>(kBefore * 60.0f), after = static_cast<uint32_t>(kAfterBounce * 60.0f);
    r.from = std::max(oldest, hit > before ? hit - before : 0u);
    r.to = r.bounceTick > hit ? std::min(last, r.bounceTick + after) : last;
    r.pending = r.to > r.from;
}

void TennisModule::startReplay(Match& m) {
    Replay& r = *m.replay;
    r.pending = false;
    r.playing = true;
    r.at = r.from;
    r.part = 0.0f;
    r.camInit = false;
    m.ball->setState(r.frame(r.from).ball);
    r.sub = m.sub;
    m.sub = "Replay";
    m_app->setTimeScale(kSlow);
    kke::log::get(name())->info("replay: court {}, {:.2f} s of play in slow motion", m.court + 1, static_cast<float>(r.to - r.from) / 60.0f);
}

void TennisModule::stepReplay(Match& m, float dt) {
    Replay& r = *m.replay;
    Ball& b = *m.ball;
    b.step(dt);
    const CourtPlace& place = m_center.courts[static_cast<size_t>(m.court)];
    auto* audio = m_app->getModule<kke::AudioModule>();
    for (const Ball::Event& e : b.takeEvents())
        if (audio && e.kind != Ball::Event::Kind::Out)
            audio->playImpact(place.toWorld(e.at), kke::AudioMaterialTable::Rubber, std::clamp(glm::length(b.velocity()) / 25.0f, 0.15f, 1.0f));
    // Each step is a quarter of a recorded one: the frames advance every
    // fourth step, the ball flies on in between.
    r.part += m_app->timeScale();
    while (r.part >= 1.0f - 1e-4f && r.playing) {
        r.part = std::max(0.0f, r.part - 1.0f);
        ++r.at;
        for (const Replay::Hit& h : r.hits) {
            if (h.tick != r.at) continue;
            // The same hit again: the ball, the strings (FEMFX both), the sound.
            if (Player& p = player(h.hitter); p.look) p.look->hitStrings(place.toWorld(h.hit.at), place.dirToWorld(b.velocity()));
            b.strikeAt(h.hit.at, h.hit.velocity, h.hit.spin, h.hit.pull, h.hit.squash);
            if (audio)
                audio->playImpact(place.toWorld(h.hit.at), kke::AudioMaterialTable::Plastic, std::clamp(glm::length(h.hit.velocity) / 40.0f, 0.3f, 1.0f));
        }
        if (r.at >= r.to) endReplay(m);
    }
}

void TennisModule::endReplay(Match& m) {
    Replay& r = *m.replay;
    if (!r.playing) return;
    r.playing = false;
    // The ball back where the point left it.
    m.ball->setState(r.frame(r.tick - 1).ball);
    m.sub = r.sub;
    m_app->setTimeScale(1.0f);
}

bool TennisModule::replayCamera(Match& m, float realDt, kke::Camera& cam) {
    if (!m.replay || !m.replay->playing) return false;
    Replay& r = *m.replay;
    const CourtPlace& place = m_center.courts[static_cast<size_t>(m.court)];
    // First beside the hitter, a little in front, watching the ball come
    // in and the swing meet it; after the hit, off after the ball, beside
    // and behind it, to its landing; once it has landed the camera stays
    // and watches the mark. The moves between are smoothed into a swoop.
    const Replay::Hit& last = r.hits.back();
    const bool landed = r.bounceTick > last.tick && r.at >= r.bounceTick;
    glm::vec3 along = last.hit.velocity; // the way the winner went
    along.y = 0.0f;
    along = glm::length(along) > 0.1f ? glm::normalize(along) : glm::vec3(0.0f, 0.0f, -1.0f);
    // The side the camera stays on: the one toward the court's middle
    // line from where it was hit, so it looks across the court.
    glm::vec3 side(along.z, 0.0f, -along.x);
    const glm::vec3 hitAt = last.hit.at;
    if (glm::dot(side, glm::vec3(-hitAt.x, 0.0f, 0.0f)) < 0.0f) side = -side;
    glm::vec3 want, look;
    if (r.at + 4 < last.tick) {
        want = hitAt + side * 4.0f + along * 2.8f;
        want.y = 1.6f;
        look = { hitAt.x, 1.0f, hitAt.z };
    } else {
        const glm::vec3 ball = landed ? r.frame(r.bounceTick).ball.pos : m.ball->position();
        want = ball - along * 3.0f + side * 2.4f + glm::vec3(0.0f, 0.9f, 0.0f);
        look = ball;
    }
    const float k = r.camInit ? 1.0f - std::exp(-4.0f * realDt) : 1.0f;
    r.camPos += (want - r.camPos) * k; // court space
    r.camLook += (look - r.camLook) * k;
    r.camInit = true;
    // Inside the fences, and over the net, never through it (2 m up
    // where it crosses; the net is 1.07 m at the posts).
    glm::vec3 at = r.camPos;
    at.x = std::clamp(at.x, -kFenceHalfX + 0.4f, kFenceHalfX - 0.4f);
    at.z = std::clamp(at.z, -kFenceHalfZ + 0.4f, kFenceHalfZ - 0.4f);
    const float nearNet = std::clamp(1.0f - std::abs(at.z) / 3.0f, 0.0f, 1.0f);
    at.y = std::max(at.y, 0.8f + 1.2f * nearNet);
    cam.position = place.toWorld(at);
    cam.target = place.toWorld(r.camLook);
    return true;
}

} // namespace tennis
