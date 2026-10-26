#include "kke/AnimEvents.h"

#include <algorithm>
#include <cmath>

namespace kke {

void passedEvents(const std::vector<AnimEvent>& events, float before, float advance, float length, bool loop, bool fromStart,
                  std::vector<const AnimEvent*>& out, int maxPasses) {
    if (events.empty() || !(advance >= 0.0f) || !(length > 0.0f)) return;
    // Scan a window per cycle: (lo, hi] in clip time, the first one closed
    // at 0 when the clip has just started.
    float lo = before, hi = before + advance;
    if (!loop) {
        hi = std::min(hi, length);
        for (const AnimEvent& e : events)
            if ((e.at > lo || (fromStart && e.at == lo)) && e.at <= hi && e.at <= length) out.push_back(&e);
        return;
    }
    lo -= std::floor(lo / length) * length; // into [0, length)
    hi = lo + advance;
    bool closedAtLo = fromStart;
    for (int pass = 0; pass < maxPasses; ++pass) {
        const float end = std::min(hi, length);
        for (const AnimEvent& e : events)
            if ((e.at > lo || (closedAtLo && e.at == lo)) && e.at <= end) out.push_back(&e);
        if (hi <= length) break;
        // Wrapped: the next cycle starts at 0, and an event at 0 is passed.
        hi -= length;
        lo = 0.0f;
        closedAtLo = true;
    }
}

void addAnimEvent(std::vector<AnimEvent>& events, float at, std::string name) {
    const auto pos = std::upper_bound(events.begin(), events.end(), at, [](float t, const AnimEvent& e) { return t < e.at; });
    events.insert(pos, AnimEvent{at, std::move(name)});
}

} // namespace kke
