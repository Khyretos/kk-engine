#pragma once

#include "kke/GameManifest.h"

#include <string>
#include <vector>

namespace kke {

class VulkanDevice;

// Checks a game's developer-declared requirements (game.json's
// "requirements" section — see GameManifest.h) against the ACTUAL
// running hardware. This is deliberately NOT automatic requirement
// inference from game code — that isn't realistic in general (it would
// need to actually run and profile the game, not just read its source),
// so the honest, buildable version of this feature is: the developer
// states what their game needs, the engine checks it against what's
// really there, and reports the difference. Nothing here blocks
// anything — see checkHardwareRequirements()'s doc comment.
struct HardwareCheckResult {
    bool meetsMinimum = true;
    bool meetsRecommended = true;
    std::vector<std::string> warnings; // human-readable, empty if everything's satisfied
};

// Compares manifest's minimum/recommended requirements against device's
// real capabilities (Vulkan API version, VRAM via VMA's heap budget
// query, and a small set of recognized device features). Returns
// warnings; NEVER throws, blocks, or refuses to let the game continue —
// "the specs exceed their capabilities" is information for the player
// to act on, not a decision the engine makes for them. If a game wants
// to actually refuse to launch below its minimum, that's the game's
// choice to make with this result, not this function's.
//
// Recognized feature names for requiredDeviceFeatures (see
// VulkanDevice::largePointsSupported() and friends) — currently just
// "largePoints"; extend this list as VulkanDevice exposes more queryable
// features, and treat an unrecognized name as "can't verify" (a
// warning, not a silent pass) rather than silently ignoring it.
HardwareCheckResult checkHardwareRequirements(const GameManifest& manifest, VulkanDevice& device);

} // namespace kke
