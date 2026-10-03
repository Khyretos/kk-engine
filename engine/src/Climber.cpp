#include "kke/Climber.h"

#include <algorithm>
#include <cmath>

namespace kke {

namespace {
constexpr float kGravity = 9.81f;
constexpr float kPi = 3.14159265f;
const glm::vec3 kUp(0.0f, 1.0f, 0.0f);

float smooth(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
float sideOf(int h) { return h == Climber::kLeft ? -1.0f : 1.0f; }
// The climber's right for a body facing `facing` (into the rock).
glm::vec3 rightOf(const glm::vec3& facing) { return glm::normalize(glm::cross(facing, kUp)); }
} // namespace

Climber::Climber(const ClimbWall& wall) : Climber(wall, Settings{}) {}

Climber::Climber(const ClimbWall& wall, const Settings& settings) : m_wall(wall), m_s(settings) { m_stamina = m_s.maxStamina; }

bool Climber::holdGone(int hold) const { return std::find(m_gone.begin(), m_gone.end(), hold) != m_gone.end(); }

bool Climber::usable(int hold, int h) const {
    if (hold < 0 || hold >= static_cast<int>(m_wall.holds().size()) || holdGone(hold)) return false;
    // Both hands may share a hold (matching it), but not fly to the same one.
    const Hand& other = m_hand[1 - h];
    if (other.move != Move::None && other.target == hold) return false;
    // A foot is on it (or stepping there).
    for (const Foot& f : m_foot)
        if (f.hold == hold || f.next == hold) return false;
    // The arms don't cross (canHang also keeps the hand on its side of the
    // body the two would hang from).
    return !crossesOver(h, hold);
}

// Sideways is along the face (x: the aim's right), not the body's facing,
// which turns a little with the rock: the holds' depth (an overhang) must
// not count as sideways.
bool Climber::crossesOver(int h, int hold) const {
    const Hand& other = m_hand[1 - h];
    if (m_state != State::Climbing || other.hold < 0 || hold < 0) return false;
    // The other hand's hold is at least out at its own shoulder, or the
    // body's middle is further over still.
    const float over = pointOn(h, hold, m_hips).x - grip(1 - h).x;
    return over * sideOf(h) < -(2.0f * m_s.shoulderHalf + m_s.crossReach);
}

float Climber::armPath(int h, const glm::vec3& wrist, const glm::vec3& hips) const {
    const glm::vec3 own = shoulderAt(h, hips);
    // Past the other shoulder the arm can't go straight: it goes round the
    // front of the chest (just in front of the other shoulder), then on.
    const glm::vec3 other = shoulderAt(1 - h, hips);
    if ((wrist.x - other.x) * sideOf(h) >= 0.0f) return glm::length(wrist - own);
    const glm::vec3 round = other + m_facing * 0.15f;
    return glm::length(round - own) + glm::length(wrist - round);
}

bool Climber::onItsSide(int h, const glm::vec3& point, const glm::vec3& hips) const {
    return (point.x - hips.x) * sideOf(h) >= -(m_s.shoulderHalf + m_s.crossReach);
}

float Climber::handProgress(int h) const {
    const Hand& hd = m_hand[h];
    return hd.move == Move::None || hd.duration <= 0.0f ? 1.0f : std::clamp(hd.t / hd.duration, 0.0f, 1.0f);
}

float Climber::mantleProgress() const {
    return m_state == State::Mantle ? std::clamp(m_mantleT / m_s.mantleTime, 0.0f, 1.0f) : m_state == State::Topped ? 1.0f : 0.0f;
}

// Reach is measured from the hold the *other* hand is on (the one the
// body hangs from while this hand moves).
glm::vec3 Climber::pivot(int h) const {
    const Hand& other = m_hand[1 - h];
    if (other.hold >= 0 && other.move == Move::None) return grip(1 - h);
    return m_hand[h].pos;
}

// A ledge's (or the summit's) lip can be taken anywhere along it: each
// edge hold stands for its own stretch of the lip (its size either side),
// and a hand takes it under its shoulder.
glm::vec3 Climber::pointOn(int h, int hold, const glm::vec3& hips) const {
    const ClimbHold& hd = m_wall.holds()[static_cast<size_t>(hold)];
    if (hd.kind != ClimbHold::Kind::Edge) return hd.position;
    float x = std::clamp(shoulderAt(h, hips).x, hd.position.x - hd.size - 0.03f, hd.position.x + hd.size + 0.03f);
    if (hd.ledge >= 0) {
        const ClimbLedge& l = m_wall.ledges()[static_cast<size_t>(hd.ledge)];
        x = std::clamp(x, l.center.x - l.halfExtents.x + 0.1f, l.center.x + l.halfExtents.x - 0.1f);
        return glm::vec3(x, hd.position.y, hd.position.z);
    }
    return glm::vec3(x, hd.position.y, m_wall.surfaceZ(x, hd.position.y) - 0.03f);
}

glm::vec3 Climber::gripAt(int h, int hold, float slide, float share) const {
    const ClimbHold& hd = m_wall.holds()[static_cast<size_t>(hold)];
    glm::vec3 p = hd.position;
    if (hd.kind == ClimbHold::Kind::Edge) {
        p.x = slide;
        if (hd.ledge < 0) p.z = m_wall.surfaceZ(slide, hd.position.y) - 0.03f;
    }
    // Two hands on one hold sit side by side on it, the left one on the
    // left (never one inside the other).
    if (share > 0.0f) p += rightOf(-hd.normal) * (sideOf(h) * m_s.handWidth * 0.5f * share);
    return p;
}

glm::vec3 Climber::grip(int h) const {
    const Hand& hd = m_hand[h];
    if (hd.hold < 0) return hd.pos;
    return gripAt(h, hd.hold, hd.slide, hd.share);
}

bool Climber::onOneLip() const {
    const int a = m_hand[0].hold, b = m_hand[1].hold;
    if (a < 0 || b < 0 || m_hand[0].move != Move::None || m_hand[1].move != Move::None) return false;
    const ClimbHold& ha = m_wall.holds()[static_cast<size_t>(a)];
    const ClimbHold& hb = m_wall.holds()[static_cast<size_t>(b)];
    return ha.kind == ClimbHold::Kind::Edge && hb.kind == ClimbHold::Kind::Edge && ha.ledge == hb.ledge;
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
            const float side = sideOf(h);
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
    keepOffRock(m_hips);
    m_dyno = Dyno{};
    m_jumpWas = true; // a jump held from the ground isn't a lunge
    for (int h = 0; h < 2; ++h) {
        Hand& hd = m_hand[h];
        hd = Hand{};
        hd.pos = m_hips + glm::vec3(sideOf(h) * 0.25f, 0.55f, -0.1f);
        if (best[h] >= 0) {
            hd.move = Move::Precise;
            hd.from = hd.pos;
            hd.target = best[h];
            hd.slide = pointOn(h, best[h], m_hips).x;
            hd.to = gripAt(h, best[h], hd.slide, 0.0f);
            hd.duration = m_s.quickTime * 1.5f;
        }
    }
    // The feet on the best footholds under the body.
    for (int f = 0; f < 2; ++f) m_foot[f] = Foot{};
    for (int f = 0; f < 2; ++f) {
        Foot& ft = m_foot[f];
        const int hold = findFoothold(f, glm::vec2(0.0f));
        ft.hold = hold;
        ft.pos = ft.target = hold >= 0 ? holds[static_cast<size_t>(hold)].position : hangingFoot(f);
        if (hold < 0) ft.pos = ft.target = glm::vec3(feet.x + sideOf(f) * m_s.hipHalf, feet.y, feet.z);
    }
    m_mantleLedge = -1;
    return true;
}

bool Climber::canSpan(int h, int hold) const { return canHang(h, hold, m_hand[1 - h].hold); }

bool Climber::canHang(int h, int hold, int otherHold) const {
    const auto& holds = m_wall.holds();
    const int n = static_cast<int>(holds.size());
    if (otherHold < 0 || hold < 0 || hold >= n || otherHold >= n) return true;
    const ClimbHold& a = holds[static_cast<size_t>(hold)];
    const ClimbHold& b = holds[static_cast<size_t>(otherHold)];
    glm::vec3 pa = pointOn(h, hold, m_hips);
    glm::vec3 pb = otherHold == m_hand[1 - h].hold ? gripAt(1 - h, otherHold, m_hand[1 - h].slide, 0.0f) : pointOn(1 - h, otherHold, m_hips);
    if (hold == otherHold) {
        pa = gripAt(h, hold, pa.x, 1.0f);
        pb = gripAt(1 - h, hold, pb.x, 1.0f);
    }
    // Never across the other hand.
    if ((pa.x - pb.x) * sideOf(h) < -(2.0f * m_s.shoulderHalf + m_s.crossReach)) return false;
    // A ledge's lip stands out from the rock: a hand may go to it even when
    // the one below can't stay (it cuts loose and the climber hangs from
    // the lip, to match it and mantle), as long as it's in reach as
    // ClimbWall::reachDistance counts it.
    if (a.kind == ClimbHold::Kind::Edge) return true;
    // Hang the body from both and see: every arm in reach (a little
    // short of where the lower hand would cut loose).
    glm::vec3 wrist[2];
    wrist[h] = wristAt(pa, a.normal);
    wrist[1 - h] = wristAt(pb, b.normal);
    glm::vec3 hips = (wrist[0] + wrist[1]) * 0.5f - glm::vec3(0.0f, m_s.shoulderUp + 0.3f, 0.0f);
    hips.z = std::max(hips.z, m_wall.surfaceZ(hips.x, hips.y) + m_s.bodyOut);
    const bool use[2] = { true, true };
    fitWrists(hips, wrist, use);
    for (int k = 0; k < 2; ++k) {
        if (armPath(k, wrist[k], hips) - m_s.armReach > m_s.cutLoose * 0.6f) return false;
        if (!onItsSide(k, wrist[k], hips)) return false;
    }
    return true;
}

int Climber::findTarget(int h, const glm::vec2& aimIn, float reach) const {
    const auto& holds = m_wall.holds();
    const glm::vec3 piv = pivot(h);
    const Hand& hd = m_hand[h];
    const Hand& other = m_hand[1 - h];
    const glm::vec3 ref = hd.hold >= 0 ? grip(h) : hd.pos;
    glm::vec2 aim = aimIn;
    const float strength = glm::length(aim);
    aim = strength < 0.2f ? glm::vec2(0.0f, 1.0f) : aim / strength;
    const float side = sideOf(h);
    const float otherX = other.hold >= 0 ? grip(1 - h).x : m_hips.x;
    int best = -1;
    float bestScore = -1e9f;
    for (size_t i = 0; i < holds.size(); ++i) {
        const int idx = static_cast<int>(i);
        if (idx == hd.hold || !usable(idx, h)) continue;
        const glm::vec3 p = pointOn(h, idx, m_hips);
        if (ClimbWall::reachDistance(p, piv) > reach) continue;
        if (!canSpan(h, idx)) continue; // the body can't hang between the two
        // Hands may cross a little, not swap sides.
        if ((p.x - otherX) * side < -0.5f) continue;
        if (p.y < m_hips.y - 0.6f) continue; // down there is for feet
        const glm::vec2 v(p.x - ref.x, p.y - ref.y);
        const float len = glm::length(v);
        if (len < 0.08f && idx != other.hold) continue;
        const float c = len < 0.08f ? 0.5f : glm::dot(v / len, aim);
        if (c < 0.35f) continue;
        float s = c * c * 1.5f - 0.35f * std::abs(len - 0.75f) + 0.15f * holdGrip(holds[i].kind);
        if (idx == other.hold) s -= 0.4f; // matching: only when nothing else is up there
        if (holds[i].kind == ClimbHold::Kind::Edge && aim.y > 0.5f) s += 0.25f; // up and over
        if (s > bestScore) {
            bestScore = s;
            best = idx;
        }
    }
    return best;
}

int Climber::catchTarget(int h, const glm::vec3& hips) const {
    const auto& holds = m_wall.holds();
    const glm::vec3 sh = shoulderAt(h, hips);
    const glm::vec3 right = rightOf(m_facing);
    // Where a hand reaching for a catch is most at home: up, a little out.
    const glm::vec3 ideal = sh + kUp * (m_s.armReach * 0.85f) + right * (sideOf(h) * 0.1f);
    int best = -1;
    float bestScore = 1e9f;
    for (size_t i = 0; i < holds.size(); ++i) {
        const int idx = static_cast<int>(i);
        if (holdGone(idx) || holds[i].position.y < hips.y + 0.2f) continue;
        const glm::vec3 p = pointOn(h, idx, hips);
        if (std::abs(p.x - sh.x) > m_s.catchReach || std::abs(p.y - sh.y) > m_s.catchReach) continue;
        const glm::vec3 wrist = wristAt(p, holds[i].normal);
        if (glm::length(wrist - sh) > m_s.catchReach || !onItsSide(h, p, hips)) continue;
        const float s = glm::length(wrist - ideal) - 0.15f * holdGrip(holds[i].kind);
        if (s < bestScore) {
            bestScore = s;
            best = idx;
        }
    }
    return best;
}

int Climber::findFoothold(int f, const glm::vec2& aim) const {
    const auto& holds = m_wall.holds();
    const float side = sideOf(f);
    // Feet up under the body, knees bent (not hanging straight), a little
    // toward where the stick points.
    const glm::vec2 ideal(m_hips.x + side * 0.22f + std::clamp(aim.x, -1.0f, 1.0f) * 0.15f, m_hips.y - 0.62f);
    const Foot& other = m_foot[1 - f];
    int best = -1;
    float bestD = 1e9f;
    for (size_t i = 0; i < holds.size(); ++i) {
        const ClimbHold& hd = holds[i];
        const int idx = static_cast<int>(i);
        if (hd.kind == ClimbHold::Kind::Edge || holdGone(idx)) continue;
        if (idx == other.hold || idx == other.next || idx == m_hand[0].hold || idx == m_hand[1].hold || idx == m_hand[0].target ||
            idx == m_hand[1].target)
            continue;
        const float dx = (hd.position.x - m_hips.x) * side, dy = m_hips.y - hd.position.y;
        if (dx < -0.12f || dx > 0.6f || dy < 0.25f || dy > 0.95f) continue;
        if (glm::length(hd.position - hipJoint(f)) > m_s.legReach * 0.97f) continue; // the leg can't get there
        const float d = glm::length(glm::vec2(hd.position.x, hd.position.y) - ideal);
        if (d < bestD) {
            bestD = d;
            best = idx;
        }
    }
    return best;
}

void Climber::launch(int h, int target, const glm::vec2& aim, float reach) {
    Hand& hd = m_hand[h];
    const Hand& other = m_hand[1 - h];
    // The body hangs from the other hand while this one moves.
    if (other.hold < 0 || other.move != Move::None) return;
    hd.from = hd.pos;
    hd.target = target;
    const bool match = target >= 0 && target == other.hold;
    if (target >= 0) {
        hd.slide = pointOn(h, target, m_hips).x;
        const ClimbHold& t = m_wall.holds()[static_cast<size_t>(target)];
        const bool side = match && (t.kind != ClimbHold::Kind::Edge || std::abs(hd.slide - other.slide) < m_s.handWidth);
        hd.to = gripAt(h, target, hd.slide, side ? 1.0f : 0.0f);
    } else {
        // A throw at nothing: the hand goes where it was aimed and closes on air.
        const glm::vec2 a = glm::length(aim) > 0.2f ? glm::normalize(aim) : glm::vec2(0.0f, 1.0f);
        const glm::vec3 piv = pivot(h);
        hd.to = m_wall.surfacePoint(piv.x + a.x * reach, piv.y + a.y * reach, 0.1f);
    }
    hd.hold = -1;
    hd.share = 0.0f;
    hd.move = Move::Precise;
    hd.t = 0.0f;
    hd.duration = m_s.reachTime;
    float cost = m_s.costPrecise;
    // Matching (onto the hold the other hand is on) is a short, easy move:
    // how a climber swaps hands to go on across, so it costs little.
    if (match) {
        cost *= 0.5f;
        hd.duration = std::min(hd.duration, m_s.quickTime * 1.5f);
    }
    m_stamina -= cost * costFactor();
}

void Climber::land(int h) {
    Hand& hd = m_hand[h];
    hd.move = Move::None;
    hd.pos = hd.to;
    if (hd.target < 0 || holdGone(hd.target)) {
        m_missed = true;
        hd.target = -1;
        return;
    }
    hd.hold = hd.target;
    hd.target = -1;
    // Beside the other hand on a shared hold from the start (it arrived there).
    hd.share = glm::length(hd.to - gripAt(h, hd.hold, hd.slide, 0.0f)) > 1e-4f ? 1.0f : 0.0f;
    m_grabbed = true;
}

void Climber::fall() {
    m_state = State::Fell;
    m_fellNow = true;
    for (Hand& hd : m_hand) {
        hd.hold = -1;
        hd.move = Move::None;
        hd.share = 0.0f;
    }
    for (Foot& f : m_foot) {
        f.hold = f.next = -1;
        f.t = 1e9f;
    }
    m_dyno = Dyno{};
    m_aim[0] = m_aim[1] = -1;
    m_footAim[0] = m_footAim[1] = -1;
}

glm::vec3 Climber::shoulderAt(int h, const glm::vec3& hips) const {
    return hips + glm::vec3(0.0f, m_s.shoulderUp, 0.0f) + rightOf(m_facing) * (sideOf(h) * m_s.shoulderHalf);
}

glm::vec3 Climber::hipJointAt(int f, const glm::vec3& hips) const { return hips + rightOf(m_facing) * (sideOf(f) * m_s.hipHalf); }

glm::vec3 Climber::hangingFoot(int f) const {
    // Straight down from the hip, a little out from the rock, never in it.
    glm::vec3 p = hipJoint(f) - kUp * (m_s.legReach * 0.92f) + rightOf(m_facing) * (sideOf(f) * 0.04f) - m_facing * 0.06f;
    p.z = std::max(p.z, m_wall.surfaceZ(p.x, p.y) + 0.12f);
    return p;
}

void Climber::keepOffRock(glm::vec3& hips) const {
    // Out of every ledge: a body (feet to the top of the head) whose
    // height overlaps a ledge's stays in front of it. Under a roof, the
    // head doesn't go into it; hanging from a lip, the body hangs in front.
    // (Coming within a hand's breadth of it, the body already leans out,
    // so it never jumps out in one frame.)
    constexpr float kRamp = 0.15f;
    for (size_t li = 0; li < m_wall.ledges().size(); ++li) {
        const ClimbLedge& l = m_wall.ledges()[li];
        if (std::abs(hips.x - l.center.x) > l.halfExtents.x + m_s.shoulderHalf) continue;
        const float low = hips.y - m_s.hipsHeight + 0.05f, high = hips.y + m_s.headUp;
        const float bottom = l.center.y - l.halfExtents.y, top = l.top();
        // (Above it, the feet on its top or over it, is standing on it.)
        if (low > top - 0.02f) continue;
        const float front = l.center.z + l.halfExtents.z + m_s.bodyDepth + 0.04f;
        // Over its top with the feet sunk into it (climbing on from it, the
        // hands pulling the body down): the feet stay on top.
        if (hips.z < front - 0.05f && hips.y - m_s.hipsHeight > top - 0.45f) {
            hips.y = top + m_s.hipsHeight - 0.06f;
            continue;
        }
        // Under its roof, holding the rock: the head stays under it (the
        // body hangs lower). Holding its lip: the body hangs in front.
        bool onLip = false;
        for (const Hand& hd : m_hand)
            if (hd.hold >= 0) {
                const ClimbHold& e = m_wall.holds()[static_cast<size_t>(hd.hold)];
                onLip = onLip || (e.kind == ClimbHold::Kind::Edge && e.ledge == static_cast<int>(li));
            }
        if (!onLip && hips.z < front - 0.05f) {
            hips.y = std::min(hips.y, bottom - m_s.headUp - 0.02f);
            continue;
        }
        const float gap = high < bottom ? bottom - high : 0.0f;
        if (gap >= kRamp) continue;
        const float rock = m_wall.surfaceZ(hips.x, hips.y) + m_s.bodyIn;
        hips.z = std::max(hips.z, rock + (front - rock) * smooth(1.0f - gap / kRamp));
    }
    // The hips, the chest and the head clear of the rock (an overhang
    // leans out over the upper body).
    hips.z = std::max(hips.z, m_wall.surfaceZ(hips.x, hips.y) + m_s.bodyIn);
    hips.z = std::max(hips.z, m_wall.surfaceZ(hips.x, hips.y + m_s.shoulderUp) + m_s.bodyIn);
    hips.z = std::max(hips.z, m_wall.surfaceZ(hips.x, hips.y + m_s.headUp - 0.1f) + 0.13f);
}

glm::vec3 Climber::fingerDirection(const glm::vec3& n) const {
    return glm::normalize(kUp - n * m_s.fingerTilt);
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
            wrist[h] = wristAt(grip(h), holds[static_cast<size_t>(hd.hold)].normal);
            use[h] = true;
        } else if (reaching && hd.move != Move::None) {
            wrist[h] = wristAt(hd.pos, hd.target >= 0 ? holds[static_cast<size_t>(hd.target)].normal : m_wall.surfaceNormal(hd.pos.x, hd.pos.y));
            use[h] = true;
        }
    }
    fitWrists(hips, wrist, use);
}

