#include "kke/FloatingBodies.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace kke {

glm::mat4 FloatingBody::transform() const {
    return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(orientation);
}

size_t FloatingBodies::add(const glm::vec3& he, float density, const glm::vec3& position, const glm::quat& orientation) {
    FloatingBody b;
    b.halfExtents = he;
    b.density = density;
    b.position = position;
    b.orientation = glm::normalize(orientation);
    const glm::vec3 size = he * 2.0f;
    const float volume = size.x * size.y * size.z;
    b.mass = density * volume;
    b.inertiaBody = glm::vec3(size.y * size.y + size.z * size.z, size.x * size.x + size.z * size.z, size.x * size.x + size.y * size.y) *
                    (b.mass / 12.0f);
    // Sample grid: ~0.35 m spacing, 2..5 points per axis (8..125 points).
    glm::ivec3 n = glm::clamp(glm::ivec3(glm::ceil(size / 0.35f)), glm::ivec3(2), glm::ivec3(5));
    for (int z = 0; z < n.z; ++z)
        for (int y = 0; y < n.y; ++y)
            for (int x = 0; x < n.x; ++x)
                b.samplePoints.push_back(-he + (glm::vec3(x, y, z) + 0.5f) * size / glm::vec3(n));
    b.pointVolume = volume / static_cast<float>(b.samplePoints.size());
    for (size_t i = 0; i < m_bodies.size(); ++i)
        if (!m_bodies[i].alive) {
            m_bodies[i] = std::move(b);
            return i;
        }
    m_bodies.push_back(std::move(b));
    return m_bodies.size() - 1;
}

size_t FloatingBodies::aliveCount() const {
    size_t n = 0;
    for (const FloatingBody& b : m_bodies) n += b.alive ? 1u : 0u;
    return n;
}

void FloatingBodies::applyForce(size_t index, const glm::vec3& force, const glm::vec3& worldPoint) {
    if (index >= m_bodies.size()) return;
    FloatingBody& b = m_bodies[index];
    b.force += force;
    b.torque += glm::cross(worldPoint - b.position, force);
}

void FloatingBodies::step(float dt, const OceanWaves& ocean, float time) {
    if (dt <= 0.0f) return;
    for (FloatingBody& b : m_bodies) {
        if (!b.alive) continue;
        const glm::mat3 R = glm::mat3_cast(b.orientation);
        glm::vec3 force = b.force + gravity * b.mass;
        glm::vec3 torque = b.torque + glm::cross(R * b.centerOfMassOffset, gravity * b.mass); // weight acts at the (possibly low) CoM
        b.force = b.torque = glm::vec3(0.0f);

        // Each sample point is a little cube of height pointHeight; the part
        // of it below the surface displaces water (smooth, so bodies don't
        // pop as points cross the surface).
        // The side of the little cube each point stands for. (This used the
        // cube root of the *point count*, which is wrong for non-cubic
        // grids: the boat's 5x2x4 grid got 0.2 m instead of 0.35 m, which
        // skewed the buoyancy and tipped it over.)
        const float pointHeight = std::cbrt(b.pointVolume);
        float submergedSum = 0.0f;
        const float perPointMass = b.mass / static_cast<float>(b.samplePoints.size());
        for (const glm::vec3& local : b.samplePoints) {
            const glm::vec3 r = R * local;
            const glm::vec3 p = b.position + r;
            const glm::vec2 xz(p.x, p.z);
            const float depth = ocean.height(xz, time) - p.y;
            const float frac = std::clamp(depth / pointHeight + 0.5f, 0.0f, 1.0f);
            if (frac <= 0.0f) continue;
            submergedSum += frac;
            glm::vec3 f(0.0f, -gravity.y * waterDensity * b.pointVolume * frac, 0.0f); // Archimedes
            // Drag against the moving water: damps bobbing and lets waves
            // carry things along. (Water velocity from the undisplaced
            // point — cheap and close enough.)
            const glm::vec3 pointVel = b.velocity + glm::cross(b.angularVelocity, r);
            const glm::vec3 rel = pointVel - ocean.velocity(xz, time);
            f -= rel * (b.linearDrag * perPointMass * frac);
            f.y -= rel.y * (b.heaveDrag * perPointMass * frac);
            force += f;
            torque += glm::cross(r, f);
        }
        const float submerged = submergedSum / static_cast<float>(b.samplePoints.size());
        b.impactSpeed = (b.submerged < 0.05f && submerged >= 0.05f) ? std::max(0.0f, -b.velocity.y) : 0.0f;
        b.submerged = submerged;

        // Integrate (semi-implicit Euler). World inertia = R I R^T.
        const glm::mat3 Iworld = R * glm::mat3(glm::vec3(b.inertiaBody.x, 0, 0), glm::vec3(0, b.inertiaBody.y, 0), glm::vec3(0, 0, b.inertiaBody.z)) *
                                 glm::transpose(R);
        torque -= Iworld * b.angularVelocity * (b.angularDrag * submerged);
        b.velocity += force / b.mass * dt;
        b.angularVelocity += glm::inverse(Iworld) * torque * dt;
        b.position += b.velocity * dt;
        glm::quat spin(0.0f, b.angularVelocity.x, b.angularVelocity.y, b.angularVelocity.z);
        b.orientation = glm::normalize(b.orientation + spin * b.orientation * (0.5f * dt));

        // Sea floor: lift the lowest corner out and kill downward motion.
        float lowest = 1e9f;
        const glm::mat3 R2 = glm::mat3_cast(b.orientation);
        for (int c = 0; c < 8; ++c) {
            glm::vec3 corner((c & 1) ? b.halfExtents.x : -b.halfExtents.x, (c & 2) ? b.halfExtents.y : -b.halfExtents.y,
                             (c & 4) ? b.halfExtents.z : -b.halfExtents.z);
            lowest = std::min(lowest, (b.position + R2 * corner).y);
        }
        if (lowest < seaFloorY) {
            b.position.y += seaFloorY - lowest;
            if (b.velocity.y < 0.0f) b.velocity.y = 0.0f;
            b.velocity.x *= 0.9f;
            b.velocity.z *= 0.9f;
            b.angularVelocity *= 0.9f;
        }
    }
    // Bodies push each other apart (inelastic): enough to stop crates
    // passing through a boat and two ships sailing through each other; not
    // a rigid-body contact solver. Each body is a capsule along its longest
    // axis (a cube is close to a sphere, a 30 m hull is a long thin pill),
    // so ships can sail side by side without bouncing off at a hull's
    // length apart.
    for (size_t i = 0; i < m_bodies.size(); ++i) {
        for (size_t j = i + 1; j < m_bodies.size(); ++j) {
            FloatingBody &a = m_bodies[i], &c = m_bodies[j];
            if (!a.alive || !c.alive || (!a.pushesSmallBits && !c.pushesSmallBits)) continue;
            const Capsule ca = capsuleOf(a), cc = capsuleOf(c);
            const float reach = ca.radius + cc.radius;
            if (glm::length(c.position - a.position) >= reach + ca.halfLength + cc.halfLength) continue; // far apart
            glm::vec3 pa, pc;
            closestPoints(ca.p0, ca.p1, cc.p0, cc.p1, pa, pc);
            glm::vec3 d = pc - pa;
            float dist = glm::length(d);
            if (dist >= reach) continue;
            glm::vec3 n = dist > 1e-6f ? d / dist : glm::normalize(c.position - a.position + glm::vec3(0.0f, 0.0f, 1e-4f));
            if (dist <= 1e-6f) dist = 0.0f;
            n.y *= 0.25f; // floating things mostly meet side on: keep the push horizontal
            n = glm::normalize(n);
            float overlap = reach - dist;
            float wa = c.mass / (a.mass + c.mass), wc = a.mass / (a.mass + c.mass);
            a.position -= n * overlap * wa;
            c.position += n * overlap * wc;
            float vn = glm::dot(c.velocity - a.velocity, n);
            if (vn < 0.0f) {
                glm::vec3 impulse = n * vn;
                a.velocity += impulse * wa;
                c.velocity -= impulse * wc;
            }
        }
    }
}

