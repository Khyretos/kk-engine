// The match: serving, moving, hitting, and the umpire (Rules.h) deciding
// every point from what the FEMFX ball really did (Ball.h).

#include "TennisModule.h"

#include "kke/ImpactSynth.h"
#include "kke/Log.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace tennis {

namespace {

constexpr float kRunSpeed = 5.8f, kChargeSpeed = 3.6f;
constexpr float kArmedFor = 1.3f;      // s a pressed shot waits for the ball
constexpr float kSwingTime = 0.38f;    // s of follow-through
constexpr float kLead = 0.14f;         // s of forward swing before contact
constexpr float kReachMin = 0.2f, kReachMax = 1.5f, kReachHelp = 0.45f;

float speedFor(ShotKind k, float charge) {
    switch (k) {
    case ShotKind::Flat: return 25.0f + 10.0f * charge;
    case ShotKind::Topspin: return 20.0f + 8.0f * charge;
    case ShotKind::Slice: return 15.0f + 5.0f * charge;
    case ShotKind::Lob: return 9.0f + 1.5f * charge;
    case ShotKind::Drop: return 8.0f;
    case ShotKind::Serve: return 30.0f + 16.0f * charge;
    }
    return 20.0f;
}

float netMarginFor(ShotKind k) {
    switch (k) {
    case ShotKind::Flat: return 0.18f;
    case ShotKind::Topspin: return 0.5f;
    case ShotKind::Slice: return 0.25f;
    case ShotKind::Lob: return 1.5f;
    case ShotKind::Drop: return 0.2f;
    case ShotKind::Serve: return 0.12f;
    }
    return 0.3f;
}

float spinRate(ShotKind k) {
    switch (k) {
    case ShotKind::Topspin: return 80.0f;
    case ShotKind::Flat: return 20.0f;
    case ShotKind::Slice: return -50.0f;
    case ShotKind::Lob: return 30.0f;
    case ShotKind::Drop: return -40.0f;
    case ShotKind::Serve: return 40.0f;
    }
    return 0.0f;
}

std::mt19937& rng() {
    static std::mt19937 r(12345u);
    return r;
}

float noise(float scale) {
    std::normal_distribution<float> n(0.0f, 1.0f);
    return n(rng()) * scale;
}

} // namespace

int TennisModule::serverIndex(const Match& m) const {
    const int team = m.score.servingTeam();
    const int slot = m.rules.teamSize > 1 ? m.score.servingPlayer() : 0;
    for (int idx : m.players)
        if (m_players[static_cast<size_t>(idx)].team == team && m_players[static_cast<size_t>(idx)].slot == slot) return idx;
    return m.players.empty() ? -1 : m.players[0];
}

int TennisModule::partnerOf(const Match& m, int index) const {
    const Player& p = m_players[static_cast<size_t>(index)];
    for (int idx : m.players)
        if (idx != index && m_players[static_cast<size_t>(idx)].team == p.team) return idx;
    return -1;
}

int TennisModule::nearestOpponent(const Match& m, const Player& p) const {
    int best = -1;
    float bestD = 1e9f;
    for (int idx : m.players) {
        const Player& o = m_players[static_cast<size_t>(idx)];
        if (o.team == p.team) continue;
        const float d = glm::length(o.feet - p.feet);
        if (d < bestD) {
            bestD = d;
            best = idx;
        }
    }
    return best;
}

// Doubles: of two partners, the one nearer to where the ball can be met
// goes for it (the other covers).
bool TennisModule::isMyBall(const Match& m, int index) const {
    const int partner = partnerOf(m, index);
    if (partner < 0) return true;
    const Player& p = m_players[static_cast<size_t>(index)];
    const Player& q = m_players[static_cast<size_t>(partner)];
    const Flight f = m.ball->flight();
    glm::vec3 contact;
    float when = 0.0f;
    const int side = m.score.sideOf(p.team);
    if (m.rally.bouncedOnce()) meetOnArc(f, contact, when);
    else if (!meetPoint(f, side, m.ball->bounceModel(), contact, when)) contact = f.at(0.3f);
    const float dp = glm::length(Bot::standFor(contact, side) - p.feet), dq = glm::length(Bot::standFor(contact, side) - q.feet);
    // Something in the air right by one of them: theirs.
    const float ap = glm::length(glm::vec2(f.pos.x - p.feet.x, f.pos.z - p.feet.z));
    const float aq = glm::length(glm::vec2(f.pos.x - q.feet.x, f.pos.z - q.feet.z));
    if (std::min(ap, aq) < 2.0f) return ap <= aq;
    return dp < dq || (dp == dq && index < partner);
}

