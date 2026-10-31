#include "kke/ContactShadows.h"

#include <algorithm>
#include <cmath>

namespace kke {

float contactShadowMarch(const float pos[3], const float toSun[3], const std::function<float(const float viewPoint[3])>& sceneDepthAt,
                         const ContactShadowSettings& settings, float jitter) {
    const int steps = std::max(1, settings.steps);
    const float step = settings.length / static_cast<float>(steps);
    float lit = 1.0f;
    for (int i = 0; i < steps; ++i) {
        const float t = (static_cast<float>(i) + jitter) * step;
        const float p[3] = { pos[0] + toSun[0] * t, pos[1] + toSun[1] * t, pos[2] + toSun[2] * t };
        const float rayDepth = -p[2];
        if (rayDepth <= 0.0f) break; // behind the camera
        const float scene = sceneDepthAt(p);
        if (scene <= 0.0f) continue; // sky
        const float inFront = rayDepth - scene; // > 0: the frame drew something nearer than the ray
        if (inFront > 0.002f + 0.0005f * rayDepth && inFront < settings.thickness) {
            // Softer towards the end of the march, so the shadow fades with distance from the contact.
            lit = std::min(lit, static_cast<float>(i) / static_cast<float>(steps) * 0.6f);
            break;
        }
    }
    return lit;
}

float contactShadowFade(float distance, const ContactShadowSettings& settings) {
    if (distance >= settings.maxDistance) return 0.0f;
    if (distance <= settings.fadeFrom || settings.maxDistance <= settings.fadeFrom) return 1.0f;
    return 1.0f - (distance - settings.fadeFrom) / (settings.maxDistance - settings.fadeFrom);
}

} // namespace kke
