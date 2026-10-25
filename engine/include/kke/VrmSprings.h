#pragma once

#include "kke/Vrm.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

namespace kke {

// Swings a VRM avatar's spring bones (hair, skirts, ribbons, docs/ANIME.md)
// the way VRM 0.x specifies them: each joint's tail keeps its momentum
// (less the drag), is pulled back towards its rest direction (stiffness)
// and down (gravity), stays at its length, and is pushed out of the
// avatar's collider spheres (head, chest, arms). It works in world space,
// so turning or walking the figure swings the hair.
//
//   VrmSprings springs(avatar);
//   springs.reset(locals, figure);         // once, and after a teleport
//   springs.step(dt, locals, figure);      // every frame, after the animation set `locals`
//   avatar.pose(locals, weights, verts);
//
// Cheap: a few hundred joints a character, no allocation per step.
class VrmSprings {
public:
    explicit VrmSprings(const VrmAvatar& avatar);

    // Puts every tail at rest for this pose: no swing.
    void reset(const std::vector<glm::mat4>& locals, const glm::mat4& modelToWorld);
    // Moves the tails on by dt seconds and turns the spring bones' locals
    // in `locals` to follow them. Steps longer than 1/30 s are split.
    void step(float dt, std::vector<glm::mat4>& locals, const glm::mat4& modelToWorld);
    size_t joints() const { return m_joints.size(); }

private:
    struct Joint {
        int bone = -1;
        int group = 0;
        glm::vec3 axis{ 0.0f, 1.0f, 0.0f }; // the tail's direction in the bone's own space, at rest
        float length = 0.07f;
        glm::quat restRotation{ 1, 0, 0, 0 }; // the bone's local rotation at rest
        glm::vec3 tail{ 0.0f }, prevTail{ 0.0f };
    };
    void worlds(const std::vector<glm::mat4>& locals, const glm::mat4& modelToWorld);
    void substep(float dt, std::vector<glm::mat4>& locals, const glm::mat4& modelToWorld);

    const VrmAvatar& m_avatar;
    std::vector<Joint> m_joints; // parents before children
    std::vector<glm::mat4> m_world; // scratch: every bone's world transform
    struct Sphere { glm::vec3 centre; float radius; };
    std::vector<std::vector<Sphere>> m_spheres; // scratch: each collider group's spheres in world space
    bool m_started = false;
};

} // namespace kke