void TennisModule::placeForPoint(Match& m) {
    const bool deuce = m.score.deuceCourt();
    const bool doubles = m.rules.teamSize > 1;
    const int server = serverIndex(m);
    const int serveTeam = m.score.servingTeam();
    kke::RigidWorld& w = m_rigid->world();
    const CourtPlace& place = m_center.courts[static_cast<size_t>(m.court)];
    for (int idx : m.players) {
        Player& p = player(idx);
        const int side = m.score.sideOf(p.team);
        glm::vec3 spot;
        if (idx == server) spot = servePosition(side, deuce, doubles);
        else if (p.team == serveTeam) spot = partnerPosition(side, !deuce, true);
        else if (!doubles || p.slot == (deuce ? 0 : 1)) spot = receivePosition(side, deuce, doubles);
        else spot = partnerPosition(side, deuce, false);
        p.feet = spot;
        p.vel = glm::vec3(0.0f);
        p.facing = glm::vec3(0.0f, 0.0f, -static_cast<float>(side));
        w.teleportCharacter(p.body, place.toWorld(spot) + glm::vec3(0.0f, 0.02f, 0.0f));
        w.setCharacterVelocity(p.body, glm::vec3(0.0f));
        p.armed = -1.0f;
        p.charge = 0.0f;
        p.swingT = -2.0f;
        p.swingKind = SwingPose::Kind::Ready;
        p.tossAge = -1.0f;
    }
}

void TennisModule::startPoint(Match& m) {
    const int serveTeam = m.score.servingTeam();
    const bool again = m.serveAgain;
    if (again) m.rally.serveAgain();
    else m.rally.newPoint(serveTeam, m.score.sideOf(serveTeam), m.score.deuceCourt(), m.rules.teamSize > 1);
    m.serveAgain = false;
    // Online: the host starts every serve (a client, when told: onNetEvent).
    if (authority()) {
        ++m.serial;
        if (netHost()) m_net->sendEvent(net::kEventServe, net::encode(net::Serve{ m_netMatch, m.serial, again, m.rally.secondServeNow() }));
    }
    placeForPoint(m);
    m.phase = Match::Phase::Serve;
    m.phaseTime = 0.0f;
    m.rallyShots = 0;
    m.deadBall = 0.0f;
    m.call.clear();
    m.sub = m.rally.secondServeNow() ? "Second serve" : m.score.callText();
    const Player& s = player(serverIndex(m));
    // The ball in the server's hand until the toss.
    const CourtPlace& place = m_center.courts[static_cast<size_t>(m.court)];
    m.ball->place(place.toLocal(s.look ? s.look->tossHand() : place.toWorld(s.feet + glm::vec3(0, 1.2f, 0))));
}

bool TennisModule::authority() const { return !netClient(); }

