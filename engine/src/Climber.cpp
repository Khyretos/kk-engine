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
    // Both hands may share a hold (matching it), but not fly to the same one.
    const Hand& other = m_hand[1 - h];
    return !(other.move != Move::None && other.target == hold);
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

bool Climber::canSpan(int h, int hold) const { return canHang(h, hold, m_hand[1 - h].hold); }

bool Climber::canHang(int h, int hold, int otherHold) const {
    const auto& holds = m_wall.holds();
    const int n = static_cast<int>(holds.size());
    if (otherHold < 0 || hold < 0 || hold >= n || otherHold >= n) return true;
    const ClimbHold& a = holds[static_cast<size_t>(hold)];
    // A ledge's lip stands out from the rock: a hand may go to it even when
    // the one below can't stay (it cuts loose and the climber hangs from
    // the lip, to match it and mantle), as long as it's in reach as
    // ClimbWall::reachDistance counts it.
    if (a.kind == ClimbHold::Kind::Edge) return true;
    // Hang the body from both and see: every arm in reach (a little
    // short of where the lower hand would cut loose).
    const ClimbHold& b = holds[static_cast<size_t>(otherHold)];
    glm::vec3 wrist[2];
    wrist[h] = wristAt(a.position, a.normal);
    wrist[1 - h] = wristAt(b.position, b.normal);
    glm::vec3 hips = (wrist[0] + wrist[1]) * 0.5f - glm::vec3(0.0f, m_s.shoulderUp + 0.3f, 0.0f);
    hips.z = std::max(hips.z, m_wall.surfaceZ(hips.x, hips.y) + m_s.bodyOut);
    const bool use[2] = { true, true };
    fitWrists(hips, wrist, use);
    for (int k = 0; k < 2; ++k)
        if (glm::length(wrist[k] - shoulderAt(k, hips)) - m_s.armReach > m_s.cutLoose * 0.6f) return false;
    return true;
}

