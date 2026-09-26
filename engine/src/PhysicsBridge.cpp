#include "kke/PhysicsBridge.h"

#include <algorithm>
#include <cmath>

namespace kke {

void boxBounds(const BridgeBox& box, glm::vec3& min, glm::vec3& max) {
    const glm::mat3 r = glm::mat3_cast(box.rotation);
    glm::vec3 e(0.0f);
    for (int axis = 0; axis < 3; ++axis) e += glm::abs(r[axis]) * box.halfExtents[axis];
    min = box.center - e;
    max = box.center + e;
}

bool boundsOverlap(const glm::vec3& aMin, const glm::vec3& aMax, const glm::vec3& bMin, const glm::vec3& bMax) {
    return aMin.x <= bMax.x && bMin.x <= aMax.x && aMin.y <= bMax.y && bMin.y <= aMax.y && aMin.z <= bMax.z && bMin.z <= aMax.z;
}

void boxSurface(const BridgeBox& box, const glm::vec3& point, glm::vec3& normal, float& distance) {
    const glm::quat inv = glm::inverse(box.rotation);
    const glm::vec3 p = inv * (point - box.center);
    const glm::vec3 h = box.halfExtents;
    const glm::vec3 d = glm::abs(p) - h; // per axis: > 0 outside that slab
    glm::vec3 n(0.0f);
    if (d.x > 0.0f || d.y > 0.0f || d.z > 0.0f) {
        // Outside: toward the nearest point on the box.
        const glm::vec3 closest = glm::clamp(p, -h, h);
        const glm::vec3 away = p - closest;
        distance = glm::length(away);
        n = away / std::max(distance, 1e-9f);
    } else {
        // Inside: out through the nearest face.
        int axis = 0;
        if (d.y > d[axis]) axis = 1;
        if (d.z > d[axis]) axis = 2;
        distance = d[axis];
        n[axis] = p[axis] >= 0.0f ? 1.0f : -1.0f;
    }
    normal = box.rotation * n;
}

glm::vec3 boxContactImpulse(const BridgeBox& box, const std::vector<BridgeVertex>& verts, const glm::vec3& gravity, float dt, float skin,
                            glm::vec3* point) {
    glm::vec3 total(0.0f), at(0.0f);
    float weight = 0.0f;
    const glm::vec3 fall = gravity * dt;
    for (const BridgeVertex& v : verts) {
        if (v.mass <= 0.0f) continue;
        glm::vec3 n;
        float dist;
        boxSurface(box, v.position, n, dist);
        if (dist > skin) continue;
        // What the contact did to the vertex (gravity alone would have
        // changed its velocity by g dt), along the push-out direction.
        const float pushed = glm::dot(v.deltaVelocity - fall, n);
        if (pushed <= 0.0f) continue;
        const float j = v.mass * pushed;
        total -= n * j;
        at += v.position * j;
        weight += j;
    }
    if (point) *point = weight > 0.0f ? at / weight : box.center;
    return total;
}

} // namespace kke
