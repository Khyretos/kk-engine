#pragma once

#include "kke/GameManifest.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace kke {

// Scans directories of "game folders" and maintains a deduplicated,
// ordered list of GameManifests, keyed by each manifest's own `id` field
// — NOT by folder name or filesystem path. This is the actual mechanism
// behind "importing a game twice shouldn't get stuck": copying the same
// game folder into the marketplace directory again (even under a
// different folder name) re-imports the same `id` and updates the
// existing entry in place rather than creating a duplicate.
//
// This class only builds and persists the index — it does not copy
// files, watch directories, or render a marketplace UI. Those are each
// their own future piece (see README "Game folder convention &
// marketplace").
class MarketplaceIndex {
public:
    // Scans every immediate subdirectory of marketplaceRoot and imports
    // each one that contains a valid game.json. A subdirectory without a
    // game.json is silently skipped (not every folder needs to be a
    // game). A subdirectory WITH a game.json that fails to parse is
    // skipped with a stderr warning naming it — one bad manifest doesn't
    // abort scanning the rest.
    void scanDirectory(const std::string& marketplaceRoot);

    // Imports (or re-imports/updates) a single game folder. Returns
    // false without throwing if game.json is missing or invalid, for the
    // same reason scanDirectory doesn't abort on one bad folder.
    bool importGame(const std::string& gameFolderPath);

    const std::vector<GameManifest>& games() const { return m_games; }
    size_t count() const { return m_games.size(); }

    // Persists the current list AND its order — "the marketplace's own
    // ordering" the games() vector represents — to a marketplace.json
    // file, independent of whatever order the filesystem happens to
    // return subdirectories in.
    void save(const std::string& marketplaceJsonPath) const;
    void load(const std::string& marketplaceJsonPath);

private:
    std::vector<GameManifest> m_games;
    std::unordered_map<std::string, size_t> m_indexById; // id -> position in m_games
};

} // namespace kke