void TennisModule::resolve(Match& m, Rally::Result r, const std::string& call) {
    if (r == Rally::Result::None) return;
    m.phase = Match::Phase::PointOver;
    m.phaseTime = 0.0f;
    m.call = authority() ? m.rally.call() : call;
    if (netHost()) m_net->sendEvent(net::kEventPoint, net::encode(net::Point{ m_netMatch, m.serial, static_cast<uint8_t>(r), m.call }));
    // Every call in the log on a test run (KKE_TENNIS_QUIT).
    if (m_quitAfter > 0.0f) kke::log::get(name())->info("court {}: {} after {} shots", m.court + 1, m.call, m.rallyShots);
    if (r == Rally::Result::Fault || r == Rally::Result::Let) {
        m.serveAgain = true;
        m.sub = r == Rally::Result::Let ? "First serve again" : "Second serve";
        if (r == Rally::Result::Let) m.sub = m.rally.secondServeNow() ? "Second serve again" : "First serve again";
        return;
    }
    const int team = r == Rally::Result::PointTo0 ? 0 : 1;
    const int setsBefore = m.score.sets(team);
    const bool game = m.score.pointTo(team);
    m.lastPointTo = team;
    onPointForCrowd(m);
    ++m_pointsPlayed;
    m_longestRally = std::max(m_longestRally, m.rallyShots);
    std::string names;
    for (int idx : m.players)
        if (player(idx).team == team) names += (names.empty() ? "" : " & ") + player(idx).name;
    if (m.score.over()) m.sub = "Game, set and match " + names;
    else if (m.score.sets(team) > setsBefore) m.sub = "Game and set " + names;
    else if (game) m.sub = "Game " + names + (m.score.inTiebreak() ? " (tiebreak)" : "");
    else m.sub = m.score.callText();
    if (m.score.endsJustSwitched() && !m.score.over()) m.sub += " - change ends";
    for (int idx : m.players) {
        Player& p = player(idx);
        p.cheer = p.team == team;
        p.celebrate = 1.6f;
    }
}

void TennisModule::stepMatch(Match& m, float dt) {
    Ball& b = *m.ball;
    b.step(dt);
    m.phaseTime += dt;
    const CourtPlace& place = m_center.courts[static_cast<size_t>(m.court)];
    auto* audio = m_app->getModule<kke::AudioModule>();
    for (const Ball::Event& e : b.takeEvents()) {
        if (audio && e.kind != Ball::Event::Kind::Out)
            audio->playImpact(place.toWorld(e.at), kke::AudioMaterialTable::Rubber, std::clamp(glm::length(b.velocity()) / 25.0f, 0.15f, 1.0f));
        if (m.phase != Match::Phase::Rally) continue;
        Rally::Result r = Rally::Result::None;
        switch (e.kind) {
        case Ball::Event::Kind::Bounce: r = m.rally.onBounce(e.at.x, e.at.z); break;
        case Ball::Event::Kind::Net: m.rally.onNet(); break;
        case Ball::Event::Kind::Out: r = m.rally.onOut(); break;
        }
        if (authority()) resolve(m, r); // a client waits for the host's call
    }

    for (int idx : m.players) stepPlayer(m, player(idx), dt);

    switch (m.phase) {
    case Match::Phase::Warmup:
        if (m.phaseTime > 1.5f && authority()) startPoint(m);
        break;
    case Match::Phase::Serve:
        break; // stepPlayer runs the toss and the hit
    case Match::Phase::Rally: {
        // A ball that stopped (rolled dead against the net) ends the rally.
        const bool still = glm::length(b.velocity()) < 0.4f && b.position().y < kBallRadius * 2.5f;
        m.deadBall = still ? m.deadBall + dt : 0.0f;
        m.sinceHit += dt;
        // A ball lying still counts as bouncing twice where it lies.
        if (authority() && (m.deadBall > 1.0f || m.sinceHit > 10.0f)) {
            Rally::Result r = m.rally.onBounce(b.position().x, b.position().z);
            if (r == Rally::Result::None) r = m.rally.onBounce(b.position().x, b.position().z);
            if (r == Rally::Result::None) r = m.rally.onOut();
            resolve(m, r);
        }
        break;
    }
    case Match::Phase::PointOver:
        if (!authority()) {
            // The host says when the next serve is; the match is over when the score says so.
            if (m.score.over() && m.phaseTime > 2.2f) {
                m.phase = Match::Phase::MatchOver;
                m.phaseTime = 0.0f;
            }
        } else if (m.phaseTime > (m.serveAgain ? 1.2f : 2.2f)) {
            if (m.score.over()) {
                m.phase = Match::Phase::MatchOver;
                m.phaseTime = 0.0f;
                kke::log::get(name())->info("match over on court {}: sets {}-{} (games {} / {}), team {} wins", m.court + 1, m.score.sets(0), m.score.sets(1),
                                            m.score.setsText(0), m.score.setsText(1), m.score.winner() + 1);
            } else {
                startPoint(m);
            }
        }
        break;
    case Match::Phase::MatchOver:
        if (m.phaseTime > 8.0f && authority() && (m_allBots || m.players.empty())) {
            m.score = Score(m.rules);
            m.phase = Match::Phase::Warmup;
            m.phaseTime = 0.0f;
        }
        break;
    }
}