void Climber::updateBody(float dt) {
    glm::vec3 anchor(0.0f);
    int n = 0;
    for (int h = 0; h < 2; ++h)
        if (m_hand[h].hold >= 0) {
            anchor += grip(h);
            ++n;
        }
    if (n == 0) {
        keepOffRock(m_hips); // both hands on their way (the first grab)
        return;
    }
    anchor /= static_cast<float>(n);
    // Hanging from one hand the body swings under it, a little to that side.
    if (n == 1) anchor.x += m_hand[0].hold >= 0 ? 0.12f : -0.12f;
    glm::vec3 target = anchor - glm::vec3(0.0f, m_s.hang, 0.0f);
    // Off the rock: at least bodyOut in front of it (hanging straight
    // down under an overhang, close in on a slab).
    const float rockZ = m_wall.surfaceZ(target.x, target.y);
    target.z = std::max(rockZ + m_s.bodyOut, std::min(anchor.z + 0.1f, rockZ + m_s.bodyOut + 0.6f));
    keepOffRock(target);
    fitArms(target, true); // where a hand is reaching, the body goes too
    m_hips += (target - m_hips) * (1.0f - std::exp(-m_s.bodyFollow * dt));
    const glm::vec3 nrm = m_wall.surfaceNormal(m_hips.x, m_hips.y);
    glm::vec3 f(-nrm.x, 0.0f, -nrm.z);
    if (glm::length(f) > 1e-3f) m_facing = glm::normalize(f);

    // Two hands too far apart for any body between them: the hand left
    // behind lets go.
    if (m_hand[0].hold >= 0 && m_hand[1].hold >= 0 && !onOneLip()) {
        const auto& holds = m_wall.holds();
        glm::vec3 best = m_hips;
        fitArms(best, false);
        float over[2];
        for (int h = 0; h < 2; ++h)
            over[h] = glm::length(wristAt(grip(h), holds[static_cast<size_t>(m_hand[h].hold)].normal) - shoulderAt(h, best)) - m_s.armReach;
        const int low = grip(0).y < grip(1).y ? 0 : 1;
        if (over[low] > m_s.cutLoose || over[1 - low] > m_s.cutLoose) {
            m_hand[low].hold = -1;
            m_hand[low].share = 0.0f;
            m_cut = low;
        }
    }
    // The settling is smooth, but the arms are not elastic: the body stays
    // where the hands on holds can hold it. Just after a catch (a hand
    // took a hold out of the body's reach) it's pulled up to it quickly,
    // not in one frame.
    glm::vec3 held = m_hips;
    keepOffRock(held);
    fitArms(held, false);
    if (m_grabbed) m_pull = 0.3f;
    m_pull = std::max(0.0f, m_pull - dt);
    const glm::vec3 d = held - m_hips;
    const float step = m_s.pullSpeed * dt, len = glm::length(d);
    m_hips += m_pull > 0.0f && len > step ? d * (step / len) : d;
    keepOffRock(m_hips); // on the way up to the hand, too
}

