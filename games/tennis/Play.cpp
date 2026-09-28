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

// The flight a stroke gives the ball (the button picked the shot; where
// it was met can change it: an overhead is flat, a stretch is a slice).
ShotKind flightKind(Stroke s, ShotKind asked) {
    switch (s) {
    case Stroke::Drive: return ShotKind::Topspin;
    case Stroke::Flat:
    case Stroke::Smash: return ShotKind::Flat;
    case Stroke::Slice:
    case Stroke::Stretch: return ShotKind::Slice;
    case Stroke::Lob: return ShotKind::Lob;
    case Stroke::Drop: return ShotKind::Drop;
    case Stroke::Volley: return asked == ShotKind::Topspin ? ShotKind::Flat : asked;
    case Stroke::HalfVolley: return asked == ShotKind::Lob ? ShotKind::Lob : ShotKind::Topspin;
    default: return ShotKind::Serve;
    }
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
        endSwing(p);
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
        if (netHost() && m.netId) m_net->sendEvent(net::kEventServe, net::encode(net::Serve{ m.netId, m.serial, again, m.rally.secondServeNow() }));
    }
    placeForPoint(m);
    m.phase = Match::Phase::Serve;
    m.phaseTime = 0.0f;
    m.rallyShots = 0;
    m.deadBall = 0.0f;
    m.sinceBounce = -1.0f;
    m.call.clear();
    m.sub = m.rally.secondServeNow() ? "Second serve" : m.score.callText();
    const Player& s = player(serverIndex(m));
    // The ball in the server's hand until the toss.
    m.ball->place(tossHand(m, s));
}

bool TennisModule::authority() const { return !netClient(); }

