#include "kke/ai/Steering.h"

#include <algorithm>
#include <cmath>

namespace kke::ai {

namespace {

constexpr float kEpsilon = 1e-5f;

float lengthSq(const glm::vec3& v) { return glm::dot(v, v); }

glm::vec3 normalizeOr(const glm::vec3& v, const glm::vec3& fallback = glm::vec3(0.0f)) {
    const float l2 = lengthSq(v);
    return l2 > kEpsilon * kEpsilon ? v / std::sqrt(l2) : fallback;
}

// Reynolds: steering = desired velocity - current velocity.
glm::vec3 steerTowards(const Mover& m, const glm::vec3& desiredVelocity) {
    return flat(desiredVelocity - m.velocity);
}

} // namespace

glm::vec3 truncate(const glm::vec3& v, float max) {
    const float l2 = lengthSq(v);
    if (l2 <= max * max || l2 <= 0.0f) return v;
    return v * (max / std::sqrt(l2));
}

glm::vec3 seek(const Mover& m, const glm::vec3& target) {
    return steerTowards(m, normalizeOr(flat(target - m.position)) * m.maxSpeed);
}

glm::vec3 flee(const Mover& m, const glm::vec3& threat, float panicDistance) {
    const glm::vec3 away = flat(m.position - threat);
    if (panicDistance > 0.0f && lengthSq(away) > panicDistance * panicDistance) return glm::vec3(0.0f);
    // Standing exactly on the threat: any direction beats none.
    return steerTowards(m, normalizeOr(away, glm::vec3(1.0f, 0.0f, 0.0f)) * m.maxSpeed);
}

glm::vec3 arrive(const Mover& m, const glm::vec3& target, float slowRadius) {
    const glm::vec3 to = flat(target - m.position);
    const float dist = std::sqrt(lengthSq(to));
    if (dist < 0.05f) return steerTowards(m, glm::vec3(0.0f));
    const float speed = slowRadius > 0.0f ? m.maxSpeed * std::min(1.0f, dist / slowRadius) : m.maxSpeed;
    return steerTowards(m, to / dist * speed);
}

namespace {
// Where a target will be by the time the two meet: distance over the
// two speeds (Reynolds' pursuit), so the guess never lands past the mover.
glm::vec3 predicted(const Mover& m, const glm::vec3& pos, const glm::vec3& vel, float maxPrediction) {
    const float dist = std::sqrt(lengthSq(flat(pos - m.position)));
    const float closing = m.maxSpeed + std::sqrt(lengthSq(flat(vel)));
    const float t = std::min(maxPrediction, dist / std::max(closing, kEpsilon));
    return pos + flat(vel) * t;
}
} // namespace

glm::vec3 pursue(const Mover& m, const glm::vec3& targetPos, const glm::vec3& targetVel, float maxPrediction) {
    return seek(m, predicted(m, targetPos, targetVel, maxPrediction));
}

glm::vec3 evade(const Mover& m, const glm::vec3& threatPos, const glm::vec3& threatVel, float panicDistance, float maxPrediction) {
    if (panicDistance > 0.0f && lengthSq(flat(m.position - threatPos)) > panicDistance * panicDistance) return glm::vec3(0.0f);
    return flee(m, predicted(m, threatPos, threatVel, maxPrediction));
}

glm::vec3 wander(const Mover& m, float& angle, float dt, float random01, const WanderParams& p) {
    angle += (random01 * 2.0f - 1.0f) * p.jitter * dt;
    angle = std::remainder(angle, 6.2831853f);
    const glm::vec3 heading = normalizeOr(flat(m.velocity), glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 centre = m.position + heading * p.distance;
    // The circle's angle is relative to the heading, so wandering never
    // flips around on its own.
    const float base = std::atan2(heading.x, heading.z);
    const glm::vec3 onCircle{ std::sin(base + angle) * p.radius, 0.0f, std::cos(base + angle) * p.radius };
    return seek(m, centre + onCircle) * 0.5f;
}

glm::vec3 separation(const Mover& m, std::span<const Neighbour> near, float distance) {
    glm::vec3 push(0.0f);
    for (const Neighbour& n : near) {
        const glm::vec3 away = flat(m.position - n.position);
        const float d = std::sqrt(lengthSq(away));
        const float reach = distance + m.radius + n.radius;
        if (d >= reach) continue;
        // Harder the closer (1 at contact distance, 0 at reach); straight
        // on top of each other, push along x.
        const float strength = 1.0f - d / reach;
        push += normalizeOr(away, glm::vec3(1.0f, 0.0f, 0.0f)) * strength;
    }
    if (lengthSq(push) <= 0.0f) return push;
    return steerTowards(m, normalizeOr(push) * m.maxSpeed);
}

glm::vec3 alignment(const Mover& m, std::span<const Neighbour> near) {
    if (near.empty()) return glm::vec3(0.0f);
    glm::vec3 avg(0.0f);
    for (const Neighbour& n : near) avg += flat(n.velocity);
    avg /= float(near.size());
    if (lengthSq(avg) < 0.01f) return glm::vec3(0.0f);
    return steerTowards(m, normalizeOr(avg) * std::min(m.maxSpeed, std::sqrt(lengthSq(avg))));
}

glm::vec3 cohesion(const Mover& m, std::span<const Neighbour> near) {
    if (near.empty()) return glm::vec3(0.0f);
    glm::vec3 centre(0.0f);
    for (const Neighbour& n : near) centre += n.position;
    centre /= float(near.size());
    return arrive(m, centre, 3.0f);
}

glm::vec3 flock(const Mover& m, std::span<const Neighbour> near, const FlockWeights& w) {
    return separation(m, near, w.separationDistance) * w.separation + alignment(m, near) * w.alignment +
           cohesion(m, near) * w.cohesion;
}

glm::vec3 avoidObstacles(const Mover& m, std::span<const CircleObstacle> obstacles, float lookAhead) {
    const glm::vec3 vel = flat(m.velocity);
    const float speed = std::sqrt(lengthSq(vel));
    if (speed < 0.05f) return glm::vec3(0.0f);
    const glm::vec3 fwd = vel / speed;
    const glm::vec3 side{ fwd.z, 0.0f, -fwd.x };
    const float boxLength = m.radius + speed * lookAhead;
    const CircleObstacle* nearest = nullptr;
    float nearestAlong = boxLength;
    float nearestSide = 0.0f;
    for (const CircleObstacle& o : obstacles) {
        const glm::vec3 rel = flat(o.centre - m.position);
        const float along = glm::dot(rel, fwd);
        const float across = glm::dot(rel, side);
        const float reach = o.radius + m.radius;
        if (along < -reach || along > boxLength + reach || std::abs(across) >= reach) continue;
        if (along < nearestAlong) {
            nearestAlong = along;
            nearestSide = across;
            nearest = &o;
        }
    }
    if (!nearest) return glm::vec3(0.0f);
    // Sideways away from it (harder the closer), and brake a little.
    const float reach = nearest->radius + m.radius;
    const float closeness = 1.0f + (boxLength - std::max(nearestAlong, 0.0f)) / boxLength;
    const float sideSign = nearestSide >= 0.0f ? -1.0f : 1.0f;
    const float sideForce = (reach - std::abs(nearestSide)) / reach;
    return (side * sideSign * (0.5f + sideForce) * closeness - fwd * 0.3f * closeness) * m.maxAccel;
}

glm::vec3 blend(const Mover& m, std::span<const Weighted> forces) {
    glm::vec3 sum(0.0f);
    for (const Weighted& f : forces) sum += f.force * f.weight;
    return truncate(flat(sum), m.maxAccel);
}

glm::vec3 integrate(Mover& m, const glm::vec3& steering, float dt) {
    const glm::vec3 accel = truncate(flat(steering), m.maxAccel);
    m.velocity = truncate(flat(m.velocity + accel * dt), m.maxSpeed);
    m.position += m.velocity * dt;
    return m.velocity;
}

int SpatialGrid::cellOf(float v) const { return int(std::floor(v / m_cell)); }

void SpatialGrid::insert(uint32_t id, const glm::vec3& position) {
    m_cells[key(cellOf(position.x), cellOf(position.z))].push_back(id);
}

} // namespace kke::ai