void Climber::updateFeet(const Input& in, float dt) {
    const auto& holds = m_wall.holds();
    for (int f = 0; f < 2; ++f) {
        Foot& ft = m_foot[f];
        // What the bumper would step onto.
        const int picked = in.footPick[f];
        const bool pickOk = picked >= 0 && picked < static_cast<int>(holds.size()) && !holdGone(picked) &&
                            holds[static_cast<size_t>(picked)].kind != ClimbHold::Kind::Edge && picked != m_foot[1 - f].hold &&
                            picked != m_foot[1 - f].next && glm::length(holds[static_cast<size_t>(picked)].position - hipJoint(f)) <= m_s.legReach * 0.97f;
        m_footAim[f] = m_dyno.flying ? -1 : pickOk ? picked : picked >= 0 ? -1 : findFoothold(f, in.aim);
        if (!m_dyno.flying && in.step[f] && m_footAim[f] >= 0 && m_footAim[f] != ft.hold && ft.next < 0) {
            ft.from = ft.pos;
            ft.next = m_footAim[f];
            ft.target = holds[static_cast<size_t>(ft.next)].position;
            ft.hold = -1;
            ft.t = 0.0f;
            m_stamina -= m_s.costStep * costFactor();
        }
        if (ft.next >= 0) {
            ft.t += dt;
            // The body moved on while the foot was on its way: it can't get there.
            if (glm::length(ft.target - hipJoint(f)) > m_s.legReach) {
                ft.next = -1;
                ft.t = 1e9f;
            } else {
                const float s = smooth(ft.t / m_s.stepTime);
                const glm::vec3 n = m_wall.surfaceNormal(ft.target.x, ft.target.y);
                ft.pos = ft.from + (ft.target - ft.from) * s + n * (0.08f * std::sin(kPi * s));
                if (ft.t >= m_s.stepTime) {
                    ft.hold = ft.next;
                    ft.next = -1;
                    ft.pos = ft.target;
                }
            }
        }
        if (ft.hold >= 0) {
            ft.target = ft.pos = holds[static_cast<size_t>(ft.hold)].position;
            // The body has moved away from it (the leg can't reach): the
            // foot comes off and hangs.
            if (holdGone(ft.hold) || glm::length(ft.pos - hipJoint(f)) > m_s.legReach + 0.02f) {
                ft.hold = -1;
                m_slipped = f;
            }
        }
        if (ft.hold < 0 && ft.next < 0) ft.pos += (hangingFoot(f) - ft.pos) * (1.0f - std::exp(-10.0f * dt));
    }
}

