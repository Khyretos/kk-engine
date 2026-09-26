#pragma once

#include "kke/EngineSettings.h"

namespace kke {

// The resource governor (ACTION_PLAN.md 1.6): "run with what it needs;
// take everything only if the user says so". Turns the player's
// Performance settings and the machine into one budget that the engine
// follows: how many worker threads physics may start, the frame cap, the
// frame cap while the window is in the background, and the render scale.
// Pure logic, unit-tested in tests/test_resource_governor.cpp.
struct ResourceBudget {
    int workerThreads = 1;           // physics / job threads, including the main one
    float frameRateLimit = 0.0f;     // 0 = none (vsync or unlimited)
    float backgroundFrameRate = 0.0f; // cap while unfocused or minimized, 0 = same as focused
    float renderScale = 1.0f;         // 3D resolution relative to the window
    bool useEverything = false;
};

// Cores this process may run on (CPU affinity and container limits on
// Linux, not just what the machine has). At least 1.
unsigned usableCpuCount();

// The rules:
// - "Use everything": every usable core, no caps except the player's own.
// - Otherwise: leave room for the OS and whatever else runs (a browser,
//   voice chat, a stream): half the cores, at least 1, at most 8; with
//   vsync off and no limit set, 144 fps is the cap (past that the GPU
//   only makes heat); 15 fps in the background.
// - An explicit worker count or frame limit from the player always wins.
ResourceBudget computeBudget(const EngineSettings& settings, unsigned usableCores);

} // namespace kke
