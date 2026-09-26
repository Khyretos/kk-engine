#include "kke/net/Visibility.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace kke::net {

namespace {

bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

} // namespace

// Any clear ray from the viewer's eye (now or `lead` ahead) to a point
// on the subject's padded body (now or `lead` ahead).
bool Visibility::sees(const Player& viewer, const Player& subject) {
    const float lead = static_cast<float>(settings.lead);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec3 eyes[2] = { viewer.feet + up * settings.eyeHeight, viewer.feet + viewer.velocity * lead + up * settings.eyeHeight };
    const glm::vec3 bodies[2] = { subject.feet, subject.feet + subject.velocity * lead };
    for (const glm::vec3& eye : eyes) {
        for (const glm::vec3& feet : bodies) {
            // Sideways, as the viewer sees it: the body's left and right edges.
            glm::vec3 toward = feet - eye;
            toward.y = 0.0f;
            const float len = glm::length(toward);
            const glm::vec3 side = len > 1e-4f ? glm::vec3(-toward.z, 0.0f, toward.x) / len : glm::vec3(1.0f, 0.0f, 0.0f);
            const float r = settings.bodyRadius + settings.margin;
            const float h = settings.bodyHeight;
            const glm::vec3 points[] = {
                feet + up * (h - 0.1f), feet + up * (h * 0.5f), feet + up * 0.1f,
                feet + up * (h * 0.5f) + side * r, feet + up * (h * 0.5f) - side * r,
                feet + up * (h + settings.margin),
            };
            for (const glm::vec3& p : points) {
                ++raysLastUpdate;
                if (m_clear(eye, p)) return true;
            }
        }
    }
    return false;
}

void Visibility::update(double now, const std::vector<Player>& players) {
    raysLastUpdate = 0;
    tracedPairs = 0;
    std::set<uint8_t> present;
    for (const Player& p : players) present.insert(p.id);
    for (auto it = m_pairs.begin(); it != m_pairs.end();) {
        if (!present.count(it->first.first) || !present.count(it->first.second)) it = m_pairs.erase(it);
        else ++it;
    }
    for (const Player& v : players) {
        for (const Player& s : players) {
            if (v.id == s.id) continue;
            Pair& pair = m_pairs[{ v.id, s.id }];
            if (!finite(v.feet) || !finite(s.feet) || !finite(v.velocity) || !finite(s.velocity)) {
                pair.visible = false; // a broken state is never a reason to reveal anyone
                continue;
            }
            const float d = glm::length(s.feet - v.feet);
            if (d > settings.maxDistance) {
                pair.visible = false;
                pair.nextCheck = -1e300;
                continue;
            }
            if (d <= settings.hearingRadius) {
                pair.visible = true;
                pair.lastSeen = now;
                pair.nextCheck = -1e300;
                continue;
            }
            if (now < pair.nextCheck) continue;
            // Viewer and subject see each other along the same rays, but
            // their eyes and bodies differ: trace each direction on its own.
            ++tracedPairs;
            pair.nextCheck = now + settings.recheck;
            if (sees(v, s)) {
                pair.visible = true;
                pair.lastSeen = now;
            } else {
                pair.visible = now - pair.lastSeen <= settings.keepVisible;
            }
        }
    }
}

bool Visibility::visible(uint8_t viewer, uint8_t subject) const {
    auto it = m_pairs.find({ viewer, subject });
    return it != m_pairs.end() && it->second.visible;
}

void Visibility::forget(uint8_t id) {
    for (auto it = m_pairs.begin(); it != m_pairs.end();) {
        if (it->first.first == id || it->first.second == id) it = m_pairs.erase(it);
        else ++it;
    }
}

} // namespace kke::net
