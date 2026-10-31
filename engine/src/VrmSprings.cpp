#include "kke/VrmSprings.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <functional>

namespace kke {

namespace {

glm::quat rotationOf(const glm::mat4& m) {
    glm::mat3 r(m);
    for (int i = 0; i < 3; ++i) {
        const float len = glm::length(r[i]);
        if (len > 1e-8f) r[i] /= len;
    }
    return glm::normalize(glm::quat_cast(r));
}

glm::quat fromTo(const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 u = glm::normalize(a), v = glm::normalize(b);
    const float d = glm::dot(u, v);
    if (d > 0.999999f) return glm::quat(1, 0, 0, 0);
    if (d < -0.999999f) {
        glm::vec3 side = glm::cross(glm::vec3(1, 0, 0), u);
        if (glm::dot(side, side) < 1e-6f) side = glm::cross(glm::vec3(0, 1, 0), u);
        return glm::angleAxis(glm::pi<float>(), glm::normalize(side));
    }
    const glm::vec3 c = glm::cross(u, v);
    return glm::normalize(glm::quat(1.0f + d, c.x, c.y, c.z));
}

} // namespace

VrmSprings::VrmSprings(const VrmAvatar& avatar) : m_avatar(avatar) {
    const std::vector<ModelBone>& bones = avatar.model.bones;
    std::vector<std::vector<int>> children(bones.size());
    for (size_t b = 0; b < bones.size(); ++b)
        if (bones[b].parent >= 0) children[static_cast<size_t>(bones[b].parent)].push_back(static_cast<int>(b));
    std::vector<bool> taken(bones.size(), false);
    for (size_t g = 0; g < avatar.springs.size(); ++g) {
        std::function<void(int)> add = [&](int b) {
            if (taken[static_cast<size_t>(b)]) return;
            taken[static_cast<size_t>(b)] = true;
            Joint j;
            j.bone = b;
            j.group = static_cast<int>(g);
            const glm::mat4& local = bones[static_cast<size_t>(b)].localRest;
            j.restRotation = rotationOf(local);
            // The tail: the first child, or 7 cm on along the bone's own
            // direction from its parent for the last bone of a chain.
            glm::vec3 tail;
            if (!children[static_cast<size_t>(b)].empty()) tail = glm::vec3(bones[static_cast<size_t>(children[static_cast<size_t>(b)][0])].localRest[3]);
            else {
                const glm::vec3 own = glm::vec3(local[3]);
                const glm::vec3 dir = glm::length(own) > 1e-6f ? glm::inverse(glm::mat3_cast(j.restRotation)) * own : glm::vec3(0, 1, 0);
                tail = glm::normalize(dir) * 0.07f;
            }
            j.length = std::max(glm::length(tail), 1e-4f);
            j.axis = glm::length(tail) > 1e-6f ? glm::normalize(tail) : glm::vec3(0, 1, 0);
            m_joints.push_back(j);
            for (int c : children[static_cast<size_t>(b)]) add(c);
        };
        for (int root : avatar.springs[g].roots) add(root);
    }
    // Parents before children, whatever order the groups listed them in.
    std::vector<int> depth(bones.size(), 0);
    for (size_t b = 0; b < bones.size(); ++b)
        depth[b] = bones[b].parent >= 0 ? depth[static_cast<size_t>(bones[b].parent)] + 1 : 0;
    std::stable_sort(m_joints.begin(), m_joints.end(), [&](const Joint& a, const Joint& b) {
        return depth[static_cast<size_t>(a.bone)] < depth[static_cast<size_t>(b.bone)];
    });
    m_world.resize(bones.size());
    m_spheres.resize(avatar.colliderGroups.size());
}

void VrmSprings::worlds(const std::vector<glm::mat4>& locals, const glm::mat4& modelToWorld) {
    const std::vector<ModelBone>& bones = m_avatar.model.bones;
    for (size_t b = 0; b < bones.size(); ++b)
        m_world[b] = (bones[b].parent >= 0 ? m_world[static_cast<size_t>(bones[b].parent)] : modelToWorld) * locals[b];
    for (size_t g = 0; g < m_avatar.colliderGroups.size(); ++g) {
        const VrmColliderGroup& cg = m_avatar.colliderGroups[g];
        m_spheres[g].clear();
        if (cg.bone < 0) continue;
        const glm::mat4& w = m_world[static_cast<size_t>(cg.bone)];
        for (const VrmCollider& c : cg.colliders) m_spheres[g].push_back({ glm::vec3(w * glm::vec4(c.offset, 1.0f)), c.radius });
    }
}

void VrmSprings::reset(const std::vector<glm::mat4>& locals, const glm::mat4& modelToWorld) {
    worlds(locals, modelToWorld);
    for (Joint& j : m_joints) {
        const glm::mat4& w = m_world[static_cast<size_t>(j.bone)];
        j.tail = j.prevTail = glm::vec3(w * glm::vec4(j.axis * j.length, 1.0f));
    }
    m_started = true;
}

void VrmSprings::step(float dt, std::vector<glm::mat4>& locals, const glm::mat4& modelToWorld) {
    if (!m_started) reset(locals, modelToWorld);
    if (dt <= 0.0f) return;
    dt = std::min(dt, 0.25f); // a stall: don't fling the hair
    const int steps = std::max(1, static_cast<int>(std::ceil(dt * 30.0f)));
    for (int i = 0; i < steps; ++i) substep(dt / static_cast<float>(steps), locals, modelToWorld);
}

void VrmSprings::substep(float dt, std::vector<glm::mat4>& locals, const glm::mat4& modelToWorld) {
    worlds(locals, modelToWorld);
    const std::vector<ModelBone>& bones = m_avatar.model.bones;
    for (Joint& j : m_joints) {
        const VrmSpringGroup& g = m_avatar.springs[static_cast<size_t>(j.group)];
        const size_t b = static_cast<size_t>(j.bone);
        const int parent = bones[b].parent;
        const glm::mat4& parentWorld = parent >= 0 ? m_world[static_cast<size_t>(parent)] : modelToWorld;
        glm::mat4& local = locals[b];
        // Where the joint sits now (its parent may have swung this step).
        const glm::mat4 restLocal = glm::translate(glm::mat4(1.0f), glm::vec3(local[3])) * glm::mat4_cast(j.restRotation);
        const glm::mat4 restWorld = parentWorld * restLocal;
        const glm::vec3 head(restWorld[3]);
        const glm::quat restRot = rotationOf(restWorld);

        glm::vec3 next = j.tail + (j.tail - j.prevTail) * (1.0f - g.drag)
                         + restRot * j.axis * (g.stiffness * dt)
                         + g.gravityDir * (g.gravityPower * dt);
        auto keepLength = [&](const glm::vec3& p) {
            const glm::vec3 d = p - head;
            const float len = glm::length(d);
            return len > 1e-6f ? head + d / len * j.length : head + restRot * j.axis * j.length;
        };
        next = keepLength(next);
        for (uint32_t cg : g.colliderGroups)
            for (const Sphere& s : m_spheres[cg]) {
                const float reach = s.radius + g.hitRadius;
                const glm::vec3 d = next - s.centre;
                const float dist = glm::length(d);
                if (dist < reach && dist > 1e-6f) next = keepLength(s.centre + d / dist * reach);
            }
        j.prevTail = j.tail;
        j.tail = next;

        // Turn the bone so its tail points at the new tail.
        const glm::vec3 want = glm::inverse(restRot) * (next - head);
        const glm::quat swing = fromTo(j.axis, want);
        const glm::quat rot = j.restRotation * swing;
        glm::vec3 scale(glm::length(glm::vec3(local[0])), glm::length(glm::vec3(local[1])), glm::length(glm::vec3(local[2])));
        local = glm::translate(glm::mat4(1.0f), glm::vec3(local[3])) * glm::mat4_cast(rot) * glm::scale(glm::mat4(1.0f), scale);
        m_world[b] = parentWorld * local;
    }
}

} // namespace kke
