#include "kke/Climber.h"

#include <algorithm>
#include <cmath>

namespace kke {

namespace {
float smooth(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
} // namespace

Climber::Climber(const ClimbWall& wall) : Climber(wall, Settings{}) {}

Climber::Climber(const ClimbWall& wall, const Settings& settings) : m_wall(wall), m_s(settings) { m_stamina = m_s.maxStamina; }

bool Climber::holdGone(int hold) const { return std::find(m_gone.begin(), m_gone.end(), hold) != m_gone.end(); }

bool Climber::usable(int hold, int h) const {
    if (hold < 0 || hold >= static_cast<int>(m_wall.holds().size()) || holdGone(hold)) return false;
    const Hand& other = m_hand[1 - h];
    return hold != other.hold && !(other.move != Move::None && other.target == hold);
}

float Climber::handProgress(int h) const {
    const Hand& hd = m_hand[h];
    return hd.move == Move::None || hd.duration <= 0.0f ? 1.0f : std::clamp(hd.t / hd.duration, 0.0f, 1.0f);
}

float Climber::mantleProgress() const {
    return m_state == State::Mantle ? std::clamp(m_mantleT / m_s.mantleTime, 0.0f, 1.0f) : m_state == State::Topped ? 1.0f : 0.0f;
}

float Climber::reachNow(int h) const {
    const Hand& hd = m_hand[h];
    return hd.charging ? m_s.span + (m_s.lungeSpan - m_s.span) * hd.charge : m_s.span;
}

// Reach is measured from the hold the *other* hand is on (the one the
// body hangs from while this hand moves).
glm::vec3 Climber::pivot(int h) const {
    const Hand& other = m_hand[1 - h];
    if (other.hold >= 0 && other.move == Move::None) return m_wall.holds()[static_cast<size_t>(other.hold)].position;
    return m_hand[h].pos;
}

bool Climber::start(const glm::vec3& feet) {
    const auto& holds = m_wall.holds();
    int best[2] = { -1, -1 };
    float score[2] = { 1e9f, 1e9f };
    for (size_t i = 0; i < holds.size(); ++i) {
        if (holdGone(static_cast<int>(i))) continue;
        const glm::vec3& p = holds[i].position;
        const float up = p.y - feet.y;
        if (up < 1.1f || up > 2.35f || std::abs(p.x - feet.x) > 1.1f || std::abs(p.z - feet.z) > 1.6f) continue;
        for (int h = 0; h < 2; ++h) {
            const float side = h == kLeft ? -1.0f : 1.0f;
            if ((p.x - feet.x) * side < -0.2f) continue;
            const float s = glm::length(glm::vec2(p.x - (feet.x + side * 0.25f), p.y - (feet.y + 1.95f))) - 0.3f * holdGrip(holds[i].kind);
            if (s < score[h]) {
                score[h] = s;
                best[h] = static_cast<int>(i);
            }
        }
    }
    if (best[0] == best[1] && best[0] >= 0) {
        // One hold good for both: the hand it's nearer to takes it.
        if (score[0] <= score[1]) best[1] = -1;
        else best[0] = -1;
    }
    if (best[0] < 0 && best[1] < 0) return false;

    m_state = State::Climbing;
    m_hips = feet + glm::vec3(0.0f, m_s.hipsHeight, 0.0f);
    m_facing = glm::vec3(0, 0, -1);
    for (int h = 0; h < 2; ++h) {
        Hand& hd = m_hand[h];
        hd = Hand{};
        hd.pos = m_hips + glm::vec3(h == kLeft ? -0.25f : 0.25f, 0.55f, -0.1f);
        if (best[h] >= 0) {
            hd.hold = -1;
            hd.move = Move::Precise;
            hd.from = hd.pos;
            hd.target = best[h];
            hd.to = holds[static_cast<size_t>(best[h])].position;
            hd.duration = m_s.quickTime * 1.5f;
        }
    }
    m_feetAnchor = glm::vec3(1e9f);
    placeFeet(true);
    for (Foot& f : m_foot) f.pos = f.target;
    m_mantleLedge = -1;
    return true;
}

int Climber::findTarget(int h, const glm::vec2& aimIn, float reach) const {
    const auto& holds = m_wall.holds();
    const glm::vec3 piv = pivot(h);
    const Hand& hd = m_hand[h];
    const Hand& other = m_hand[1 - h];
    const glm::vec3 ref = hd.hold >= 0 ? holds[static_cast<size_t>(hd.hold)].position : hd.pos;
    glm::vec2 aim = aimIn;
    const float strength = glm::length(aim);
    aim = strength < 0.2f ? glm::vec2(0.0f, 1.0f) : aim / strength;
    const float side = h == kLeft ? -1.0f : 1.0f;
    const float otherX = other.hold >= 0 ? holds[static_cast<size_t>(other.hold)].position.x : m_hips.x;
    int best = -1;
    float bestScore = -1e9f;
    for (size_t i = 0; i < holds.size(); ++i) {
        const int idx = static_cast<int>(i);
        if (idx == hd.hold || !usable(idx, h)) continue;
        const glm::vec3& p = holds[i].position;
        if (ClimbWall::reachDistance(p, piv) > reach) continue;
        // Hands may cross a little, not swap sides.
        if ((p.x - otherX) * side < -0.5f) continue;
        if (p.y < m_hips.y - 0.6f) continue; // down there is for feet
        const glm::vec2 v(p.x - ref.x, p.y - ref.y);
        const float len = glm::length(v);
        if (len < 0.08f) continue;
        const float c = glm::dot(v / len, aim);
        if (c < 0.35f) continue;
        float s = c * c * 1.5f - 0.35f * std::abs(len - 0.75f) + 0.15f * holdGrip(holds[i].kind);
        if (holds[i].kind == ClimbHold::Kind::Edge && aim.y > 0.5f) s += 0.25f; // up and over
        if (s > bestScore) {
            bestScore = s;
            best = idx;
        }
    }
    return best;
}

void Climber::launch(int h, Move m, int target, const glm::vec2& aim, float reach) {
    Hand& hd = m_hand[h];
    const Hand& other = m_hand[1 - h];
    // The body hangs from the other hand while this one moves.
    if (other.hold < 0 || other.move != Move::None) return;
    hd.from = hd.pos;
    hd.target = target;
    if (target >= 0) {
        hd.to = m_wall.holds()[static_cast<size_t>(target)].position;
    } else {
        // A throw at nothing: the hand goes where it was aimed and closes on air.
        const glm::vec2 a = glm::length(aim) > 0.2f ? glm::normalize(aim) : glm::vec2(0.0f, 1.0f);
        const glm::vec3 piv = pivot(h);
        hd.to = m_wall.surfacePoint(piv.x + a.x * reach, piv.y + a.y * reach, 0.1f);
    }
    hd.hold = -1;
    hd.move = m;
    hd.t = 0.0f;
    hd.duration = m == Move::Precise ? m_s.reachTime : m == Move::Quick ? m_s.quickTime : m_s.lungeTime;
    float cost = m == Move::Precise ? m_s.costPrecise : m == Move::Quick ? m_s.costQuick : m_s.costLungeMin + (m_s.costLungeMax - m_s.costLungeMin) * hd.charge;
    m_stamina -= cost;
    hd.charge = 0.0f;
    hd.charging = false;
}

void Climber::land(int h) {
    Hand& hd = m_hand[h];
    const Move m = hd.move;
    hd.move = Move::None;
    hd.pos = hd.to;
    if (hd.target < 0 || holdGone(hd.target)) {
        m_missed = true;
        hd.target = -1;
        return;
    }
    const ClimbHold& hold = m_wall.holds()[static_cast<size_t>(hd.target)];
    if (hold.loose && m == Move::Lunge) {
        // Hit hard, it comes off the rock: the hand closes on nothing.
        m_gone.push_back(hd.target);
        m_broke = hd.target;
        m_missed = true;
        hd.target = -1;
        return;
    }
    hd.hold = hd.target;
    hd.target = -1;
    m_grabbed = true;
}

void Climber::fall() {
    m_state = State::Fell;
    m_fellNow = true;
    for (Hand& hd : m_hand) {
        hd.hold = -1;
        hd.move = Move::None;
        hd.charging = false;
        hd.charge = 0.0f;
    }
    m_aim[0] = m_aim[1] = -1;
}

void Climber::placeFeet(bool force) {
    if (!force && glm::length(m_hips - m_feetAnchor) < 0.3f) return;
    m_feetAnchor = m_hips;
    const auto& holds = m_wall.holds();
    for (int f = 0; f < 2; ++f) {
        const float side = f == 0 ? -1.0f : 1.0f;
        // Feet up under the body, knees bent (not hanging straight).
        const glm::vec2 ideal(m_hips.x + side * 0.25f, m_hips.y - 0.6f);
        int best = -1;
        float bestD = 1e9f;
        for (size_t i = 0; i < holds.size(); ++i) {
            const ClimbHold& hd = holds[i];
            if (hd.kind == ClimbHold::Kind::Edge || holdGone(static_cast<int>(i))) continue;
            if (static_cast<int>(i) == m_foot[1 - f].hold || static_cast<int>(i) == m_hand[0].hold || static_cast<int>(i) == m_hand[1].hold) continue;
            const float dx = (hd.position.x - m_hips.x) * side, dy = m_hips.y - hd.position.y;
            if (dx < -0.1f || dx > 0.6f || dy < 0.25f || dy > 0.95f) continue;
            const float d = glm::length(glm::vec2(hd.position.x, hd.position.y) - ideal);
            if (d < bestD) {
                bestD = d;
                best = static_cast<int>(i);
            }
        }
        m_foot[f].hold = best;
        m_foot[f].target = best >= 0 ? holds[static_cast<size_t>(best)].position : m_wall.surfacePoint(ideal.x, ideal.y, 0.06f);
    }
}

void Climber::updateBody(float dt, bool fast) {
    const auto& holds = m_wall.holds();
    glm::vec3 anchor(0.0f);
    int n = 0;
    for (int h = 0; h < 2; ++h)
        if (m_hand[h].hold >= 0) {
            anchor += holds[static_cast<size_t>(m_hand[h].hold)].position;
            ++n;
        }
    if (n == 0) return;
    anchor /= static_cast<float>(n);
    // Hanging from one hand the body swings under it, a little to that side.
    if (n == 1) anchor.x += m_hand[0].hold >= 0 ? 0.12f : -0.12f;
    glm::vec3 target = anchor - glm::vec3(0.0f, m_s.hang, 0.0f);
    // Off the rock: at least bodyOut in front of it (hanging straight
    // down under an overhang, close in on a slab).
    const float rockZ = m_wall.surfaceZ(target.x, target.y);
    target.z = std::max(rockZ + m_s.bodyOut, std::min(anchor.z + 0.1f, rockZ + m_s.bodyOut + 0.6f));
    const float rate = fast ? m_s.bodyFollow * 2.2f : m_s.bodyFollow;
    m_hips += (target - m_hips) * (1.0f - std::exp(-rate * dt));
    const glm::vec3 nrm = m_wall.surfaceNormal(m_hips.x, m_hips.y);
    glm::vec3 f(-nrm.x, 0.0f, -nrm.z);
    if (glm::length(f) > 1e-3f) m_facing = glm::normalize(f);
}

void Climber::updateStamina(float dt) {
    const auto& holds = m_wall.holds();
    int n = 0;
    float grip = 0.0f;
    bool jugs = true;
    // A hand on its way to a hold is part of the move (its cost was paid
    // at launch): it counts as half a hand, so reaching isn't punished
    // on top. Only a hand hanging free leaves the other alone.
    bool reaching = false;
    for (const Hand& hd : m_hand) {
        reaching = reaching || hd.move != Move::None;
        if (hd.hold >= 0) {
            const ClimbHold::Kind k = holds[static_cast<size_t>(hd.hold)].kind;
            grip += holdGrip(k);
            jugs = jugs && (k == ClimbHold::Kind::Jug || k == ClimbHold::Kind::Edge);
            ++n;
        }
    }
    if (n == 0) {
        m_drain = 0.0f;
        return;
    }
    grip /= static_cast<float>(n);
    const float lean = m_wall.leanAt(m_hips.y + m_s.hang);
    const int feetOn = (m_foot[0].hold >= 0 ? 1 : 0) + (m_foot[1].hold >= 0 ? 1 : 0);
    if (n == 2 && jugs && feetOn > 0 && lean < 5.0f) {
        m_drain = -m_s.shakeOut;
    } else {
        const float hands = n == 2 ? m_s.drainTwoHands : reaching ? (m_s.drainTwoHands + m_s.drainOneHand) * 0.5f : m_s.drainOneHand;
        float drain = hands / std::max(0.35f, grip);
        drain *= 1.0f + std::max(0.0f, lean) * m_s.overhangDrain - std::min(0.35f, std::max(0.0f, -lean) * 0.015f);
        // Feet cut loose under a steep overhang take less of the weight.
        const float feetShare = m_s.footRelief * (lean > 10.0f ? 0.4f : 1.0f);
        drain *= 1.0f - feetShare * static_cast<float>(feetOn) * 0.5f;
        m_drain = drain;
    }
    m_stamina = std::min(m_s.maxStamina, m_stamina - m_drain * dt);
}

void Climber::recover(float perSecond, float dt) {
    if (climbing()) return;
    m_stamina = std::min(m_s.maxStamina, m_stamina + perSecond * dt);
}

void Climber::tryMantle(const Input& in) {
    const auto& holds = m_wall.holds();
    const int a = m_hand[0].hold, b = m_hand[1].hold;
    if (a < 0 || b < 0) return;
    const ClimbHold& ha = holds[static_cast<size_t>(a)];
    const ClimbHold& hb = holds[static_cast<size_t>(b)];
    if (ha.kind != ClimbHold::Kind::Edge || hb.kind != ClimbHold::Kind::Edge || ha.ledge != hb.ledge) return;
    const bool push = in.aim.y > 0.5f && (in.reach[0] || in.reach[1] || in.power[0] > 0.5f || in.power[1] > 0.5f);
    if (!push) return;
    const float x = (ha.position.x + hb.position.x) * 0.5f;
    if (ha.ledge >= 0) {
        const ClimbLedge& l = m_wall.ledges()[static_cast<size_t>(ha.ledge)];
        m_mantleFeet = glm::vec3(std::clamp(x, l.center.x - l.halfExtents.x + 0.35f, l.center.x + l.halfExtents.x - 0.35f), l.top(),
                                 l.center.z + l.halfExtents.z - 0.45f);
    } else {
        m_mantleFeet = glm::vec3(x, m_wall.summitY(), m_wall.surfaceZ(x, m_wall.summitY()) - 0.8f);
    }
    m_mantleLedge = ha.ledge;
    m_mantleFrom = m_hips;
    m_mantleT = 0.0f;
    m_state = State::Mantle;
    m_hand[0].charging = m_hand[1].charging = false;
}

void Climber::update(const Input& in, float dt) {
    m_broke = -1;
    m_missed = m_grabbed = m_fellNow = false;
    if (m_state == State::Mantle) {
        m_mantleT += dt;
        const float t = mantleProgress();
        // Up first (hips over the edge), then forward onto the top.
        const glm::vec3 endHips = m_mantleFeet + glm::vec3(0.0f, m_s.hipsHeight, 0.0f);
        const float up = smooth(t / 0.6f), fwd = smooth((t - 0.35f) / 0.65f);
        m_hips = glm::vec3(m_mantleFrom.x + (endHips.x - m_mantleFrom.x) * fwd,
                           m_mantleFrom.y + (endHips.y + 0.25f * (1.0f - fwd) - m_mantleFrom.y) * up,
                           m_mantleFrom.z + (endHips.z - m_mantleFrom.z) * fwd);
        for (Foot& f : m_foot) f.pos += (m_hips - glm::vec3(0.0f, 0.9f * (1.0f - fwd), 0.0f) - f.pos) * (1.0f - std::exp(-8.0f * dt));
        if (m_mantleT >= m_s.mantleTime) {
            m_state = State::Topped;
            for (Hand& hd : m_hand) hd.hold = -1;
        }
        return;
    }
    if (m_state != State::Climbing) return;

    if (in.letGo) {
        fall();
        return;
    }

    // Hands in flight.
    bool lunging = false;
    for (int h = 0; h < 2; ++h) {
        Hand& hd = m_hand[h];
        if (hd.move == Move::None) continue;
        hd.t += dt;
        const float s = smooth(hd.t / hd.duration);
        // An arc out from the rock, so the hand doesn't drag along it.
        const glm::vec3 n = m_wall.surfaceNormal(hd.to.x, hd.to.y);
        hd.pos = hd.from + (hd.to - hd.from) * s + n * (0.12f * std::sin(3.14159265f * s));
        lunging = lunging || hd.move == Move::Lunge;
        if (hd.t >= hd.duration) land(h);
    }

    // Both hands on one edge and pushing up: over it (before the push
    // could turn into a reach).
    if (m_hand[0].move == Move::None && m_hand[1].move == Move::None) {
        tryMantle(in);
        if (m_state == State::Mantle) return;
    }

    // Choosing and launching.
    for (int h = 0; h < 2; ++h) {
        Hand& hd = m_hand[h];
        const Hand& other = m_hand[1 - h];
        const bool free = hd.move == Move::None && other.hold >= 0 && other.move == Move::None;
        const float power = in.power[h];
        if (hd.move == Move::None && power > 0.2f) {
            if (!hd.charging) hd.charge = 0.0f;
            hd.charging = true;
            hd.charge = std::min(power, hd.charge + dt / m_s.chargeTime);
        }
        const float reach = reachNow(h);
        const int picked = in.pick[h];
        const float pickDist = picked >= 0 ? ClimbWall::reachDistance(m_wall.holds()[static_cast<size_t>(picked)].position, pivot(h)) : 1e9f;
        const bool pickOk = picked >= 0 && usable(picked, h) && picked != hd.hold && pickDist <= reach;
        m_aim[h] = hd.move != Move::None ? -1 : pickOk ? picked : picked >= 0 ? -1 : findTarget(h, in.aim, reach);
        if (!free) continue;
        if (in.reach[h]) {
            // Bumper: a precise reach, or with the trigger held a quick snatch.
            const bool quick = hd.charging;
            hd.charging = false;
            hd.charge = 0.0f;
            // A hold picked outright that's out of a bumper's reach: no move
            // (the game shows it out of reach), not some other hold.
            const int t = picked >= 0 ? (pickOk && pickDist <= m_s.span ? picked : -1) : findTarget(h, in.aim, m_s.span);
            if (t >= 0) launch(h, quick ? Move::Quick : Move::Precise, t, in.aim, m_s.span);
        } else if (hd.charging && power < 0.1f) {
            // Trigger let go: throw the hand, as far as the charge carries it.
            if (hd.charge >= 0.08f) launch(h, Move::Lunge, m_aim[h], in.aim, reach);
            hd.charging = false;
            hd.charge = 0.0f;
        }
    }

    updateBody(dt, lunging);
    placeFeet(false);
    for (Foot& f : m_foot) f.pos += (f.target - f.pos) * (1.0f - std::exp(-12.0f * dt));
    // A free hand hangs by the body.
    for (int h = 0; h < 2; ++h) {
        Hand& hd = m_hand[h];
        if (hd.hold < 0 && hd.move == Move::None) {
            const glm::vec3 rest = m_hips + glm::vec3(h == kLeft ? -0.3f : 0.3f, 0.35f, 0.0f) - m_facing * 0.05f;
            hd.pos += (rest - hd.pos) * (1.0f - std::exp(-6.0f * dt));
        }
    }
    updateStamina(dt);

    const bool holding = m_hand[0].hold >= 0 || m_hand[1].hold >= 0;
    const bool inFlight = m_hand[0].move != Move::None || m_hand[1].move != Move::None;
    if (m_stamina <= 0.0f) {
        m_stamina = 0.0f;
        fall();
    } else if (!holding && !inFlight) {
        fall();
    }
}

// ---------------------------------------------------------------------

int ClimbBot::routeIndex(int hold) const {
    for (size_t i = 0; i < m_route.size(); ++i)
        if (m_route[i] == hold) return static_cast<int>(i);
    return -1;
}

Climber::Input ClimbBot::think(const Climber& c, float dt) {
    Climber::Input in;
    in.aim = glm::vec2(0.0f, 1.0f);
    if (c.state() != Climber::State::Climbing) return in;
    if (c.handMoving(0) || c.handMoving(1)) return in;
    const auto& holds = c.wall().holds();
    const int h0 = c.handHold(0), h1 = c.handHold(1);
    // A lunge being charged: hold the trigger until the charge carries
    // that far, then let go.
    if (m_lungeHand >= 0) {
        const int h = m_lungeHand;
        const int anchorHold = c.handHold(1 - h);
        if (anchorHold < 0 || c.holdGone(m_lungePick)) {
            m_lungeHand = -1;
            return in;
        }
        const Climber::Settings& s = c.settings();
        const glm::vec3& target = holds[static_cast<size_t>(m_lungePick)].position;
        const float d = ClimbWall::reachDistance(target, holds[static_cast<size_t>(anchorHold)].position);
        const float need = std::clamp((d - s.span) / (s.lungeSpan - s.span) + 0.12f, 0.1f, 1.0f);
        const glm::vec3 cur = c.hand(h);
        const glm::vec2 dir(target.x - cur.x, target.y - cur.y);
        in.aim = glm::length(dir) > 1e-3f ? glm::normalize(dir) : glm::vec2(0.0f, 1.0f);
        in.pick[h] = m_lungePick;
        if (c.charge(h) < need) in.power[h] = 1.0f;
        else m_lungeHand = -1; // trigger let go: the lunge
        return in;
    }
    // Both hands on an edge: mantle when it's the top, or to rest.
    if (h0 >= 0 && h1 >= 0) {
        const ClimbHold& a = holds[static_cast<size_t>(h0)];
        const ClimbHold& b = holds[static_cast<size_t>(h1)];
        if (a.kind == ClimbHold::Kind::Edge && b.kind == ClimbHold::Kind::Edge && a.ledge == b.ledge &&
            (a.ledge < 0 || c.staminaFraction() < ledgeRestBelow)) {
            in.reach[0] = true;
            return in;
        }
    }
    // A breath between moves, longer when tired.
    m_wait -= dt;
    if (m_wait > 0.0f) return in;
    // Under an overhang there's no rest in waiting: keep moving.
    const bool steep = c.wall().leanAt(c.hips().y + 1.0f) > 5.0f;
    m_wait = steep ? pause * 0.5f : pause + (1.0f - c.staminaFraction()) * pause;

    // The hand that hangs on: the one further along the route (or higher).
    auto progress = [&](int hold) -> float {
        if (hold < 0) return -1e9f;
        const ClimbHold& h = holds[static_cast<size_t>(hold)];
        // A hand on the summit's edge, or a ledge's when a rest is due,
        // stays there: the other one joins it.
        if (h.kind == ClimbHold::Kind::Edge && (h.ledge < 0 || c.staminaFraction() < ledgeRestBelow)) return 1e6f + h.position.y;
        const int r = routeIndex(hold);
        return r >= 0 ? static_cast<float>(r) * 10.0f : h.position.y;
    };
    int stay = progress(h0) >= progress(h1) ? 0 : 1;
    if (c.handHold(stay) < 0) stay = 1 - stay;
    const int move = 1 - stay;
    const int anchor = c.handHold(stay);
    if (anchor < 0) return in;
    const glm::vec3 from = holds[static_cast<size_t>(anchor)].position;
    const float span = c.wall().desc().routeStep; // the steps the route was built with
    const float reach = std::max(span, 1.0f) + 0.3f;
    const int at = routeIndex(anchor);
    int pick = -1;
    // Resting on two jugs with feet on: shake out before going on.
    if (h0 >= 0 && h1 >= 0 && c.drainRate() < 0.0f && c.staminaFraction() < restUntil) return in;
    // Tired: make for a ledge's edge in reach, to stand on it and rest,
    // or else a jug to shake out on (not under an overhang: no rest there).
    const ClimbHold& anchorHold = holds[static_cast<size_t>(anchor)];
    const bool atLedge = anchorHold.kind == ClimbHold::Kind::Edge && anchorHold.ledge >= 0;
    if (c.staminaFraction() < restBelow || (atLedge && c.staminaFraction() < ledgeRestBelow)) {
        float best = 1e9f;
        for (size_t i = 0; i < holds.size(); ++i) {
            const ClimbHold& h = holds[i];
            if (h.kind != ClimbHold::Kind::Edge || h.ledge < 0 || static_cast<int>(i) == h0 || static_cast<int>(i) == h1) continue;
            const float d = ClimbWall::reachDistance(h.position, from);
            if (d <= reach && d < best && h.position.y > from.y - 0.5f) {
                best = d;
                pick = static_cast<int>(i);
            }
        }
        const bool shake = pick < 0 && anchorHold.kind == ClimbHold::Kind::Jug &&
                           c.wall().leanAt(from.y) < 5.0f;
        for (size_t i = 0; shake && i < holds.size(); ++i) {
            const ClimbHold& h = holds[i];
            if (h.kind != ClimbHold::Kind::Jug || h.loose || static_cast<int>(i) == h0 || static_cast<int>(i) == h1 || c.holdGone(static_cast<int>(i))) continue;
            const float d = ClimbWall::reachDistance(h.position, from);
            if (d > 0.3f && d <= reach * 0.8f && std::abs(h.position.y - from.y) < 0.5f && d < best) {
                best = d;
                pick = static_cast<int>(i);
            }
        }
    }
    // Else the furthest route hold within reach of the anchor.
    // Not past a ledge's edge that wants a rest: stop there.
    int last = static_cast<int>(m_route.size()) - 1;
    if (c.staminaFraction() < ledgeRestBelow)
        for (int i = at + 1; i <= last; ++i) {
            const ClimbHold& h = holds[static_cast<size_t>(m_route[static_cast<size_t>(i)])];
            if (h.kind == ClimbHold::Kind::Edge && h.ledge >= 0) last = i;
        }
    for (int i = last; pick < 0 && i > at; --i) {
        const int hold = m_route[static_cast<size_t>(i)];
        if (hold == anchor || c.holdGone(hold)) continue;
        const glm::vec3& p = holds[static_cast<size_t>(hold)].position;
        if (ClimbWall::reachDistance(p, from) <= reach && p.y > from.y - 0.4f) pick = hold;
    }
    if (pick < 0) {
        // Off the line (a rest took it aside): head back toward the
        // nearest point of it further up.
        float best = 1e9f;
        glm::vec3 toward = from + glm::vec3(0.0f, 1.0f, 0.0f);
        for (int hold : m_route) {
            const glm::vec3& p = holds[static_cast<size_t>(hold)].position;
            const float d = glm::length(glm::vec2(p.x - from.x, p.y - from.y));
            if (p.y > from.y - 0.2f && d < best) {
                best = d;
                toward = p;
            }
        }
        // The hold in reach that gets closest to it (and higher is better).
        float bestTo = 1e9f;
        for (size_t i = 0; i < holds.size(); ++i) {
            const ClimbHold& h = holds[i];
            const int idx = static_cast<int>(i);
            if (idx == h0 || idx == h1 || h.loose || c.holdGone(idx) || h.position.y < from.y - 0.3f) continue;
            if (ClimbWall::reachDistance(h.position, from) > reach) continue;
            const float to = glm::length(glm::vec2(toward.x - h.position.x, toward.y - h.position.y)) - 0.2f * h.position.y;
            if (to < bestTo) {
                bestTo = to;
                pick = idx;
            }
        }
        if (pick < 0) {
            in.reach[move] = true; // whatever the aim (up) finds
            return in;
        }
    }
    // Fresh and on the line: lunge past the next holds when one further
    // along is within a lunge.
    const int pickAt = routeIndex(pick);
    if (lunges && pickAt >= 0 && c.staminaFraction() > 0.7f) {
        const float far = c.settings().lungeSpan * 0.9f;
        for (int i = static_cast<int>(m_route.size()) - 1; i > pickAt; --i) {
            const int hold = m_route[static_cast<size_t>(i)];
            const ClimbHold& h = holds[static_cast<size_t>(hold)];
            if (h.kind == ClimbHold::Kind::Edge || c.holdGone(hold) || h.position.y < from.y) continue;
            if (ClimbWall::reachDistance(h.position, from) > far) continue;
            m_lungeHand = move;
            m_lungePick = hold;
            in.pick[move] = hold;
            in.power[move] = 1.0f;
            return in;
        }
    }
    const glm::vec3& target = holds[static_cast<size_t>(pick)].position;
    const glm::vec3 cur = c.hand(move);
    const glm::vec2 d(target.x - cur.x, target.y - cur.y);
    in.aim = glm::length(d) > 1e-3f ? glm::normalize(d) : glm::vec2(0.0f, 1.0f);
    in.pick[move] = pick;
    in.reach[move] = true;
    // Fresh: a quick snatch (trigger held with the bumper).
    if (c.staminaFraction() > 0.6f) in.power[move] = 0.6f;
    return in;
}

} // namespace kke
