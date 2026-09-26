#pragma once

#include "kke/EngineSettings.h"
#include "kke/Platform.h"

#include <string>
#include <vector>

namespace kke {

// Hardware targets (docs/PLATFORMS.md): a named device class with the
// settings a game should START on there. A Steam Deck opens at 800p-sized
// UI, 2x MSAA and a 60 fps cap; a phone at a reduced render scale, no
// shadows and 30 fps. The player's saved settings always win: a target
// only changes the defaults a missing settings.json key falls back to.
//
// The same engine and the same game run on every target. What a target
// cannot do is make a phone as fast as a gaming PC; it makes the scaling
// predictable, and kke_bench (benchmarks/) measures each one.
struct HardwareTarget {
    std::string name;        // "steam-deck", used by KKE_TARGET and CMake presets
    std::string displayName; // "Steam Deck"
    std::string description;
    float targetFps = 60.0f; // the frame rate content should be budgeted for here
    // Settings overrides in exactly the settings.json format (JSON or
    // YAML text; see kke/EngineSettings.h). Keys it leaves out keep the
    // engine defaults.
    std::string settings;
};

// Every known target: the built-in ones, then any added by
// registerHardwareTarget() / loadHardwareTargetsFile().
std::vector<HardwareTarget> hardwareTargets();
// nullptr if no target has that name. The pointer stays valid for the
// life of the program; registerHardwareTarget() may change what it points to.
const HardwareTarget* findHardwareTarget(const std::string& name);

// Adds a target, or replaces the one with the same name. For a private
// console backend or a game with its own tiers.
void registerHardwareTarget(const HardwareTarget& target);

// Reads targets from a data file (targets.json or targets.yml, see
// kke/DataFile.h): { "targets": [ { "name": ..., "displayName": ...,
// "description": ..., "targetFps": ..., "settings": { ...settings.json
// sections... } } ] }. Returns how many were registered; a missing file
// is not an error (0, no message), a broken one sets *error.
int loadHardwareTargetsFile(const std::string& path, std::string* error = nullptr);

// EngineSettings{} with the target's overrides applied and sanitized.
EngineSettings settingsForTarget(const HardwareTarget& target);

// Which target this device is, and why (for the log).
struct TargetChoice {
    const HardwareTarget* target = nullptr; // never null from chooseHardwareTarget()
    std::string reason;
};

// Pure decision, unit-tested:
// 1. `requested` (KKE_TARGET) when it names a known target;
// 2. `buildDefault` (the CMake preset's KKE_DEFAULT_TARGET) when known;
// 3. a recognised device: Android / iOS by OS, Steam Deck by DMI name or
//    Steam's Game Mode flag, other PC handhelds by DMI name;
// 4. "desktop-low" for machines with <= 2 cores or < 6 GB of RAM;
// 5. "desktop".
TargetChoice chooseHardwareTarget(const platform::DeviceHints& hints, const std::string& requested = {},
                                  const std::string& buildDefault = {});

// chooseHardwareTarget() for this device: platform::deviceHints(), the
// KKE_TARGET environment variable and the compiled-in KKE_DEFAULT_TARGET.
TargetChoice detectHardwareTarget();

} // namespace kke
