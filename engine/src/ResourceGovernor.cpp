#include "kke/ResourceGovernor.h"

#include <algorithm>
#include <thread>

#if defined(__linux__)
#include <sched.h>
#endif

namespace kke {

unsigned usableCpuCount() {
    unsigned n = std::thread::hardware_concurrency();
#if defined(__linux__)
    // hardware_concurrency() counts every core in the machine, ignoring
    // CPU affinity (taskset, container limits, the min-spec emulation in
    // docs/PERFORMANCE_NOTES.md).
    cpu_set_t affinity;
    if (sched_getaffinity(0, sizeof(affinity), &affinity) == 0) n = static_cast<unsigned>(CPU_COUNT(&affinity));
#endif
    return std::max(1u, n);
}

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