void TennisModule::resolve(Match& m, Rally::Result r, const std::string& call) {
    if (r == Rally::Result::None) return;
    m.phase = Match::Phase::PointOver;
    m.phaseTime = 0.0f;
    m.call = authority() ? m.rally.call() : call;
    if (netHost() && m.netId) m_net->sendEvent(net::kEventPoint, net::encode(net::Point{ m.netId, m.serial, static_cast<uint8_t>(r), m.call }));
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
    queueReplay(m, team);
    m.history.push_back(static_cast<uint8_t>(team));
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
    if (m.sinceBounce >= 0.0f) m.sinceBounce += dt;
    const CourtPlace& place = m_center.courts[static_cast<size_t>(m.court)];
    auto* audio = m_app->getModule<kke::AudioModule>();
    for (const Ball::Event& e : b.takeEvents()) {
        if (audio && e.kind != Ball::Event::Kind::Out)
            audio->playImpact(place.toWorld(e.at), kke::AudioMaterialTable::Rubber, std::clamp(glm::length(b.velocity()) / 25.0f, 0.15f, 1.0f));
        if (m.phase != Match::Phase::Rally) continue;
        Rally::Result r = Rally::Result::None;
        switch (e.kind) {
        case Ball::Event::Kind::Bounce:
            r = m.rally.onBounce(e.at.x, e.at.z);
            if (m.replay && m.sinceBounce < 0.0f) m.replay->bounceTick = m.replay->tick; // the landing, for a replay
            m.sinceBounce = 0.0f;
            break;
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
        } else if (m.replay && m.replay->pending && m.phaseTime > 0.9f) {
            startReplay(m); // a winner or an ace, again in slow motion, once the call has shown
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
    p.intent.press = d.swing; // the takeback; stepPlayer lets it go when the ball comes
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
    glm::vec3 move = it.move * p.stamina.speedFactor();

    // Stamina: running hard in a rally costs, the time between points gives it back.
    const float running = glm::length(glm::vec2(p.vel.x, p.vel.z));
    if (m.phase == Match::Phase::Rally) {
        p.stamina.run(running, dt);
        if (running < 2.0f) p.stamina.rest(dt, false);
    } else {
        p.stamina.rest(dt, true);
    }
    if (p.timingShown > 0.0f) p.timingShown -= dt;

    const bool idle = p.swingT <= kSwingIdle + 0.5f;
    // Let go of the button: the forward swing. The CPU (and a person with
    // the menu's Assist) let go at the right moment, the CPU give or take
    // its skill.
    const bool autoTime = p.cpu || m_autoTiming;
    bool release = false;
    if (!idle && p.contactAt < 0.0f) {
        if (autoTime) {
            const float tt = timeToSpot(m, p);
            const StrokeShape& sh = shapeOf(p.stroke);
            // (Half a step early: the step that lets go is the one nearest the moment.)
            release = (tt >= 0.0f && tt <= sh.forward + p.releaseLead + 0.5f * dt) || (tt < 0.0f && p.crossAt >= 0.0f);
        } else {
            release = !it.held;
        }
    }

    // Serving: press to toss (holding builds the serve's power), let go to hit.
    if (serving) {
        move = glm::vec3(0.0f);
        if (p.tossAge < 0.0f) {
            b.place(tossHand(m, p));
            const bool cpuToss = p.cpu && m.phaseTime > 1.1f;
            if ((it.press && !p.cpu && m.phaseTime > 0.3f) || cpuToss) {
                p.tossAge = 0.0f;
                b.place(b.position() + glm::vec3(0.0f, 0.1f, 0.0f), glm::vec3(0.0f, 5.4f, -0.35f * s));
                if (online() && m.netId)
                    m_net->sendEvent(net::kEventToss,
                                     net::encode(net::Toss{ m.netId, m.serial, static_cast<uint8_t>(indexInMatch(m, p)), b.position(), b.velocity() }));
                if (p.cpu) it.aim = glm::vec2(noise(0.5f), 0.2f);
                startSwing(m, p, it.kind, true);
                release = false;
            }
        } else {
            p.tossAge += dt;
            stepSwing(m, p, true, release, dt);
            if (b.position().y < 1.0f && b.velocity().y < 0.0f && m.phase == Match::Phase::Serve) {
                // Nobody hit it: catch it and toss again.
                if (m_swingLog) kke::log::get(name())->info("court {}: {} caught the toss (let go: {}, ball at the spot: {})", m.court + 1, p.name, p.contactAt >= 0.0f, p.crossAt >= 0.0f);
                p.tossAge = -1.0f;
                endSwing(p);
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
                if (d > 0.1f) move = to / d * std::min(kRunSpeed * 0.9f * p.stamina.speedFactor(), d * 4.0f);
            }
        }
        // The CPU starts its takeback in time for a full one.
        if (p.cpu && idle && m.rally.mayHit(p.team) && isMyBall(m, idx)) {
            const float tt = timeToSpot(m, p);
            const StrokeShape& sh = shapeOf(Stroke::Drive);
            if (tt >= 0.0f && tt < sh.windUp + sh.forward + 0.05f) it.press = true;
        }
        if (it.press && idle && m.rally.mayHit(p.team)) startSwing(m, p, it.kind, false);
        else if (!idle) stepSwing(m, p, false, release, dt);
        it.press = false;
    } else {
        it.press = false;
        if (!idle) stepSwing(m, p, false, false, dt);
        // Between points: stand still (placeForPoint puts everyone back).
        move = glm::vec3(0.0f);
    }
    // In the takeback the feet slow (a person's; the CPU sets its own pace).
    if (!p.cpu && p.swingT > kSwingIdle + 0.5f && p.contactAt < 0.0f) move *= kChargeSpeed / kRunSpeed;

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

// Where the server holds the ball (court space): the hand, once the body
// has been posed; before that, beside them.
glm::vec3 TennisModule::tossHand(const Match& m, const Player& p) const {
    const CourtPlace& place = m_center.courts[static_cast<size_t>(m.court)];
    const glm::vec3 fallback = p.feet + glm::vec3(0.0f, 1.2f, 0.0f);
    if (!p.look) return fallback;
    const glm::vec3 hand = place.toLocal(p.look->tossHand());
    return glm::length(glm::vec2(hand.x - p.feet.x, hand.z - p.feet.z)) < 1.0f ? hand : fallback;
}

// The ball in this player's body frame: x right, y up, z toward the net.
glm::vec3 TennisModule::bodyRel(const Match& m, const Player& p, const glm::vec3& at) const {
    const float s = static_cast<float>(m.score.sideOf(p.team));
    return { (at.x - p.feet.x) * s, at.y, -(at.z - p.feet.z) * s };
}

// How far the ball still has to go to the hitting spot of the stroke
// (the plane in front of the body where the racket meets it, or for an
// overhead, the height): > 0 before, <= 0 once there.
float TennisModule::planeGap(const Match& m, const Player& p) const {
    const StrokeShape& sh = shapeOf(p.stroke);
    const glm::vec3 rel = bodyRel(m, p, m.ball->position());
    if (sh.overhead) return m.ball->velocity().y > 0.0f ? 99.0f : rel.y - sh.plane;
    return rel.z - sh.plane;
}

// When the ball gets to the hitting spot (s from now; -1: not soon), on
// its flight and, before its bounce, the bounce after it.
float TennisModule::timeToSpot(const Match& m, const Player& p, glm::vec3* at, float* bounceAge) const {
    const StrokeShape& sh = shapeOf(p.stroke == Stroke::Ready ? Stroke::Drive : p.stroke);
    const Flight f = m.ball->flight();
    if (bounceAge) *bounceAge = m.sinceBounce;
    float t = -1.0f;
    Flight on = f;
    float from = 0.0f; // when `on` starts
    if (sh.overhead) {
        t = f.timeDownTo(sh.plane);
    } else {
        const float s = static_cast<float>(m.score.sideOf(p.team));
        const float zPlane = p.feet.z - s * sh.plane;
        t = f.timeAtZ(zPlane);
        if (t >= 0.0f && m.sinceBounce < 0.0f && m.phase == Match::Phase::Rally) {
            const float land = f.timeDownTo(kBallRadius);
            if (land >= 0.0f && land < t) {
                const float after = kGravity + (f.gravity - kGravity) * kPullAfterBounce;
                on = bounce(f, land, m.ball->bounceModel(), after);
                from = land;
                const float t2 = on.timeAtZ(zPlane);
                t = t2 < 0.0f ? -1.0f : land + t2;
            }
        }
    }
    if (t < 0.0f) return -1.0f;
    if (at) *at = on.at(t - from);
    if (bounceAge) *bounceAge = m.sinceBounce >= 0.0f ? m.sinceBounce + t : from > 0.0f ? t - from : -1.0f;
    return t;
}

void TennisModule::startSwing(Match& m, Player& p, ShotKind kind, bool serve) {
    p.armedKind = kind;
    p.clock = 0.0f;
    p.contactAt = -1.0f;
    p.crossAt = -1.0f;
    p.prevPlane = 99.0f;
    p.swingDone = false;
    p.charge = 0.0f;
    p.swingT = -2.0f;
    if (serve) {
        // The CPU mixes its first serves (flat, slice, kick) and kicks its second.
        if (p.cpu) {
            const float roll = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng());
            kind = m.rally.secondServeNow() || roll > 0.8f ? ShotKind::Topspin : roll < 0.5f ? ShotKind::Flat : ShotKind::Slice;
        }
        p.stroke = serveFor(kind);
        p.backhand = false;
    } else {
        // Where it'll be met picks the stroke (a volley at the net, a
        // half-volley at the feet, a stretch out wide...).
        glm::vec3 at = m.ball->position();
        float sinceBounce = m.sinceBounce;
        timeToSpot(m, p, &at, &sinceBounce);
        const StrokeChoice c = pickStroke(kind, bodyRel(m, p, at), sinceBounce, std::abs(p.feet.z));
        p.stroke = c.stroke;
        p.backhand = c.backhand;
    }
    const StrokeShape& sh = shapeOf(p.stroke);
    p.swingContact = sh.contact * glm::vec3(p.backhand ? -1.0f : 1.0f, 1.0f, 1.0f);
    // The CPU's timing: off by a little, more when it's tired.
    p.releaseLead = 0.0f;
    if (p.cpu && p.bot) p.releaseLead = noise(p.bot->skill().timing / std::max(0.3f, p.stamina.windowScale()));
}

void TennisModule::endSwing(Player& p) {
    p.swingT = kSwingIdle;
    p.stroke = Stroke::Ready;
    p.contactAt = -1.0f;
    p.crossAt = -1.0f;
    p.swingDone = false;
    p.charge = 0.0f;
}

// One step of a swing: the takeback builds power until let go; the forward
// swing reaches contact `forward` s later; the ball is hit if it reaches
// the hitting spot (in reach) within the stroke's timing window of that.
void TennisModule::stepSwing(Match& m, Player& p, bool serving, bool release, float dt) {
    const StrokeShape& sh = shapeOf(p.stroke);
    p.clock += dt;
    if (p.contactAt < 0.0f) {
        // The takeback.
        p.swingT = std::min(-1.0f, -2.0f + p.clock / sh.windUp);
        const float cap = p.stamina.powerCap() * (p.cpu && p.bot ? p.bot->skill().power + 0.3f : 1.0f);
        p.charge = std::min(std::min(1.0f, cap), p.charge + p.stamina.chargeRate() * dt);
        if (release) {
            p.contactAt = p.clock + sh.forward;
            p.swingT = -1.0f;
        }
        // Still re-reading the ball: a forehand can turn into a backhand.
        if (!serving && m.phase == Match::Phase::Rally && !p.swingDone) {
            glm::vec3 at;
            float sinceBounce = -1.0f;
            if (timeToSpot(m, p, &at, &sinceBounce) >= 0.0f) {
                const StrokeChoice c = pickStroke(p.armedKind, bodyRel(m, p, at), sinceBounce, std::abs(p.feet.z));
                if (c.stroke != p.stroke || c.backhand != p.backhand) {
                    p.stroke = c.stroke;
                    p.backhand = c.backhand;
                }
            }
        }
    } else if (p.clock <= p.contactAt) {
        p.swingT = -1.0f + (p.clock - (p.contactAt - sh.forward)) / sh.forward;
    } else {
        p.swingT = (p.clock - p.contactAt) / sh.follow;
        if (p.swingT > 1.0f) {
            endSwing(p);
            return;
        }
    }
    if (p.swingDone || !m.rally.mayHit(p.team) || (!serving && m.phase != Match::Phase::Rally)) return;

    // Where the ball is against the hitting spot: record when it got there
    // (in reach), between steps.
    const glm::vec3 ball = m.ball->position();
    const glm::vec3 rel = bodyRel(m, p, ball);
    const float gap = planeGap(m, p);
    if (p.crossAt < 0.0f && p.prevPlane > 0.0f && gap <= 0.0f && p.prevPlane < 90.0f) {
        const float reach = kReachMax + (!p.cpu && m_assist ? kReachHelp : 0.0f) + (p.stroke == Stroke::Stretch ? 0.35f : 0.0f);
        bool inReach;
        if (sh.overhead) inReach = glm::length(glm::vec2(rel.x, rel.z)) < (serving ? 1.3f : 1.5f);
        else inReach = std::abs(rel.x) > kReachMin * 0.5f && std::abs(rel.x) < reach && rel.y > 0.06f && rel.y < 2.4f;
        if (inReach) p.crossAt = p.clock - dt * (-gap) / std::max(1e-4f, p.prevPlane - gap);
    }
    p.prevPlane = gap;

    const float scale = p.stamina.windowScale() * m_timingScale;
    const float maxError = std::max(sh.window * 1.5f, sh.maxError * std::max(0.5f, scale));
    if (p.contactAt >= 0.0f && p.crossAt >= 0.0f) {
        const float error = p.contactAt - p.crossAt; // + late: the ball got there first
        if (std::abs(error) > maxError) {
            p.swingDone = true;
            p.timingText = timingWord(error, sh, scale);
            p.timingShown = 1.2f;
            if (m_swingLog) kke::log::get(name())->info("court {}: {} {} missed: {:+.0f} ms", m.court + 1, p.name, strokeName(p.stroke), error * 1000.0f);
            return;
        }
        if (p.clock + 1e-5f >= std::max(p.contactAt, p.crossAt)) {
            p.swingDone = true;
            hitBall(m, p, ball, serving, error);
        }
        return;
    }
    // Swung too soon (the ball never came) or never swung (it went by).
    if ((p.contactAt >= 0.0f && p.clock > p.contactAt + maxError) || (p.crossAt >= 0.0f && p.contactAt < 0.0f && p.clock > p.crossAt + maxError)) {
        p.swingDone = true;
        p.timingText = p.contactAt >= 0.0f ? "Too early" : "Too late";
        p.timingShown = 1.2f;
        if (m_swingLog) kke::log::get(name())->info("court {}: {} {} missed: {}", m.court + 1, p.name, strokeName(p.stroke), p.timingText);
        if (p.contactAt < 0.0f) endSwing(p);
    }
}

// Another machine's player: its feet and swing come from what it sends
// (Net.cpp); its hits and its toss come as events.
void TennisModule::stepRemote(Match& m, Player& p, float dt) {
    const CourtPlace& place = m_center.courts[static_cast<size_t>(m.court)];
    glm::vec3 face = place.dirToLocal(p.pose.facing);
    face.y = 0.0f;
    if (glm::length(face) > 1e-3f) p.facing = glm::normalize(face);
    p.stroke = p.pose.stroke;
    p.backhand = p.pose.backhand;
    p.swingT = p.pose.swingT;
    p.swingContact = p.pose.contact;
    const bool serving = m.phase == Match::Phase::Serve && static_cast<int>(&p - m_players.data()) == serverIndex(m);
    if (serving) {
        Ball& b = *m.ball;
        // The ball in their hand until their toss comes; back in it if
        // they let it drop.
        if (p.tossAge < 0.0f) {
            b.place(tossHand(m, p));
        } else {
            p.tossAge += dt;
            if (b.position().y < 1.0f && b.velocity().y < 0.0f) p.tossAge = -1.0f;
        }
    }
    if (p.celebrate > 0.0f) p.celebrate -= dt;
}

void TennisModule::hitBall(Match& m, Player& p, const glm::vec3& contact, bool serve, float timingError) {
    const int side = m.score.sideOf(p.team);
    const float s = static_cast<float>(side);
    const bool doubles = m.rules.teamSize > 1;
    const StrokeShape& sh = shapeOf(p.stroke);
    const ShotKind kind = serve ? ShotKind::Serve : flightKind(p.stroke, p.armedKind);
    const glm::vec3 rel = bodyRel(m, p, contact); // x right, z forward
    const bool forehand = !p.backhand;
    // Spacing: an arm and a racket to the side, between knee and shoulder,
    // beside or just in front; each a band, not a point.
    auto band = [](float v, float lo, float hi, float falloff) {
        const float out = v < lo ? lo - v : v > hi ? v - hi : 0.0f;
        return std::max(0.0f, 1.0f - out / falloff);
    };
    float spacing = 1.0f;
    if (serve || sh.overhead) spacing = band(glm::length(glm::vec2(rel.x, rel.z)), 0.0f, 0.6f, 1.0f);
    else spacing = band(std::abs(rel.x), 0.45f, 1.1f, 0.7f) * band(rel.y, 0.3f, 1.6f, 1.0f);
    const float scale = p.stamina.windowScale() * m_timingScale;
    const float timing = timingQuality(timingError, sh, scale);
    float quality = std::clamp(spacing * (0.25f + 0.75f * timing), 0.15f, 1.0f);
    if (!p.cpu && m_assist) quality = std::max(quality, 0.7f);
    p.timingText = timingWord(timingError, sh, scale);
    p.timingShown = 1.2f;
    if (m_swingLog)
        kke::log::get(name())->info("court {}: {} {}{} {:+.0f} ms ({}), spacing {:.2f}, power {:.2f}, stamina {:.2f}", m.court + 1, p.name,
                                    p.backhand ? "backhand " : "", strokeName(p.stroke), timingError * 1000.0f, p.timingText, spacing, p.charge,
                                    p.stamina.level);
    // Late pushes it wide the way the racket faced, early pulls it across.
    const float early = serve ? 0.0f : std::clamp(-timingError / std::max(0.02f, sh.maxError), -1.0f, 1.0f);

    glm::vec2 aim = glm::clamp(p.intent.aim, glm::vec2(-1.0f), glm::vec2(1.0f));
    glm::vec3 target;
    const float err = ((1.0f - quality) * 2.2f + (p.bot ? p.bot->skill().aimError : 0.4f)) * sh.control + p.stamina.aimWobble();
    if (serve) {
        const bool deuce = m.score.deuceCourt();
        const float centre = deuce ? -2.05f : 2.05f; // x * s of the box's middle
        const float wide = p.stroke == Stroke::ServeSlice ? (deuce ? -0.4f : 0.4f) : 0.0f; // slice swings out wide
        const float x = std::clamp(centre + wide + aim.x * 1.4f + noise(err * 0.5f), centre - 1.8f, centre + 1.8f);
        const float depth = std::clamp(4.6f + aim.y * 0.9f + noise(err * 0.4f), 3.0f, kServiceLine + 0.6f);
        target = { x * s, 0.0f, -s * depth };
    } else {
        const float half = (doubles ? kDoublesHalfWidth : kSinglesHalfWidth) - 0.6f;
        float depth = 7.6f + aim.y * 1.9f;
        if (kind == ShotKind::Drop) depth = 2.6f + aim.y * 0.5f;
        if (kind == ShotKind::Lob) depth = 9.6f + aim.y * 1.2f;
        if (p.stroke == Stroke::Volley || p.stroke == Stroke::HalfVolley) depth -= 1.2f;
        const float x = aim.x * half + (forehand ? -1.0f : 1.0f) * early * 1.6f + noise(err);
        target = { x * s, 0.0f, -s * (depth + noise(err * 0.8f)) };
    }
    const float charge = std::clamp(p.charge, 0.0f, 1.0f);
    float speed = speedFor(kind, charge) * sh.speed * (0.7f + 0.3f * quality);
    if (serve && m.rally.secondServeNow()) speed *= 0.8f;
    // Slow shots from far away still get there in time (a drop shot from
    // the baseline is a hard push, not a moon ball).
    const float longest = kind == ShotKind::Lob ? 2.4f : 1.5f;
    speed = std::max(speed, glm::length(glm::vec2(target.x - contact.x, target.z - contact.z)) / longest);
    // A kick serve flies like topspin: higher over the net, dipping in.
    const ShotKind flight = p.stroke == Stroke::ServeKick ? ShotKind::Topspin : kind;
    ShotPlan plan = planShot(contact, target, speed, flight, netMarginFor(flight) * (0.6f + 0.6f * quality));
    // A mistimed shot also comes off the frame a bit: a little into the net
    // or long.
    plan.velocity *= 1.0f + noise((1.0f - quality) * 0.1f);
    glm::vec3 dir(plan.velocity.x, 0.0f, plan.velocity.z);
    dir = glm::length(dir) > 1e-3f ? glm::normalize(dir) : glm::vec3(0.0f, 0.0f, -s);
    const glm::vec3 spin = glm::cross(glm::vec3(0, 1, 0), dir) * spinRate(flight);
    p.stamina.swing(charge, serve);
    p.swingContact = glm::vec3(rel.x, contact.y, std::max(0.2f, rel.z));
    p.charge = 0.0f;


    net::Hit h;
    h.match = m.netId;
    h.point = m.serial;
    h.player = static_cast<uint8_t>(indexInMatch(m, p));
    h.shot = static_cast<uint8_t>(std::min(m.rallyShots, 255));
    h.serve = serve;
    h.kind = static_cast<uint8_t>(kind);
    h.at = contact;
    h.velocity = plan.velocity;
    h.spin = spin;
    h.pull = plan.gravity - kGravity;
    h.squash = 0.25f + 0.75f * std::min(1.0f, speed / 45.0f);
    if (online() && m.netId) {
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
    const CourtPlace& place = m_center.courts[static_cast<size_t>(m.court)];
    // The strings take the ball as it comes in (FEMFX, Strings.h).
    if (Player& hp = player(hitter); hp.look) hp.look->hitStrings(place.toWorld(h.at), place.dirToWorld(b.velocity()));
    b.strikeAt(h.at, h.velocity, h.spin, h.pull, h.squash);
    if (auto* audio = m_app->getModule<kke::AudioModule>())
        audio->playImpact(place.toWorld(h.at), kke::AudioMaterialTable::Plastic,
                          std::clamp(glm::length(h.velocity) / 40.0f, 0.3f, 1.0f));
    m.rallyShots = h.shot + 1;
    m.lastHitter = hitter;
    if (m.replay && !m.replay->playing) {
        std::vector<Replay::Hit>& hits = m.replay->hits;
        if (hits.size() >= 8) hits.erase(hits.begin());
        hits.push_back({ m.replay->tick, hitter, h });
    }
    m.sinceHit = 0.0f;
    m.sinceBounce = -1.0f;
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