void TennisModule::readHuman(Match& m, Player& p) {
    if (p.input < 0) return;
    kke::InputMap& in = m_input->map(p.input);
    // The stick is in screen space: the camera sits behind the player's
    // own half, so up is toward the net.
    const int side = m.score.sideOf(p.team);
    const float s = static_cast<float>(side);
    const glm::vec2 stick = in.axis2("move");
    const glm::vec3 fwd(0.0f, 0.0f, -s), right(s, 0.0f, 0.0f);
    glm::vec3 move = right * stick.x + fwd * stick.y;
    if (glm::length(move) > 1.0f) move = glm::normalize(move);
    Intent it;
    it.move = move * kRunSpeed;
    it.aim = glm::clamp(stick, glm::vec2(-1.0f), glm::vec2(1.0f));
    struct B { const char* id; ShotKind kind; };
    for (const B& b : { B{ "tennis.topspin", ShotKind::Topspin }, B{ "tennis.flat", ShotKind::Flat }, B{ "tennis.slice", ShotKind::Slice },
                        B{ "tennis.lob", ShotKind::Lob } }) {
        if (in.pressed(b.id)) {
            it.press = true;
            it.kind = b.kind;
        }
        if (in.held(b.id)) {
            it.held = true;
            if (!it.press) it.kind = b.kind;
        }
    }
    // Presses are kept until a fixed step has seen them.
    it.press = it.press || p.intent.press;
    p.intent = it;
}

void TennisModule::thinkCpu(Match& m, Player& p, float dt) {
    if (!p.bot) return;
    const int side = m.score.sideOf(p.team);
    const int idx = static_cast<int>(&p - m_players.data());
    const int opp = nearestOpponent(m, p);
    Bot::View v;
    v.ball = m.ball->flight();
    v.ballInPlay = m.phase == Match::Phase::Rally;
    v.mayHit = m.rally.mayHit(p.team);
    v.bounced = m.rally.bouncedOnce();
    v.myTurn = isMyBall(m, idx);
    v.side = side;
    v.feet = p.feet;
    v.opponent = opp >= 0 ? player(opp).feet : glm::vec3(0.0f, 0.0f, -static_cast<float>(side) * kHalfLength);
    v.atNet = m.rules.teamSize > 1 && m.score.servingTeam() == p.team && serverIndex(m) != idx;
    v.bounce = m.ball->bounceModel();
    const Bot::Decision d = p.bot->think(v, dt);
    glm::vec3 to = d.moveTo - p.feet;
    to.y = 0.0f;
    const float dist = glm::length(to);
    const float top = p.bot->skill().speed * d.urgency;
    // Slow down onto the spot instead of overshooting it.
    const float speed = std::min(top, dist * 4.0f);
    p.intent = Intent{};
    p.intent.move = dist > 0.05f ? to / dist * speed : glm::vec3(0.0f);
    p.intent.kind = d.kind;
    p.intent.aim = d.aim;
    if (d.swing) {
        p.intent.press = true;
        p.charge = d.charge;
    }
}

