#include "kke/ShadowRays.h"

#include <cmath>

namespace kke {

ShadowRays shadowRaysFromName(const std::string& name, ShadowRays fallback) {
    if (name == "off") return ShadowRays::Off;
    if (name == "kke") return ShadowRays::Kke;
    if (name == "classic") return ShadowRays::Classic;
    return fallback;
}

bool shadowRayTraced(ShadowRays mode, float distance, const ShadowRaySettings& settings) {
    if (mode == ShadowRays::Off || distance > settings.maxDistance) return false;
    return mode == ShadowRays::Classic || distance >= settings.kkeFrom;
}

float shadowRayLookup(float lit, float tracedDepth, float expectedDepth, const ShadowRaySettings& settings) {
    if (tracedDepth <= 0.0f) return -1.0f;
    return std::abs(tracedDepth - expectedDepth) <= settings.reprojectSlack * expectedDepth + 0.05f ? lit : -1.0f;
}

} // namespace kke