glm::vec3 Climber::lungeApexFor(const glm::vec2& aimIn, float charge) const {
    glm::vec2 dir = glm::length(aimIn) > 0.2f ? glm::normalize(aimIn) : glm::vec2(0.0f, 1.0f);
    dir.y = std::max(dir.y, -0.3f); // not down the rock: that's a fall
    dir = glm::normalize(dir);
    const float d = m_s.dynoMin + (m_s.dynoMax - m_s.dynoMin) * std::clamp(charge, 0.0f, 1.0f);
    glm::vec3 apex = m_hips + rightOf(m_facing) * (dir.x * d) + kUp * (dir.y * d);
    // Along the rock, as far out from it as the body hangs.
    apex.z = m_wall.surfaceZ(apex.x, apex.y) + m_s.bodyOut;
    keepOffRock(apex);
    return apex;
}

glm::vec3 Climber::lungeApex() const {
    if (m_dyno.flying) return m_dyno.apex;
    return lungeApexFor(m_dyno.aim, m_dyno.charge);
}

void Climber::updateLunge(const Input& in, float dt) {
    const bool pressed = in.jump && !m_jumpWas;
    m_jumpWas = in.jump;
    Dyno& d = m_dyno;
    if (!d.flying) {
        // Both hands on one lip: a jump is over the top.
        if (pressed && onOneLip()) {
            startMantle();
            return;
        }
        const bool holding = m_hand[0].hold >= 0 || m_hand[1].hold >= 0;
        const bool still = m_hand[0].move == Move::None && m_hand[1].move == Move::None;
        if (!holding || !still) {
            d.charging = false;
            d.charge = 0.0f;
            return;
        }
        if (pressed) {
            d.charging = true;
            d.charge = 0.0f;
        }
        if (d.charging && in.jump) {
            d.charge = std::min(1.0f, d.charge + dt / m_s.chargeTime);
            if (glm::length(in.aim) > 0.2f) d.aim = in.aim;
            return;
        }
        if (!d.charging) return;
        // Let go. A tap is nothing (no hop on the spot).
        d.charging = false;
        if (d.charge < m_s.minCharge) {
            d.charge = 0.0f;
            return;
        }
        const float cost = (m_s.costDynoMin + (m_s.costDynoMax - m_s.costDynoMin) * d.charge) * costFactor();
        d.apex = lungeApexFor(d.aim, d.charge);
        glm::vec2 dir = glm::length(d.aim) > 0.2f ? glm::normalize(d.aim) : glm::vec2(0.0f, 1.0f);
        d.aim = dir;
        d.from = m_hips;
        d.t = 0.0f;
        d.rise = m_s.dynoRise * (0.7f + 0.5f * d.charge);
        d.flying = true;
        m_stamina -= cost;
        m_lunged = true;
        // Off the rock: both hands and both feet.
        for (Hand& hd : m_hand) {
            hd.hold = -1;
            hd.move = Move::None;
            hd.share = 0.0f;
            hd.retry = 0.0f;
        }
        for (Foot& f : m_foot) {
            f.hold = f.next = -1;
            f.t = 1e9f;
        }
        return;
    }

    // In the air: up to the dead point, slowing; then down.
    d.t += dt;
    if (d.t < d.rise) {
        const float s = d.t / d.rise;
        m_hips = d.from + (d.apex - d.from) * (1.0f - (1.0f - s) * (1.0f - s));
    } else {
        const float tf = d.t - d.rise;
        m_hips = d.apex - glm::vec3(0.0f, 0.5f * kGravity * tf * tf, 0.0f);
    }
    keepOffRock(m_hips);
    const glm::vec3 right = rightOf(m_facing);
    for (int h = 0; h < 2; ++h) {
        Hand& hd = m_hand[h];
        hd.retry = std::max(0.0f, hd.retry - dt);
        // The hands up and out toward the jump, ready to catch.
        const glm::vec3 dirUp = glm::normalize(kUp * 0.9f + right * (d.aim.x * 0.35f + sideOf(h) * 0.2f) - m_facing * 0.1f);
        const glm::vec3 ready = shoulderAt(h, m_hips) + dirUp * (m_s.armReach * 0.95f);
        hd.pos += (ready - hd.pos) * (1.0f - std::exp(-18.0f * dt));
        m_aim[h] = catchTarget(h, m_hips);
    }
    for (int h = 0; h < 2; ++h) {
        Hand& hd = m_hand[h];
        if (!in.reach[h] || hd.retry > 0.0f) continue;
        const int t = m_aim[h];
        if (t < 0) {
            m_missed = true; // grabbed at air: a moment before it can try again
            hd.retry = 0.2f;
            continue;
        }
        const ClimbHold& hold = m_wall.holds()[static_cast<size_t>(t)];
        if (hold.loose) {
            // Hit hard, it comes off the rock: the hand closes on nothing.
            m_gone.push_back(t);
            m_broke = t;
            m_missed = true;
            hd.retry = 0.2f;
            continue;
        }
        hd.hold = t;
        hd.slide = pointOn(h, t, m_hips).x;
        hd.share = 0.0f;
        hd.pos = grip(h);
        m_grabbed = true;
        d = Dyno{};
        d.aim = glm::vec2(0.0f, 1.0f);
        break;
    }
    if (d.flying && d.t > d.rise + m_s.catchWindow) fall();
}

