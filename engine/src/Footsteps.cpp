#include "kke/Footsteps.h"

#if KKE_ENABLE_JOLT
#include "kke/RigidWorld.h"
#include "kke/modules/AudioModule.h"
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace kke {

void FootstepDetector::reset() {
    m_feet[0] = Foot{};
    m_feet[1] = Foot{};
}

void FootstepDetector::update(int foot, const glm::vec3& position, float height, bool grounded, float bodySpeed, float dt,
                              std::vector<FootstepEvent>& out) {
    Foot& f = m_feet[foot & 1];
    f.sinceStep += dt;
    if (!grounded || !std::isfinite(height)) {
        // In the air: the next touch-down is a landing.
        f.airTime += dt;
        f.groundTime = 0.0f;
        f.lifted = true;
        return;
    }
    f.groundTime += dt;
    // Learn the resting ankle height: the lowest seen, relaxing upward
    // slowly so crouching or a new character recalibrates.
    if (height < f.rest) f.rest = height;
    else f.rest += (height - f.rest) * std::min(1.0f, settings.relax * dt);
    const float above = height - f.rest;
    if (above > settings.liftHeight) f.lifted = true;
    if (f.lifted && above < settings.plantHeight && f.sinceStep >= settings.minInterval) {
        float intensity = 0.25f + 0.75f * std::clamp(bodySpeed / std::max(0.1f, settings.fullSpeed), 0.0f, 1.0f);
        if (f.airTime > 0.25f) intensity = std::max(intensity, std::clamp(0.4f + f.airTime, 0.0f, 1.0f)); // landing
        out.push_back({foot & 1, position, intensity});
        f.lifted = false;
        f.sinceStep = 0.0f;
        f.airTime = 0.0f;
    }
    if (f.groundTime > 0.3f) f.airTime = 0.0f; // stood a while: the next step is just a step
}

#if KKE_ENABLE_JOLT
namespace {
std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
} // namespace

bool CharacterFootsteps::bind(const ModelData& rig) {
    m_foot[0] = m_foot[1] = -1;
    // Names seen in the wild, per side; first match wins.
    const char* names[2][6] = {{"foot_l", "leftfoot", "foot.l", "l_foot", "foot_left", "lfoot"},
                               {"foot_r", "rightfoot", "foot.r", "r_foot", "foot_right", "rfoot"}};
    for (int side = 0; side < 2; ++side)
        for (const char* want : names[side]) {
            for (size_t b = 0; b < rig.bones.size() && m_foot[side] < 0; ++b) {
                std::string n = lower(rig.bones[b].name);
                if (const size_t colon = n.rfind(':'); colon != std::string::npos) n = n.substr(colon + 1); // mixamorig:LeftFoot
                if (n == want) m_foot[side] = int(b);
            }
            if (m_foot[side] >= 0) break;
        }
    detector.reset();
    return bound();
}

void CharacterFootsteps::update(const std::vector<glm::mat4>& modelBones, const glm::mat4& toWorld, RigidWorld& world, AudioModule* audio,
                                float bodySpeed, bool grounded, float dt) {
    if (!bound()) return;
    m_events.clear();
    uint32_t material[2] = {0, 0};
    for (int side = 0; side < 2; ++side) {
        const size_t b = size_t(m_foot[side]);
        if (b >= modelBones.size()) return;
        const glm::vec3 p = glm::vec3(toWorld * modelBones[b][3]);
        const RigidWorld::RayHit h = world.raycast(p + glm::vec3(0, 0.3f, 0), glm::vec3(0, -1, 0), 1.5f);
        const float height = h.hit ? p.y - h.point.y : NAN;
        material[side] = h.hit ? h.material : 0;
        detector.update(side, h.hit ? h.point : p, height, grounded && h.hit, bodySpeed, dt, m_events);
    }
    if (!audio) return;
    for (const FootstepEvent& e : m_events) {
        audio->playFootstep(e.position, material[e.foot], e.intensity, m_seed++, gain);
        ++m_played;
    }
}
#endif

} // namespace kke