int Climber::findTarget(int h, const glm::vec2& aimIn, float reach, bool dyno) const {
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
        if (!dyno && !canSpan(h, idx)) continue; // the body can't hang between the two
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
            if (glm::length(hd.position - hipJoint(f)) > m_s.legReach * 0.97f) continue; // the leg can't get there
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

glm::vec3 Climber::shoulderAt(int h, const glm::vec3& hips) const {
    const glm::vec3 right = glm::normalize(glm::cross(m_facing, glm::vec3(0.0f, 1.0f, 0.0f)));
    return hips + glm::vec3(0.0f, m_s.shoulderUp, 0.0f) + right * (h == kLeft ? -m_s.shoulderHalf : m_s.shoulderHalf);
}

glm::vec3 Climber::hipJoint(int f) const {
    const glm::vec3 right = glm::normalize(glm::cross(m_facing, glm::vec3(0.0f, 1.0f, 0.0f)));
    return m_hips + right * (f == 0 ? -m_s.hipHalf : m_s.hipHalf);
}

void Climber::keepOffRock(glm::vec3& hips) const {
    hips.z = std::max(hips.z, m_wall.surfaceZ(hips.x, hips.y) + m_s.bodyIn);
}

glm::vec3 Climber::fingerDirection(const glm::vec3& n) const {
    return glm::normalize(glm::vec3(0.0f, 1.0f, 0.0f) - n * m_s.fingerTilt);
}

glm::vec3 Climber::wristAt(const glm::vec3& grip, const glm::vec3& n) const {
    return grip + n * m_s.knuckleOut - fingerDirection(n) * m_s.handLength;
}

void Climber::fitWrists(glm::vec3& hips, const glm::vec3 wrist[2], const bool use[2]) const {
    // Each arm pulls the hips toward its hand until the hand is in reach;
    // with two arms pulling, the pulls are averaged (so a spread neither
    // arm can bridge ends up shared, not all on one arm).
    for (int it = 0; it < 8; ++it) {
        glm::vec3 pull(0.0f);
        int n = 0;
        for (int h = 0; h < 2; ++h) {
            if (!use[h]) continue;
            const glm::vec3 d = wrist[h] - shoulderAt(h, hips);
            const float len = glm::length(d);
            if (len > m_s.armReach) {
                pull += d * (1.0f - m_s.armReach / len);
                ++n;
            }
        }
        if (n == 0) break;
        hips += pull / static_cast<float>(n);
        keepOffRock(hips);
    }
}

void Climber::fitArms(glm::vec3& hips, bool reaching) const {
    const auto& holds = m_wall.holds();
    glm::vec3 wrist[2];
    bool use[2] = { false, false };
    for (int h = 0; h < 2; ++h) {
        const Hand& hd = m_hand[h];
        if (hd.hold >= 0) {
            const ClimbHold& hold = holds[static_cast<size_t>(hd.hold)];
            wrist[h] = wristAt(hold.position, hold.normal);
            use[h] = true;
        } else if (reaching && hd.move != Move::None) {
            wrist[h] = wristAt(hd.pos, hd.target >= 0 ? holds[static_cast<size_t>(hd.target)].normal : m_wall.surfaceNormal(hd.pos.x, hd.pos.y));
            use[h] = true;
        }
    }
    fitWrists(hips, wrist, use);
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
    fitArms(target, true); // where a hand is reaching, the body goes too
    const float rate = fast ? m_s.bodyFollow * 2.2f : m_s.bodyFollow;
    m_hips += (target - m_hips) * (1.0f - std::exp(-rate * dt));
    const glm::vec3 nrm = m_wall.surfaceNormal(m_hips.x, m_hips.y);
    glm::vec3 f(-nrm.x, 0.0f, -nrm.z);
    if (glm::length(f) > 1e-3f) m_facing = glm::normalize(f);

    // Two hands too far apart for any body between them (the moving hand
    // caught something far above): the hand left behind lets go.
    if (m_hand[0].hold >= 0 && m_hand[1].hold >= 0) {
        glm::vec3 best = m_hips;
        fitArms(best, false);
        float over[2];
        for (int h = 0; h < 2; ++h)
            over[h] = glm::length(wristAt(holds[static_cast<size_t>(m_hand[h].hold)].position, holds[static_cast<size_t>(m_hand[h].hold)].normal) -
                                  shoulderAt(h, best)) - m_s.armReach;
        const int low = holds[static_cast<size_t>(m_hand[0].hold)].position.y < holds[static_cast<size_t>(m_hand[1].hold)].position.y ? 0 : 1;
        // (Both on one lip is the way over it: they stay, however stretched.)
        const ClimbHold& ha = holds[static_cast<size_t>(m_hand[0].hold)];
        const ClimbHold& hb = holds[static_cast<size_t>(m_hand[1].hold)];
        const bool lip = ha.kind == ClimbHold::Kind::Edge && hb.kind == ClimbHold::Kind::Edge && ha.ledge == hb.ledge;
        if (!lip && (over[low] > m_s.cutLoose || over[1 - low] > m_s.cutLoose)) {
            m_hand[low].hold = -1;
            m_cut = low;
        }
    }
    // The settling is smooth, but the arms are not elastic: the body stays
    // where the hands on holds can hold it. Just after a catch (a hand
    // took a hold out of the body's reach) it's pulled up to it quickly,
    // not in one frame.
    glm::vec3 held = m_hips;
    fitArms(held, false);
    if (m_grabbed) m_pull = 0.3f;
    m_pull = std::max(0.0f, m_pull - dt);
    const glm::vec3 d = held - m_hips;
    const float step = m_s.pullSpeed * dt, len = glm::length(d);
    m_hips += m_pull > 0.0f && len > step ? d * (step / len) : d;
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
        // A foot smearing on the rock (no hold under it) takes some weight
        // too, as long as the rock isn't overhanging.
        const int smears = lean < 5.0f ? 2 - feetOn : 0;
        drain *= 1.0f - feetShare * (static_cast<float>(feetOn) + 0.5f * static_cast<float>(smears)) * 0.5f;
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
    m_broke = m_cut = -1;
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
        const bool pickOk = picked >= 0 && usable(picked, h) && picked != hd.hold && pickDist <= reach && (hd.charging || canSpan(h, picked));
        m_aim[h] = hd.move != Move::None ? -1 : pickOk ? picked : picked >= 0 ? -1 : findTarget(h, in.aim, reach, hd.charging);
        if (!free) continue;
        if (in.reach[h]) {
            // Bumper: a precise reach, or with the trigger held a quick snatch.
            const bool quick = hd.charging;
            hd.charging = false;
            hd.charge = 0.0f;
            // A hold picked outright that's out of a bumper's reach: no move
            // (the game shows it out of reach), not some other hold.
            const int t = picked >= 0 ? (pickOk && pickDist <= m_s.span && canSpan(h, picked) ? picked : -1) : findTarget(h, in.aim, m_s.span, false);
            if (t >= 0) launch(h, quick ? Move::Quick : Move::Precise, t, in.aim, m_s.span);
        } else if (hd.charging && power < 0.1f) {
            // Trigger let go: throw the hand, as far as the charge carries it.
            if (hd.charge >= 0.08f) launch(h, Move::Lunge, m_aim[h], in.aim, reach);
            hd.charging = false;
            hd.charge = 0.0f;
        }
    }

    updateBody(dt, lunging);
    // A foothold the body has moved away from (the leg can't reach it any
    // more): the feet find new ones now, not after the next big move.
    bool stretched = false;
    for (int f = 0; f < 2; ++f)
        stretched = stretched || (m_foot[f].hold >= 0 && glm::length(m_foot[f].target - hipJoint(f)) > m_s.legReach);
    placeFeet(stretched);
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

std::vector<std::pair<int, int>> ClimbBot::findWay(const Climber& c, int at, int last, float reach) const {
    // Breadth first over (left hold, right hold): each step moves one hand
    // to a hold in reach that the body can hang from with the other. Done
    // when a hand is on the line past `at`: the shortest way, then the
    // furthest along.
    const auto& holds = c.wall().holds();
    struct Node { int l, r, parent, hand, hold; };
    std::vector<Node> nodes{ { c.handHold(0), c.handHold(1), -1, -1, -1 } };
    std::vector<std::pair<int, int>> seen{ { nodes[0].l, nodes[0].r } };
    const glm::vec3 start = (holds[static_cast<size_t>(nodes[0].l)].position + holds[static_cast<size_t>(nodes[0].r)].position) * 0.5f;
    size_t head = 0;
    // Progress is a hold further along than either hand has now.
    int found = -1, foundAt = std::max({ at, routeIndex(nodes[0].l), routeIndex(nodes[0].r) });
    for (int depth = 0; depth < 4 && found < 0; ++depth) {
        const size_t end = nodes.size();
        for (; head < end && nodes.size() < 4000; ++head) {
            const Node n = nodes[head];
            for (int hand = 0; hand < 2; ++hand) {
                const int hang = hand == 0 ? n.r : n.l;
                const glm::vec3& hp = holds[static_cast<size_t>(hang)].position;
                for (size_t i = 0; i < holds.size(); ++i) {
                    const int idx = static_cast<int>(i);
                    const ClimbHold& h = holds[i];
                    if (idx == (hand == 0 ? n.l : n.r) || h.loose || c.holdGone(idx)) continue; // matching the other hand's hold is fine
                    if (h.position.y < start.y - 1.5f || std::abs(h.position.x - start.x) > 3.5f) continue;
                    if (ClimbWall::reachDistance(h.position, hp) > reach || !c.canHang(hand, idx, hang)) continue;
                    const std::pair<int, int> key = hand == 0 ? std::make_pair(idx, n.r) : std::make_pair(n.l, idx);
                    if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
                    seen.push_back(key);
                    nodes.push_back({ key.first, key.second, static_cast<int>(head), hand, idx });
                    const int r = routeIndex(idx);
                    if (r > foundAt && r <= last) {
                        found = static_cast<int>(nodes.size()) - 1;
                        foundAt = std::max(foundAt, r);
                    }
                }
            }
        }
    }
    std::vector<std::pair<int, int>> way;
    for (int n = found; n > 0; n = nodes[static_cast<size_t>(n)].parent)
        way.insert(way.begin(), { nodes[static_cast<size_t>(n)].hand, nodes[static_cast<size_t>(n)].hold });
    return way;
}

Climber::Input ClimbBot::think(const Climber& c, float dt) {
    Climber::Input in = decide(c, dt);
    // Both hands on one ledge's lip, pushing up, is a mantle: when the bot
    // means to climb on past the ledge, its hands go by pick, not up.
    const int h0 = c.handHold(0), h1 = c.handHold(1);
    if (h0 >= 0 && h1 >= 0 && c.state() == Climber::State::Climbing) {
        const auto& holds = c.wall().holds();
        const ClimbHold& a = holds[static_cast<size_t>(h0)];
        const ClimbHold& b = holds[static_cast<size_t>(h1)];
        const bool lip = a.kind == ClimbHold::Kind::Edge && b.kind == ClimbHold::Kind::Edge && a.ledge == b.ledge;
        const bool mantle = a.ledge < 0 || c.staminaFraction() < ledgeRestBelow;
        if (lip && !mantle && (in.pick[0] >= 0 || in.pick[1] >= 0)) in.aim = glm::vec2(0.0f);
    }
    return in;
}

Climber::Input ClimbBot::decide(const Climber& c, float dt) {
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
    // A breath between moves, longer when tired; none with a hand hanging
    // free (a lunge's catch cut the other loose): grab again at once.
    m_wait -= dt;
    if (m_wait > 0.0f && h0 >= 0 && h1 >= 0) return in;
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
    int move = 1 - stay;
    const int anchor = c.handHold(stay);
    if (anchor < 0) return in;
    glm::vec3 from = holds[static_cast<size_t>(anchor)].position;
    const float span = c.wall().desc().routeStep; // the steps the route was built with
    const float reach = std::max(span, 1.0f) + 0.3f;
    int at = routeIndex(anchor);
    // Off the line (started from a ledge, or gone round something): as far
    // along it as the line's holds already below the hands.
    if (at < 0)
        for (size_t i = 0; i < m_route.size(); ++i)
            if (holds[static_cast<size_t>(m_route[i])].position.y < from.y - 0.3f) at = static_cast<int>(i);
    int pick = -1;
    // Following a way round worked out earlier (see below): the next
    // move of it, while it still fits.
    while (!m_path.empty()) {
        const auto [hand, next] = m_path.front();
        m_path.erase(m_path.begin());
        if (c.handHold(1 - hand) < 0 || c.holdGone(next) || !c.canSpan(hand, next)) {
            m_path.clear();
            break;
        }
        m_came[hand] = c.handHold(hand);
        in.pick[hand] = next;
        in.reach[hand] = true;
        const glm::vec3& target = holds[static_cast<size_t>(next)].position;
        const glm::vec3 cur = c.hand(hand);
        const glm::vec2 d(target.x - cur.x, target.y - cur.y);
        in.aim = glm::length(d) > 1e-3f ? glm::normalize(d) : glm::vec2(0.0f, 1.0f);
        return in;
    }
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
            if (d <= reach && d < best && h.position.y > from.y - 0.5f && c.canSpan(move, static_cast<int>(i))) {
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
            if (d > 0.3f && d <= reach * 0.8f && std::abs(h.position.y - from.y) < 0.5f && d < best && c.canSpan(move, static_cast<int>(i))) {
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
        if (hold == anchor || hold == c.handHold(move) || c.holdGone(hold)) continue;
        const glm::vec3& p = holds[static_cast<size_t>(hold)].position;
        if (ClimbWall::reachDistance(p, from) <= reach && p.y > from.y - 0.4f && c.canSpan(move, hold)) pick = hold;
    }
    // Nothing on the line this hand can take with the body hanging from
    // the other one: maybe the other hand can go on while this one holds.
    if (pick < 0 && c.handHold(move) >= 0) {
        const int anchor2 = c.handHold(move);
        const glm::vec3 from2 = holds[static_cast<size_t>(anchor2)].position;
        for (int i = last; pick < 0 && i > at; --i) {
            const int hold = m_route[static_cast<size_t>(i)];
            if (hold == anchor || hold == anchor2 || c.holdGone(hold)) continue;
            const glm::vec3& p = holds[static_cast<size_t>(hold)].position;
            if (ClimbWall::reachDistance(p, from2) <= reach && p.y > from2.y - 0.4f && c.canSpan(stay, hold)) pick = hold;
        }
        if (pick >= 0) {
            move = stay;
            from = from2;
        }
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
        // The hold in reach that gets closest to it (and higher is better),
        // for hand `mover` with the body hanging from `hang`.
        auto closer = [&](int mover, const glm::vec3& hang, float drop) {
            int found = -1;
            float bestTo = 1e9f;
            for (size_t i = 0; i < holds.size(); ++i) {
                const ClimbHold& h = holds[i];
                const int idx = static_cast<int>(i);
                if (idx == h0 || idx == h1 || idx == m_came[mover] || h.loose || c.holdGone(idx) || h.position.y < hang.y - drop) continue;
                if (ClimbWall::reachDistance(h.position, hang) > reach || !c.canSpan(mover, idx)) continue;
                const float to = glm::length(glm::vec2(toward.x - h.position.x, toward.y - h.position.y)) - 0.2f * h.position.y;
                if (to < bestTo) {
                    bestTo = to;
                    found = idx;
                }
            }
            return found;
        };
        // No single move gets on: search a few moves ahead (either hand,
        // any hold the body can hang from) for a way round to the line.
        if (h0 >= 0 && h1 >= 0) {
            m_path = findWay(c, at, last, reach);
            if (!m_path.empty()) {
                move = m_path.front().first;
                pick = m_path.front().second;
                from = holds[static_cast<size_t>(c.handHold(1 - move))].position;
                m_path.erase(m_path.begin());
            }
        }
        // Across or up first; down a move if that's the only way (into a
        // corner with no holds above: climb down out of it).
        for (float drop : { 0.3f, 1.3f }) {
            if (pick >= 0) break;
            pick = closer(move, from, drop);
            // This hand can't get closer with the body hanging where it is:
            // the other hand goes instead (a shuffle across, say).
            if (pick < 0 && c.handHold(move) >= 0) {
                const glm::vec3 from2 = holds[static_cast<size_t>(c.handHold(move))].position;
                pick = closer(stay, from2, drop);
                if (pick >= 0) {
                    move = stay;
                    from = from2;
                }
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
    if (lunges && pickAt >= 0 && c.staminaFraction() > 0.85f) {
        const float far = c.settings().lungeSpan * 0.9f;
        for (int i = last; i > pickAt; --i) { // not past a ledge where a rest is due
            const int hold = m_route[static_cast<size_t>(i)];
            const ClimbHold& h = holds[static_cast<size_t>(hold)];
            if (h.kind == ClimbHold::Kind::Edge || hold == c.handHold(move) || hold == c.handHold(1 - move) || c.holdGone(hold) || h.position.y < from.y) continue;
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
    m_came[move] = c.handHold(move); // not straight back there when finding a way round
    // Fresh: a quick snatch (trigger held with the bumper).
    if (c.staminaFraction() > 0.6f) in.power[move] = 0.6f;
    return in;
}

} // namespace kke