void Climber::updateStamina(float dt) {
    const auto& holds = m_wall.holds();
    int n = 0;
    float grip = 0.0f;
    // A hand on its way to a hold is part of the move (its cost was paid
    // at launch): it counts as half a hand, so reaching isn't punished
    // on top. Only a hand hanging free leaves the other alone.
    bool reaching = false;
    for (const Hand& hd : m_hand) {
        reaching = reaching || hd.move != Move::None;
        if (hd.hold >= 0) {
            grip += holdGrip(holds[static_cast<size_t>(hd.hold)].kind);
            ++n;
        }
    }
    if (n == 0 || m_dyno.flying) {
        m_drain = 0.0f;
        return;
    }
    grip /= static_cast<float>(n);
    const float lean = m_wall.leanAt(m_hips.y + m_s.hang);
    const int feet = feetPlanted();
    const bool matched = n == 2 && m_hand[0].hold == m_hand[1].hold;
    if (n == 2 && !reaching && feet == 2) {
        // Planted: the arms get their breath back, slowly but surely.
        float back = m_s.feetRecover * std::clamp(grip, 0.4f, 1.0f);
        if (matched) back *= m_s.matchRecover;
        if (lean > 10.0f) back *= 0.5f;
        m_drain = -back;
    } else {
        const float hands = n == 2 ? m_s.drainTwoHands : reaching ? (m_s.drainTwoHands + m_s.drainOneHand) * 0.5f : m_s.drainOneHand;
        float drain = hands / std::max(0.35f, grip);
        drain *= 1.0f + std::max(0.0f, lean) * m_s.overhangDrain - std::min(0.35f, std::max(0.0f, -lean) * 0.015f);
        // Feet cut loose under a steep overhang take less of the weight.
        const float relief = m_s.footRelief * (lean > 10.0f ? 0.4f : 1.0f);
        if (feet == 0) drain *= m_s.handsOnly;
        else drain *= 1.0f - relief * static_cast<float>(feet) * 0.75f;
        if (matched) drain *= 0.9f;
        m_drain = drain;
    }
    m_stamina = std::min(m_s.maxStamina, m_stamina - m_drain * dt);
}

