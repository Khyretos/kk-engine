#include "kke/ResourceGovernor.h"

#include "kke/Platform.h"

#include <algorithm>

namespace kke {

unsigned usableCpuCount() { return platform::usableCpuCount(); }

ResourceBudget computeBudget(const EngineSettings& s, unsigned cores) {
    cores = std::max(1u, cores);
    const EngineSettings::Performance& p = s.performance;
    ResourceBudget b;
    b.useEverything = p.useEverything;
    b.renderScale = p.renderScale;
    if (p.useEverything) {
        b.workerThreads = static_cast<int>(cores);
        b.frameRateLimit = s.graphics.frameRateLimit;
        b.backgroundFrameRate = 0.0f;
    } else {
        b.workerThreads = static_cast<int>(std::clamp(cores / 2, 1u, 8u));
        b.frameRateLimit = s.graphics.frameRateLimit > 0.0f ? s.graphics.frameRateLimit : (s.graphics.vsync ? 0.0f : 144.0f);
        b.backgroundFrameRate = p.backgroundFrameRate;
    }
    if (p.workerThreads > 0) b.workerThreads = std::min(p.workerThreads, static_cast<int>(cores) * 2);
    return b;
}

} // namespace kke
