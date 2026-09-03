#pragma once

#include <string>
#include <vector>

namespace kke {

// The parsed contents of a "game folder"'s game.json — see the README's
// "Game folder convention & marketplace" section for the full spec and
// the fork-a-game-folder workflow this supports.
//
// `id` is the field that matters most: it's the dedup key the whole
// marketplace import story rests on. Two folders with the same `id` are
// the same game as far as MarketplaceIndex is concerned, no matter what
// the folders themselves are named or where they were copied from.
struct GameManifest {
    std::string id;                   // required, e.g. "com.kreativekompas.demo" — the dedup key
    std::string title;                // required, human-readable
    std::string description;
    std::string version = "0.1.0";
    std::string icon;                 // path relative to the game folder
    std::string banner;               // path relative to the game folder
    std::vector<std::string> tags;
    std::string engineVersion;        // informational for now — see Roadmap on compatibility checking
    std::vector<std::string> modules; // module names this game declares it uses

    // Developer-declared, not automatically inferred — see
    // kke/HardwareCheck.h for why automatic inference from arbitrary
    // game code isn't realistic, and what checking this against real
    // hardware actually looks like.
    struct HardwareRequirements {
        int vramMb = 0;                                  // 0 = not declared
        std::string vulkanApiVersion;                     // e.g. "1.2", empty = not declared
        std::vector<std::string> requiredDeviceFeatures;  // e.g. "largePoints" — see HardwareCheck.cpp for the recognized set
    };
    HardwareRequirements minimumRequirements;
    HardwareRequirements recommendedRequirements;
    std::string requirementsNotes;

    // NOT part of game.json itself. Filled in by whatever imported this
    // manifest (MarketplaceIndex::importGame), so a re-scan can find the
    // manifest again and so two same-id games can be told apart in logs.
    std::string sourcePath;
};

// Parses <gameFolderPath>/game.json. Throws std::runtime_error with a
// specific, human-readable message — file not found, invalid JSON,
// missing required "id" or "title" — rather than letting a bare JSON
// library exception propagate. Callers scanning many game folders need
// to catch-and-skip one bad manifest without the whole scan failing, and
// a specific message is what makes that debuggable rather than mysterious.
GameManifest loadGameManifest(const std::string& gameFolderPath);

} // namespace kke