void Climber::recover(float perSecond, float dt) {
    if (climbing()) return;
    m_stamina = std::min(m_s.maxStamina, m_stamina + perSecond * dt);
}

void Climber::knock(float cost) {
    if (!climbing()) return;
    m_stamina = std::max(0.0f, m_stamina - std::max(0.0f, cost));
}

void Climber::startMantle() {
    const auto& holds = m_wall.holds();
    const ClimbHold& ha = holds[static_cast<size_t>(m_hand[0].hold)];
    const float x = (grip(0).x + grip(1).x) * 0.5f;
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
    m_dyno = Dyno{};
    for (Foot& f : m_foot) {
        f.hold = f.next = -1;
        f.t = 1e9f;
    }
}

void Climber::update(const Input& in, float dt) {
    m_broke = m_cut = m_slipped = -1;
    m_missed = m_grabbed = m_fellNow = m_lunged = false;
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

    // Hands on their way to a hold.
    for (int h = 0; h < 2; ++h) {
        Hand& hd = m_hand[h];
        if (hd.move == Move::None) continue;
        hd.t += dt;
        const float s = smooth(hd.t / hd.duration);
        // An arc out from the rock, so the hand doesn't drag along it.
        const glm::vec3 n = m_wall.surfaceNormal(hd.to.x, hd.to.y);
        hd.pos = hd.from + (hd.to - hd.from) * s + n * (0.12f * std::sin(kPi * s));
        if (hd.t >= hd.duration) land(h);
    }

    // The lunge (and a jump over the top).
    updateLunge(in, dt);
    if (m_state != State::Climbing) return;
    if (m_dyno.flying) {
        updateFeet(in, dt);
        updateStamina(dt);
        return;
    }

    // Choosing and launching.
    for (int h = 0; h < 2; ++h) {
        Hand& hd = m_hand[h];
        const Hand& other = m_hand[1 - h];
        const bool free = hd.move == Move::None && other.hold >= 0 && other.move == Move::None;
        const int picked = in.pick[h];
        const float pickDist = picked >= 0 && picked < static_cast<int>(m_wall.holds().size())
                                   ? ClimbWall::reachDistance(pointOn(h, picked, m_hips), pivot(h))
                                   : 1e9f;
        const bool pickOk = picked >= 0 && usable(picked, h) && picked != hd.hold && pickDist <= m_s.span && canSpan(h, picked);
        if (m_dyno.charging) m_aim[h] = catchTarget(h, lungeApex());
        else m_aim[h] = hd.move != Move::None ? -1 : pickOk ? picked : picked >= 0 ? -1 : findTarget(h, in.aim, m_s.span);
        if (!free || m_dyno.charging || !in.reach[h]) continue;
        // A hold picked outright that's out of reach: no move (the game
        // shows it out of reach), not some other hold.
        if (m_aim[h] >= 0) launch(h, m_aim[h], in.aim, m_s.span);
    }

    updateBody(dt);
    updateFeet(in, dt);
    const glm::vec3 right = rightOf(m_facing);
    for (int h = 0; h < 2; ++h) {
        Hand& hd = m_hand[h];
        // A free hand hangs by the body, down at its side, off the rock.
        if (hd.hold < 0 && hd.move == Move::None) {
            const glm::vec3 rest = shoulderAt(h, m_hips) - kUp * (m_s.armReach * 0.85f) + right * (sideOf(h) * 0.1f) - m_facing * 0.08f;
            hd.pos += (rest - hd.pos) * (1.0f - std::exp(-6.0f * dt));
        }
        // Two hands on one hold make room for each other.
        const Hand& other = m_hand[1 - h];
        bool beside = hd.hold >= 0 && hd.hold == other.hold;
        if (beside && m_wall.holds()[static_cast<size_t>(hd.hold)].kind == ClimbHold::Kind::Edge)
            beside = std::abs(hd.slide - other.slide) < m_s.handWidth;
        hd.share += ((beside ? 1.0f : 0.0f) - hd.share) * (1.0f - std::exp(-12.0f * dt));
        if (hd.hold < 0) hd.share = 0.0f;
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

Climber::Input ClimbBot::think(const Climber& c, float dt) { return decide(c, dt); }

bool ClimbBot::feetFirst(const Climber& c, Climber::Input& in) const {
    const auto& holds = c.wall().holds();
    for (int f = 0; f < 2; ++f)
        if (c.footMoving(f)) return true; // let it get there
    for (int f = 0; f < 2; ++f) {
        const int t = c.footTarget(f), cur = c.footHold(f);
        if (t < 0 || t == cur) continue;
        if (cur >= 0 && holds[static_cast<size_t>(t)].position.y < holds[static_cast<size_t>(cur)].position.y + 0.25f) continue;
        in.step[f] = true;
        in.footPick[f] = t;
        return true;
    }
    return false;
}

Climber::Input ClimbBot::decide(const Climber& c, float dt) {
    Climber::Input in;
    in.aim = glm::vec2(0.0f, 1.0f);
    if (c.state() != Climber::State::Climbing) return in;
    const auto& holds = c.wall().holds();
    // In the air: the trigger as soon as the planned hold is in the hand's
    // reach; past the top with it not coming, whatever is.
    if (c.flying()) {
        in.aim = m_lungeAim;
        if (m_lungeHand >= 0 && c.aimTarget(m_lungeHand) == m_lungePick) {
            in.reach[m_lungeHand] = true;
            return in;
        }
        if (c.hips().y < c.lungeApex().y - 0.1f)
            for (int h = 0; h < 2; ++h)
                if (c.aimTarget(h) >= 0) {
                    in.reach[h] = true;
                    break;
                }
        return in;
    }
    // A lunge being charged: hold jump until the charge carries that far,
    // then let go.
    if (c.charging()) {
        in.aim = m_lungeAim;
        in.jump = c.charge() < m_lungeNeed;
        return in;
    }
    m_lungeHand = m_lungePick = -1;
    if (c.handMoving(0) || c.handMoving(1)) return in;
    const int h0 = c.handHold(0), h1 = c.handHold(1);
    // Both hands on an edge: mantle when it's the top, or to rest.
    if (h0 >= 0 && h1 >= 0) {
        const ClimbHold& a = holds[static_cast<size_t>(h0)];
        const ClimbHold& b = holds[static_cast<size_t>(h1)];
        // (A ledge sticks out from the rock: there's no climbing on past
        // it without getting onto it.)
        if (a.kind == ClimbHold::Kind::Edge && b.kind == ClimbHold::Kind::Edge && a.ledge == b.ledge) {
            in.jump = true;
            return in;
        }
    }
    // A breath between moves, longer when tired; none with a hand hanging
    // free (a catch with one hand): grab again at once.
    m_wait -= dt;
    if (m_wait > 0.0f && h0 >= 0 && h1 >= 0) return in;
    // Feet first: onto footholds, and up as the body goes up.
    if (h0 >= 0 && h1 >= 0 && feetFirst(c, in)) {
        m_wait = pause * 0.4f;
        return in;
    }
    // Under an overhang there's no rest in waiting: keep moving.
    const bool steep = c.wall().leanAt(c.hips().y + 1.0f) > 5.0f;
    m_wait = steep ? pause * 0.5f : pause + (1.0f - c.staminaFraction()) * pause;

    // The hand that hangs on: the one further along the route (or higher).
    auto progress = [&](int hold) -> float {
        if (hold < 0) return -1e9f;
        const ClimbHold& h = holds[static_cast<size_t>(hold)];
        // A hand on the summit's edge, or a ledge's, stays there: the other
        // one joins it.
        if (h.kind == ClimbHold::Kind::Edge) return 1e6f + h.position.y;
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
    // The line's next hold: each hand works its own side, so the hand on
    // that side goes (both hands on one hold: whichever that is).
    const int nextOnLine = at + 1 < static_cast<int>(m_route.size()) ? m_route[static_cast<size_t>(at + 1)] : -1;
    if (h0 >= 0 && h0 == h1 && nextOnLine >= 0) {
        const float dx = holds[static_cast<size_t>(nextOnLine)].position.x - from.x;
        move = dx < 0.0f ? Climber::kLeft : Climber::kRight;
        stay = 1 - move;
    }
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
    // Before a long steep stretch (nowhere to rest on it) it rests to full.
    float ahead = 0.0f;
    for (float up = 1.0f; up <= 7.0f; up += 1.0f) ahead = std::max(ahead, c.wall().leanAt(c.hips().y + up));
    const float restTo = ahead > 5.0f ? std::max(restUntil, 0.99f) : restUntil;
    if (h0 >= 0 && h1 >= 0 && c.drainRate() < 0.0f && c.staminaFraction() < restTo) return in;
    // Tired: make for a ledge's edge in reach, to stand on it and rest,
    // or else a jug to shake out on (not under an overhang: no rest there).
    const ClimbHold& anchorHold = holds[static_cast<size_t>(anchor)];
    const bool atLedge = anchorHold.kind == ClimbHold::Kind::Edge && anchorHold.ledge >= 0;
    // One hand already on a ledge's lip: the other joins it there (both
    // hands on it, the climber pulls up onto the ledge).
    for (int k = 0; k < 2 && pick < 0; ++k) {
        const int lip = c.handHold(k);
        if (lip < 0 || lip == c.handHold(1 - k)) continue;
        const ClimbHold& e = holds[static_cast<size_t>(lip)];
        if (e.kind != ClimbHold::Kind::Edge || e.ledge < 0 || !c.canSpan(1 - k, lip)) continue;
        pick = lip;
        move = 1 - k;
        from = e.position;
    }
    if (pick < 0 && (c.staminaFraction() < restBelow || (atLedge && c.staminaFraction() < ledgeRestBelow))) {
        float best = 1e9f;
        for (size_t i = 0; pick < 0 && i < holds.size(); ++i) {
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
    // Not past a ledge's edge: up onto the ledge first.
    int last = static_cast<int>(m_route.size()) - 1;
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
    // The next hold is over on the anchor's side, past it: this hand
    // matches the anchor's hold, and the other one goes on from there.
    // The same on an edge with nothing else of it on this hand's side:
    // both hands on one lip is enough to pull up (or rest) on.
    if (pick < 0 && c.handHold(move) != anchor && c.canSpan(move, anchor) &&
        ((nextOnLine >= 0 && c.crossesOver(move, nextOnLine)) || anchorHold.kind == ClimbHold::Kind::Edge))
        pick = anchor;
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
    // Fresh, feet on and on the line: lunge past the next holds when one
    // further along is a sure catch from the top of a lunge.
    const int pickAt = routeIndex(pick);
    if (lunges && pickAt >= 0 && c.staminaFraction() > 0.85f && c.feetPlanted() == 2 && h0 >= 0 && h1 >= 0) {
        const Climber::Settings& s = c.settings();
        const glm::vec3 hips = c.hips();
        for (int i = last; i > pickAt + 1; --i) { // not past a ledge where a rest is due
            const int hold = m_route[static_cast<size_t>(i)];
            const ClimbHold& h = holds[static_cast<size_t>(hold)];
            if (h.kind == ClimbHold::Kind::Edge || h.loose || hold == h0 || hold == h1 || c.holdGone(hold) || h.position.y < from.y + 0.3f) continue;
            if (ClimbWall::reachDistance(h.position, from) > s.span + s.dynoMax * 0.8f) continue;
            // Where the hips need to be to catch it, and which way that is.
            const glm::vec2 to(h.position.x - hips.x, h.position.y - (hips.y + s.shoulderUp + s.armReach * 0.7f));
            if (glm::length(to) < 0.2f) continue;
            const glm::vec2 aim = glm::normalize(to);
            const int hand = h.position.x < hips.x ? Climber::kLeft : Climber::kRight;
            for (float charge = 0.2f; charge <= 1.0f; charge += 0.1f) {
                const glm::vec3 apex = c.lungeApexFor(aim, charge);
                if (c.catchTarget(hand, apex) != hold || c.catchTarget(hand, apex - glm::vec3(0.0f, 0.15f, 0.0f)) != hold) continue;
                m_lungeHand = hand;
                m_lungePick = hold;
                m_lungeNeed = std::min(1.0f, charge + 0.03f);
                m_lungeAim = aim;
                in.aim = aim;
                in.jump = true;
                return in;
            }
        }
    }
    const glm::vec3& target = holds[static_cast<size_t>(pick)].position;
    const glm::vec3 cur = c.hand(move);
    const glm::vec2 d(target.x - cur.x, target.y - cur.y);
    in.aim = glm::length(d) > 1e-3f ? glm::normalize(d) : glm::vec2(0.0f, 1.0f);
    in.pick[move] = pick;
    in.reach[move] = true;
    m_came[move] = c.handHold(move); // not straight back there when finding a way round
    return in;
}

} // namespace kke
