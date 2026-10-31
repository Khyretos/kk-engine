#include "kke/ai/Crowd.h"

#include "kke/ai/NavMesh.h"

#include <DetourCrowd.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

#include <algorithm>
#include <cmath>

namespace kke::ai {

struct Crowd::Impl {
    dtCrowd* crowd = nullptr;
    int capacity = 0;
    std::vector<glm::vec3> targets; // per agent slot: where it was sent (arrived())
    std::vector<bool> hasTarget;
    ~Impl() { dtFreeCrowd(crowd); }

    int index(AgentId id) const {
        const int i = static_cast<int>(id) - 1;
        if (!crowd || i < 0 || i >= capacity) return -1;
        const dtCrowdAgent* a = crowd->getAgent(i);
        return a && a->active ? i : -1;
    }
    bool snap(const glm::vec3& p, dtPolyRef& ref, glm::vec3& out) const {
        const dtNavMeshQuery* q = crowd->getNavMeshQuery();
        const float ext[3] = { 2.0f, 4.0f, 2.0f };
        float nearest[3];
        if (dtStatusFailed(q->findNearestPoly(&p.x, ext, crowd->getFilter(0), &ref, nearest)) || !ref) return false;
        out = glm::vec3(nearest[0], nearest[1], nearest[2]);
        return true;
    }
};

namespace {

dtCrowdAgentParams toDetour(const CrowdAgentParams& p) {
    dtCrowdAgentParams d{};
    d.radius = std::max(0.05f, p.radius);
    d.height = std::max(0.1f, p.height);
    d.maxAcceleration = std::max(0.0f, p.maxAcceleration);
    d.maxSpeed = std::max(0.0f, p.maxSpeed);
    d.collisionQueryRange = d.radius * 12.0f;
    d.pathOptimizationRange = d.radius * 30.0f;
    d.separationWeight = std::max(0.0f, p.separation);
    d.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OPTIMIZE_VIS | DT_CROWD_OPTIMIZE_TOPO | DT_CROWD_OBSTACLE_AVOIDANCE |
                    (p.separation > 0.0f ? DT_CROWD_SEPARATION : 0);
    d.obstacleAvoidanceType = static_cast<unsigned char>(std::clamp(p.avoidanceQuality, 0, 3));
    d.queryFilterType = 0;
    return d;
}

} // namespace

Crowd::Crowd(NavMesh& mesh, int maxAgents, float maxRadius) : m(std::make_unique<Impl>()) {
    dtNavMesh* nav = mesh.detour();
    if (!nav) return;
    m->crowd = dtAllocCrowd();
    if (!m->crowd || !m->crowd->init(maxAgents, maxRadius, nav)) {
        dtFreeCrowd(m->crowd);
        m->crowd = nullptr;
        return;
    }
    m->capacity = maxAgents;
    m->targets.assign(static_cast<size_t>(maxAgents), glm::vec3(0.0f));
    m->hasTarget.assign(static_cast<size_t>(maxAgents), false);
    // The walkable polygons only (NavMesh flags them 1).
    m->crowd->getEditableFilter(0)->setIncludeFlags(0x01);
    // Avoidance from cheap to good (Detour's sample's settings).
    dtObstacleAvoidanceParams params = *m->crowd->getObstacleAvoidanceParams(0);
    const unsigned char divs[4] = { 5, 5, 7, 7 }, rings[4] = { 2, 2, 2, 3 }, depth[4] = { 1, 2, 2, 3 };
    for (int i = 0; i < 4; ++i) {
        params.velBias = 0.5f;
        params.adaptiveDivs = divs[i];
        params.adaptiveRings = rings[i];
        params.adaptiveDepth = depth[i];
        m->crowd->setObstacleAvoidanceParams(i, &params);
    }
}

Crowd::~Crowd() = default;

bool Crowd::valid() const { return m->crowd != nullptr; }

Crowd::AgentId Crowd::add(const glm::vec3& position, const CrowdAgentParams& params) {
    if (!m->crowd) return 0;
    dtPolyRef ref = 0;
    glm::vec3 on;
    if (!m->snap(position, ref, on)) return 0;
    const dtCrowdAgentParams d = toDetour(params);
    const int i = m->crowd->addAgent(&on.x, &d);
    if (i < 0) return 0;
    m->hasTarget[static_cast<size_t>(i)] = false;
    return static_cast<AgentId>(i + 1);
}

void Crowd::remove(AgentId id) {
    const int i = m->index(id);
    if (i >= 0) m->crowd->removeAgent(i);
}

void Crowd::setParams(AgentId id, const CrowdAgentParams& params) {
    const int i = m->index(id);
    if (i < 0) return;
    const dtCrowdAgentParams d = toDetour(params);
    m->crowd->updateAgentParameters(i, &d);
}

bool Crowd::moveTo(AgentId id, const glm::vec3& target) {
    const int i = m->index(id);
    if (i < 0) return false;
    dtPolyRef ref = 0;
    glm::vec3 on;
    if (!m->snap(target, ref, on)) return false;
    m->targets[static_cast<size_t>(i)] = on;
    m->hasTarget[static_cast<size_t>(i)] = true;
    return m->crowd->requestMoveTarget(i, ref, &on.x);
}

bool Crowd::setVelocity(AgentId id, const glm::vec3& velocity) {
    const int i = m->index(id);
    if (i < 0) return false;
    m->hasTarget[static_cast<size_t>(i)] = false;
    return m->crowd->requestMoveVelocity(i, &velocity.x);
}

bool Crowd::stop(AgentId id) {
    const int i = m->index(id);
    if (i < 0) return false;
    m->hasTarget[static_cast<size_t>(i)] = false;
    return m->crowd->resetMoveTarget(i);
}

void Crowd::update(float dt) {
    if (!m->crowd || dt <= 0.0f) return;
    m->crowd->update(dt, nullptr);
}

bool Crowd::alive(AgentId id) const { return m->index(id) >= 0; }

glm::vec3 Crowd::position(AgentId id) const {
    const int i = m->index(id);
    if (i < 0) return glm::vec3(0.0f);
    const float* p = m->crowd->getAgent(i)->npos;
    return glm::vec3(p[0], p[1], p[2]);
}

glm::vec3 Crowd::velocity(AgentId id) const {
    const int i = m->index(id);
    if (i < 0) return glm::vec3(0.0f);
    const float* v = m->crowd->getAgent(i)->vel;
    return glm::vec3(v[0], v[1], v[2]);
}

bool Crowd::arrived(AgentId id, float radius) const {
    const int i = m->index(id);
    if (i < 0) return false;
    const glm::vec3 d = m->targets[static_cast<size_t>(i)] - position(id);
    return !m->hasTarget[static_cast<size_t>(i)] || d.x * d.x + d.z * d.z <= radius * radius;
}

size_t Crowd::count() const {
    size_t n = 0;
    for (int i = 0; m->crowd && i < m->capacity; ++i)
        if (const dtCrowdAgent* a = m->crowd->getAgent(i); a && a->active) ++n;
    return n;
}

std::vector<Crowd::AgentId> Crowd::agents() const {
    std::vector<AgentId> out;
    for (int i = 0; m->crowd && i < m->capacity; ++i)
        if (const dtCrowdAgent* a = m->crowd->getAgent(i); a && a->active) out.push_back(static_cast<AgentId>(i + 1));
    return out;
}

int Crowd::capacity() const { return m->capacity; }

} // namespace kke::ai
