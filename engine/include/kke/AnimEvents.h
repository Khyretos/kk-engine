#pragma once

#include <string>
#include <vector>

namespace kke {

// Animation events (Unity's animation events, Unreal's anim notifies): a
// name at a point in a clip ("footstep" when a foot lands, "hit" when the
// sword arrives, "release" when the ball leaves the hand), reported on the
// update the clip's time passes it. Kept by kke::Animator (per state, at a
// fraction of the cycle) and ModelModule (per clip, in seconds).
struct AnimEvent {
    float at = 0.0f;   // where in the clip: seconds or a 0..1 fraction (the owner says which)
    std::string name;
};

// The events a clip's time passed in one update: from `before` it moved
// `advance` forward (both in the events' unit) on a clip `length` long.
// An event fires when before < at <= before + advance, once per pass, so
// a looping clip fires it again every cycle (at most `maxPasses` times in
// one update, for a huge dt). `fromStart` (the first update after the
// clip started) also fires events at exactly 0. A clip that doesn't loop
// stops at its end: events past `length` never fire.
// Appends to `out` in time order; events must be sorted by `at`.
void passedEvents(const std::vector<AnimEvent>& events, float before, float advance, float length, bool loop, bool fromStart,
                  std::vector<const AnimEvent*>& out, int maxPasses = 4);
// Inserts keeping `events` sorted by time (same times keep their order).
void addAnimEvent(std::vector<AnimEvent>& events, float at, std::string name);

} // namespace kke
