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
    m_bodies.push_back(std::move(b));
    return m_bodies.size() - 1;
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
    // Bodies push each other apart (bounding spheres, inelastic): enough to
    // stop crates passing through the boat; not a rigid-body contact solver.
    for (size_t i = 0; i < m_bodies.size(); ++i) {
        for (size_t j = i + 1; j < m_bodies.size(); ++j) {
            FloatingBody &a = m_bodies[i], &c = m_bodies[j];
            if (!a.alive || !c.alive) continue;
            glm::vec3 d = c.position - a.position;
            float ra = glm::length(a.halfExtents) * 0.8f, rc = glm::length(c.halfExtents) * 0.8f;
            float dist = glm::length(d);
            if (dist >= ra + rc || dist < 1e-6f) continue;
            glm::vec3 n = d / dist;
            float overlap = ra + rc - dist;
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

} // namespace kke
