#include "Shot.h"

#include "Court.h"

#include <algorithm>
#include <cmath>

namespace tennis {

namespace {
// (1 - e^-kt) / k, and t as k -> 0.
float fade(float k, float t) { return k > 1e-5f ? (1.0f - std::exp(-k * t)) / k : t; }
} // namespace

// Solved: v' = -g y - k v, so v(t) = (v0 + g/k) e^-kt - g/k (vertical g).
glm::vec3 Flight::at(float t) const {
    const float f = fade(drag, t);
    glm::vec3 p = pos + vel * f;
    // The fall: g/k (t - f); for no drag, g t^2 / 2.
    p.y -= drag > 1e-5f ? gravity / drag * (t - f) : 0.5f * gravity * t * t;
    return p;
}

glm::vec3 Flight::velocityAt(float t) const {
    const float e = std::exp(-drag * t);
    glm::vec3 v = vel * e;
    v.y -= gravity * fade(drag, t);
    return v;
}

float Flight::timeDownTo(float height) const {
    // The height rises to one top, then falls for good: find the top, then
    // the crossing after it by bisection.
    if (gravity <= 0.0f) return -1.0f;
    float top = 0.0f;
    if (vel.y > 0.0f) top = drag > 1e-5f ? std::log(1.0f + drag * vel.y / gravity) / drag : vel.y / gravity;
    if (at(top).y < height) return -1.0f; // never gets up to it
    float lo = top, hi = top + 0.5f;
    for (int i = 0; i < 40 && at(hi).y > height; ++i) hi += (hi - lo) * 2.0f;
    if (at(hi).y > height) return -1.0f;
    for (int i = 0; i < 40; ++i) {
        const float mid = 0.5f * (lo + hi);
        (at(mid).y > height ? lo : hi) = mid;
    }
    return 0.5f * (lo + hi);
}

float Flight::timeAtZ(float z) const {
    const float dz = z - pos.z;
    if (std::abs(vel.z) < 1e-5f || dz * vel.z < 0.0f) return dz == 0.0f ? 0.0f : -1.0f;
    // dz = vz (1 - e^-kt) / k
    if (drag <= 1e-5f) return dz / vel.z;
    const float q = 1.0f - drag * dz / vel.z;
    if (q <= 0.0f) return -1.0f; // the air stops it first
    return -std::log(q) / drag;
}

Flight bounce(const Flight& f, float t, const BounceModel& m, float gravityAfter) {
    Flight out;
    out.pos = f.at(t);
    out.pos.y = kBallRadius;
    const glm::vec3 v = f.velocityAt(t);
    out.vel = glm::vec3(v.x * m.keepAlong, std::abs(v.y) * m.restitution, v.z * m.keepAlong);
    out.gravity = gravityAfter;
    return out;
}

float spinPull(ShotKind kind) {
    switch (kind) {
    case ShotKind::Topspin: return 6.0f;
    case ShotKind::Slice: return -2.5f;
    case ShotKind::Lob: return 1.0f;
    case ShotKind::Drop: return -1.5f;
    case ShotKind::Serve: return 2.0f;
    case ShotKind::Flat: return 0.0f;
    }
    return 0.0f;
}

ShotPlan planShot(const glm::vec3& from, const glm::vec3& target, float speed, ShotKind kind, float netMargin) {
    ShotPlan plan;
    plan.gravity = kGravity + spinPull(kind);
    const glm::vec3 land(target.x, kBallRadius, target.z);
    const glm::vec2 flat(land.x - from.x, land.z - from.z);
    const float distance = std::max(0.5f, glm::length(flat));
    // `speed` is the speed along the court off the racket; the air takes
    // some on the way, so the flight lasts a little longer than without.
    const float along = std::max(speed, kDrag * distance / 0.85f);
    float t = -std::log(1.0f - kDrag * distance / along) / kDrag;
    auto launch = [&](float time) {
        // Exact for Flight::at: horizontal d = v f, vertical
        // dy = vy f - g/k (t - f).
        const float f = fade(kDrag, time);
        glm::vec3 v = (land - from) / f;
        v.y = (land.y - from.y + plan.gravity / kDrag * (time - f)) / f;
        return v;
    };
    // Raise the arc (a longer flight) until it clears the net.
    for (int tries = 0; tries < 60; ++tries) {
        const glm::vec3 v = launch(t);
        Flight f{ from, v, plan.gravity };
        const float tn = f.timeAtNet();
        bool clear = true;
        if (tn >= 0.0f && tn < t) {
            const glm::vec3 at = f.at(tn);
            clear = at.y - kBallRadius >= netHeight(at.x) + netMargin;
        }
        plan.velocity = v;
        plan.time = t;
        plan.clearsNet = clear;
        if (clear) break;
        t *= 1.06f;
    }
    // ... but not up into the roof: a slow lob from far behind the
    // baseline would need a moon ball (the air takes its speed), so it
    // goes faster and lower instead.
    const float highest = kLidHeight - 2.5f;
    for (int tries = 0; tries < 60; ++tries) {
        const Flight f{ from, plan.velocity, plan.gravity };
        const float top = f.vel.y > 0.0f ? std::log(1.0f + kDrag * f.vel.y / plan.gravity) / kDrag : 0.0f;
        if (f.at(top).y <= highest) break;
        t *= 0.95f;
        plan.velocity = launch(t);
        plan.time = t;
    }
    return plan;
}

bool afterBounceAt(const Flight& f, float height, const BounceModel& m, glm::vec3& where, float& when, float horizon) {
    const float tLand = f.timeDownTo(kBallRadius);
    if (tLand < 0.0f || tLand > horizon) return false;
    const Flight up = bounce(f, tLand, m, kGravity + (f.gravity - kGravity) * kPullAfterBounce);
    // Take it on the way down after the bounce's top (or at the top, if it
    // never gets that high).
    float t = up.timeDownTo(height);
    if (t < 0.0f) t = up.vel.y / up.gravity; // the top of the arc
    if (tLand + t > horizon) return false;
    where = up.at(t);
    when = tLand + t;
    return true;
}

bool meetPoint(const Flight& f, int side, const BounceModel& m, glm::vec3& where, float& when, float horizon) {
    const float tLand = f.timeDownTo(kBallRadius);
    if (tLand < 0.0f || tLand > horizon) return false;
    const Flight up = bounce(f, tLand, m, kGravity + (f.gravity - kGravity) * kPullAfterBounce);
    if (up.pos.z * static_cast<float>(side) < 0.0f) return false; // it bounces on the other side
    meetOnArc(up, where, when, horizon - tLand);
    when += tLand;
    return true;
}

void meetOnArc(const Flight& up, glm::vec3& where, float& when, float horizon) {
    // As deep as anyone goes: a few steps behind the baseline.
    const float deepest = kHalfLength + 3.0f;
    constexpr float kStep = 0.02f;
    glm::vec3 last = up.pos;
    float lastT = 0.0f;
    for (float t = 0.0f; t < horizon; t += kStep) {
        const glm::vec3 p = up.at(t);
        if (p.y < kBallRadius) break;
        if (std::abs(p.z) > deepest || std::abs(p.x) > kFenceHalfX - 1.0f) break; // take it where it gets out of reach
        last = p;
        lastT = t;
        const bool falling = up.velocityAt(t).y < 0.0f;
        if (falling && p.y <= 1.2f) break; // waist high on the way down
    }
    where = last;
    when = lastT;
}

} // namespace tennis