void TennisModule::stepPlayer(Match& m, Player& p, float dt) {
    kke::RigidWorld& w = m_rigid->world();
    const CourtPlace& place = m_center.courts[static_cast<size_t>(m.court)];
    p.feet = place.toLocal(w.characterPosition(p.body));
    p.vel = place.dirToLocal(w.characterVelocity(p.body));
    if (p.remote) {
        stepRemote(m, p, dt);
        return;
    }
    const int idx = static_cast<int>(&p - m_players.data());
    const int side = m.score.sideOf(p.team);
    const float s = static_cast<float>(side);
    Ball& b = *m.ball;

    if (p.cpu) thinkCpu(m, p, dt);
    Intent& it = p.intent;
    const bool serving = m.phase == Match::Phase::Serve && idx == serverIndex(m);
    glm::vec3 move = it.move;

    // A pressed shot waits for the ball; holding the button charges it.
    if (it.press && m.phase == Match::Phase::Rally && p.armed < 0.0f) {
        p.armed = 0.0f;
        p.ballDist = 99.0f;
        p.armedKind = it.kind;
        if (!p.cpu) p.charge = 0.0f;
    }
    if (p.armed >= 0.0f) {
        p.armed += dt;
        if (!p.cpu && it.held) p.charge = std::min(1.0f, p.charge + dt * 1.4f);
        if (p.armed > kArmedFor) {
            // Too early: a swing at nothing.
            p.armed = -1.0f;
            p.swingKind = SwingPose::Kind::Forehand;
            p.swingContact = glm::vec3(0.6f, 1.0f, 0.4f);
            p.swingT = 0.0f;
        }
        if (!p.cpu && it.held) move *= kChargeSpeed / kRunSpeed;
    }

    // Serving: the first press tosses, the next one hits.
    if (serving) {
        move = glm::vec3(0.0f);
        if (p.tossAge < 0.0f) {
            b.place(place.toLocal(p.look ? p.look->tossHand() : place.toWorld(p.feet + glm::vec3(0.3f, 1.2f, 0.0f))));
            p.swingKind = SwingPose::Kind::Ready;
            const bool cpuToss = p.cpu && m.phaseTime > 1.1f;
            if ((it.press && !p.cpu && m.phaseTime > 0.3f) || cpuToss) {
                p.tossAge = 0.0f;
                b.place(b.position() + glm::vec3(0.0f, 0.1f, 0.0f), glm::vec3(0.0f, 5.4f, -0.35f * s));
                p.swingKind = SwingPose::Kind::Toss;
                if (online() && p.netId >= 0)
                    m_net->sendEvent(net::kEventToss, net::encode(net::Toss{ m_netMatch, m.serial, static_cast<uint8_t>(p.netId), b.position(), b.velocity() }));
                it.press = false;
                p.charge = 0.0f;
            }
        } else {
            p.tossAge += dt;
            if (!p.cpu && it.held) p.charge = std::min(1.0f, p.charge + dt * 1.6f);
            const glm::vec3 ball = b.position();
            const bool falling = b.velocity().y < 0.0f;
            bool hit = false;
            if (p.cpu) hit = falling && ball.y < 2.7f + 0.2f * p.bot->skill().power && ball.y > 1.9f;
            else hit = it.press && p.tossAge > 0.15f;
            if (hit) {
                if (p.cpu) {
                    p.charge = p.bot->skill().power;
                    it.aim = glm::vec2(noise(0.5f), 0.2f);
                }
                if (!tryHit(m, p, true)) p.swingT = 0.0f; // a swing and a miss: toss again
            }
            if (ball.y < 1.0f && falling && m.phase == Match::Phase::Serve) {
                // Nobody hit it: catch it and toss again.
                p.tossAge = -1.0f;
                p.swingT = -2.0f;
            }
        }
        it.press = false;
    } else if (m.phase == Match::Phase::Rally) {
        // Help (the menu's Assist, and always for the CPU's own feet):
        // when the stick is let go and the ball is coming, walk to meet it.
        if (!p.cpu && m_assist && glm::length(it.move) < 0.5f && m.rally.mayHit(p.team) && isMyBall(m, idx) && b.velocity().z * s > 0.0f) {
            glm::vec3 contact;
            float when = 0.0f;
            bool found = true;
            if (m.rally.bouncedOnce()) meetOnArc(b.flight(), contact, when);
            else found = meetPoint(b.flight(), side, b.bounceModel(), contact, when);
            if (found) {
                glm::vec3 to = Bot::standFor(contact, side) - p.feet;
                to.y = 0.0f;
                const float d = glm::length(to);
                if (d > 0.1f) move = to / d * std::min(kRunSpeed * 0.9f, d * 4.0f);
            }
        }
        if (p.armed >= 0.0f || (p.cpu && it.press)) {
            if (p.cpu && p.armed < 0.0f) {
                p.armed = 0.0f;
                p.ballDist = 99.0f;
                p.armedKind = it.kind;
            }
            tryHit(m, p, false);
        }
        it.press = false;
    } else {
        it.press = false;
        // Between points: walk back toward the middle of the half.
        if (m.phase == Match::Phase::PointOver || m.phase == Match::Phase::MatchOver) move = glm::vec3(0.0f);
        if (m.phase == Match::Phase::Serve || m.phase == Match::Phase::Warmup) move = glm::vec3(0.0f);
    }

    // The forward swing starts just before the ball arrives.
    if (p.armed >= 0.0f && m.phase == Match::Phase::Rally) {
        const glm::vec3 rel = b.position() - (p.feet + glm::vec3(0.0f, 1.0f, 0.0f));
        const float approach = -glm::dot(glm::normalize(glm::vec3(rel.x, 0.0f, rel.z) + glm::vec3(1e-4f)), glm::vec3(b.velocity().x, 0.0f, b.velocity().z));
        const float tc = approach > 0.5f ? (glm::length(glm::vec2(rel.x, rel.z)) - 0.8f) / approach : 1.0f;
        const bool forehand = (b.position().x - p.feet.x) * s >= -0.1f;
        p.swingKind = forehand ? SwingPose::Kind::Forehand : SwingPose::Kind::Backhand;
        p.swingContact = glm::vec3(forehand ? 0.75f : -0.7f, std::clamp(b.position().y, 0.4f, 2.0f), 0.45f);
        p.swingT = tc < kLead ? -std::max(0.0f, tc) / kLead : -1.0f;
    } else if (p.swingT >= 0.0f) {
        p.swingT += dt / kSwingTime;
        if (p.swingT > 1.0f) p.swingT = -2.0f;
    } else if (p.swingKind != SwingPose::Kind::Toss) {
        p.swingT = -2.0f;
    }

    // Facing: the net, unless running flat out somewhere.
    glm::vec3 face(0.0f, 0.0f, -s);
    const float speed = glm::length(glm::vec2(move.x, move.z));
    if (speed > 4.5f && !m_rig->sideSteps()) face = glm::normalize(glm::vec3(move.x, 0.0f, move.z));
    const float k = 1.0f - std::exp(-10.0f * dt);
    p.facing = glm::normalize(p.facing + (face - p.facing) * k + glm::vec3(0.0f, 0.0f, 1e-4f));
    kke::RigidWorld::CharacterInput ci;
    ci.move = place.dirToWorld(move);
    w.setCharacterInput(p.body, ci);
    if (p.celebrate > 0.0f) p.celebrate -= dt;
}