FloatingBodies::Capsule FloatingBodies::capsuleOf(const FloatingBody& b) {
    const glm::vec3& h = b.halfExtents;
    int axis = 0;
    if (h.y > h[axis]) axis = 1;
    if (h.z > h[axis]) axis = 2;
    // The radius: the larger of the two other half extents (the hull's beam).
    const float radius = std::max(h[(axis + 1) % 3], h[(axis + 2) % 3]);
    Capsule c;
    c.radius = radius;
    c.halfLength = std::max(0.0f, h[axis] - radius);
    glm::vec3 local(0.0f);
    local[axis] = c.halfLength;
    const glm::vec3 along = glm::mat3_cast(b.orientation) * local;
    c.p0 = b.position - along;
    c.p1 = b.position + along;
    return c;
}

// Closest points between segments p1-q1 and p2-q2 (Ericson, Real-Time
// Collision Detection 5.1.9).
void FloatingBodies::closestPoints(const glm::vec3& p1, const glm::vec3& q1, const glm::vec3& p2, const glm::vec3& q2, glm::vec3& c1, glm::vec3& c2) {
    const glm::vec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
    const float a = glm::dot(d1, d1), e = glm::dot(d2, d2), f = glm::dot(d2, r);
    float s = 0.0f, t = 0.0f;
    constexpr float kEps = 1e-8f;
    if (a <= kEps && e <= kEps) {
        c1 = p1;
        c2 = p2;
        return;
    }
    if (a <= kEps) {
        t = std::clamp(f / e, 0.0f, 1.0f);
    } else {
        const float c = glm::dot(d1, r);
        if (e <= kEps) {
            s = std::clamp(-c / a, 0.0f, 1.0f);
        } else {
            const float b = glm::dot(d1, d2), denom = a * e - b * b;
            s = denom > kEps ? std::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f) {
                t = 0.0f;
                s = std::clamp(-c / a, 0.0f, 1.0f);
            } else if (t > 1.0f) {
                t = 1.0f;
                s = std::clamp((b - c) / a, 0.0f, 1.0f);
            }
        }
    }
    c1 = p1 + d1 * s;
    c2 = p2 + d2 * t;
}

} // namespace kke