// Another machine's player: its feet and swing come from what it sends
// (Net.cpp); its hits and its toss come as events.
void TennisModule::stepRemote(Match& m, Player& p, float dt) {
    const CourtPlace& place = m_center.courts[static_cast<size_t>(m.court)];
    glm::vec3 face = place.dirToLocal(p.pose.facing);
    face.y = 0.0f;
    if (glm::length(face) > 1e-3f) p.facing = glm::normalize(face);
    p.swingKind = p.pose.swing == SwingPose::Kind::Toss && p.tossAge < 0.0f ? SwingPose::Kind::Ready : p.pose.swing;
    p.swingT = p.pose.swingT;
    p.swingContact = p.pose.contact;
    const bool serving = m.phase == Match::Phase::Serve && static_cast<int>(&p - m_players.data()) == serverIndex(m);
    if (serving) {
        Ball& b = *m.ball;
        // The ball in their hand until their toss comes; back in it if
        // they let it drop.
        if (p.tossAge < 0.0f) {
            b.place(place.toLocal(p.look ? p.look->tossHand() : place.toWorld(p.feet + glm::vec3(0.3f, 1.2f, 0.0f))));
        } else {
            p.tossAge += dt;
            if (b.position().y < 1.0f && b.velocity().y < 0.0f) p.tossAge = -1.0f;
        }
    }
    if (p.celebrate > 0.0f) p.celebrate -= dt;
}

bool TennisModule::tryHit(Match& m, Player& p, bool serve) {
    Ball& b = *m.ball;
    if (!m.rally.mayHit(p.team)) return false;
    const glm::vec3 ball = b.position();
    const glm::vec2 flat(ball.x - p.feet.x, ball.z - p.feet.z);
    const float d = glm::length(flat);
    const float reach = kReachMax + (!p.cpu && m_assist ? kReachHelp : 0.0f);
    if (serve) {
        if (ball.y < 1.7f || ball.y > 3.4f || d > 1.2f) return false;
    } else {
        const float was = p.ballDist;
        p.ballDist = d;
        if (d < kReachMin * 0.5f || d > reach || ball.y < 0.08f || ball.y > 2.9f) return false;
        // Not once it has gone past behind the player.
        const float s = static_cast<float>(m.score.sideOf(p.team));
        const float forward = -(ball.z - p.feet.z) * s;
        if (forward < -0.9f) return false;
        // Wait for it to reach the hitting spot, a little in front, or to
        // start going away (the last chance).
        if (forward > 0.5f && d < was + 1e-4f) return false;
    }
    hitBall(m, p, ball, serve);
    return true;
}

void TennisModule::hitBall(Match& m, Player& p, const glm::vec3& contact, bool serve) {
    const int side = m.score.sideOf(p.team);
    const float s = static_cast<float>(side);
    const bool doubles = m.rules.teamSize > 1;
    const ShotKind kind = serve ? ShotKind::Serve : p.armedKind;
    // Timing: the ball by the right hip, a little in front, waist high, is
    // perfect; the further from that, the wilder.
    const glm::vec3 rel((contact.x - p.feet.x) * s, contact.y, -(contact.z - p.feet.z) * s); // x right, z forward
    const bool forehand = rel.x >= -0.1f;
    float quality = 1.0f;
    if (serve) {
        quality = std::clamp(1.0f - std::abs(contact.y - 2.75f) / 0.9f, 0.2f, 1.0f);
    } else {
        // Spacing: an arm and a racket to the side, between knee and
        // shoulder, beside or just in front; each a band, not a point.
        auto band = [](float v, float lo, float hi, float falloff) {
            const float out = v < lo ? lo - v : v > hi ? v - hi : 0.0f;
            return std::max(0.0f, 1.0f - out / falloff);
        };
        quality = band(std::abs(rel.x), 0.5f, 1.05f, 0.7f) * band(rel.y, 0.55f, 1.55f, 1.0f) * band(rel.z, -0.1f, 0.6f, 0.9f);
        // Timing: pressed a moment before it came (a human's press; the
        // CPU's by its skill).
        if (p.cpu) quality *= 1.0f - std::abs(noise(0.25f * (1.0f - p.bot->skill().power)));
        else quality *= band(p.armed, 0.08f, 0.7f, 0.9f);
        quality = std::clamp(quality, 0.15f, 1.0f);
    }
    if (!p.cpu && m_assist) quality = std::max(quality, 0.7f);
    // Early (in front) pulls it across the body, late pushes it wide.
    const float early = serve ? 0.0f : std::clamp((rel.z - 0.4f) / 0.6f, -1.0f, 1.0f);

    glm::vec2 aim = glm::clamp(p.intent.aim, glm::vec2(-1.0f), glm::vec2(1.0f));
    glm::vec3 target;
    // Long rallies wear the CPU down: the aim wanders a little more with
    // every shot, so a rally between two good CPU players ends (real ones
    // average four or five shots).
    const float tired = p.bot ? 0.1f * static_cast<float>(std::max(0, m.rallyShots - 3)) : 0.0f;
    const float err = (1.0f - quality) * 2.2f + (p.bot ? p.bot->skill().aimError : 0.4f) + tired;
    if (serve) {
        const bool deuce = m.score.deuceCourt();
        const float centre = deuce ? -2.05f : 2.05f; // x * s of the box's middle
        const float x = std::clamp(centre + aim.x * 1.4f + noise(err * 0.5f), centre - 1.8f, centre + 1.8f);
        const float depth = std::clamp(4.6f + aim.y * 0.9f + noise(err * 0.4f), 3.0f, kServiceLine + 0.6f);
        target = { x * s, 0.0f, -s * depth };
    } else {
        const float half = (doubles ? kDoublesHalfWidth : kSinglesHalfWidth) - 0.6f;
        float depth = 7.6f + aim.y * 1.9f;
        if (kind == ShotKind::Drop) depth = 2.6f + aim.y * 0.5f;
        if (kind == ShotKind::Lob) depth = 9.6f + aim.y * 1.2f;
        const float x = aim.x * half + (forehand ? -1.0f : 1.0f) * early * 1.6f + noise(err);
        target = { x * s, 0.0f, -s * (depth + noise(err * 0.8f)) };
    }
    const float charge = std::clamp(p.charge, 0.0f, 1.0f);
    float speed = speedFor(kind, charge) * (0.7f + 0.3f * quality);
    if (serve && m.rally.secondServeNow()) speed *= 0.8f;
    // Slow shots from far away still get there in time (a drop shot from
    // the baseline is a hard push, not a moon ball).
    const float longest = kind == ShotKind::Lob ? 2.4f : 1.5f;
    speed = std::max(speed, glm::length(glm::vec2(target.x - contact.x, target.z - contact.z)) / longest);
    const ShotKind flight = serve ? ShotKind::Serve : kind;
    ShotPlan plan = planShot(contact, target, speed, flight, netMarginFor(kind) * (0.6f + 0.6f * quality));
    // A mistimed shot also comes off the frame a bit: a little into the net
    // or long.
    plan.velocity *= 1.0f + noise((1.0f - quality) * 0.06f);
    glm::vec3 dir(plan.velocity.x, 0.0f, plan.velocity.z);
    dir = glm::length(dir) > 1e-3f ? glm::normalize(dir) : glm::vec3(0.0f, 0.0f, -s);
    const glm::vec3 spin = glm::cross(glm::vec3(0, 1, 0), dir) * spinRate(kind);
    p.armed = -1.0f;
    p.charge = 0.0f;
    p.swingKind = serve ? SwingPose::Kind::Serve : (forehand ? SwingPose::Kind::Forehand : SwingPose::Kind::Backhand);
    p.swingContact = glm::vec3(rel.x, contact.y, std::max(0.2f, rel.z));
    p.swingT = 0.0f;

    net::Hit h;
    h.match = m_netMatch;
    h.point = m.serial;
    h.player = static_cast<uint8_t>(std::max(0, p.netId));
    h.shot = static_cast<uint8_t>(std::min(m.rallyShots, 255));
    h.serve = serve;
    h.kind = static_cast<uint8_t>(kind);
    h.at = contact;
    h.velocity = plan.velocity;
    h.spin = spin;
    h.pull = plan.gravity - kGravity;
    h.squash = 0.25f + 0.75f * std::min(1.0f, speed / 45.0f);
    if (online() && p.netId >= 0) {
        // Everyone flies the ball from the same numbers: ours too go
        // through the wire's rounding.
        const std::vector<uint8_t> bytes = net::encode(h);
        if (const auto back = net::decodeHit(bytes)) h = *back;
        m_net->sendEvent(net::kEventHit, bytes);
    }
    applyHit(m, static_cast<int>(&p - m_players.data()), h);
}

// A hit, from this machine or another (online): the ball goes, the
// rally counts it.
void TennisModule::applyHit(Match& m, int hitter, const net::Hit& h) {
    Ball& b = *m.ball;
    b.strikeAt(h.at, h.velocity, h.spin, h.pull, h.squash);
    if (auto* audio = m_app->getModule<kke::AudioModule>())
        audio->playImpact(m_center.courts[static_cast<size_t>(m.court)].toWorld(h.at), kke::AudioMaterialTable::Plastic,
                          std::clamp(glm::length(h.velocity) / 40.0f, 0.3f, 1.0f));
    m.rallyShots = h.shot + 1;
    m.sinceHit = 0.0f;
    if (h.serve) {
        m.phase = Match::Phase::Rally;
        m.phaseTime = 0.0f;
    }
    const int team = player(hitter).team;
    for (int idx : m.players) {
        Player& q = player(idx);
        if (idx == hitter) q.tossAge = -1.0f;
        if (q.team != team && q.bot) q.bot->onOpponentHit();
    }
    const Rally::Result r = m.rally.onHit(team);
    if (authority()) resolve(m, r);
}

} // namespace tennis
