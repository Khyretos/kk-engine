#include "kke/MarketplaceIndex.h"
#include "kke/Log.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

namespace kke {

void MarketplaceIndex::scanDirectory(const std::string& marketplaceRoot) {
    namespace fs = std::filesystem;

    if (!fs::exists(marketplaceRoot) || !fs::is_directory(marketplaceRoot)) {
        log::get("Marketplace")->warn("not a directory, skipping scan: {}", marketplaceRoot);
        return;
    }

    for (const auto& entry : fs::directory_iterator(marketplaceRoot)) {
        if (!entry.is_directory()) continue;
        if (!fs::exists(entry.path() / "game.json")) continue; // not every subfolder need be a game

        importGame(entry.path().string());
    }
}

bool MarketplaceIndex::importGame(const std::string& gameFolderPath) {
    GameManifest manifest;
    try {
        manifest = loadGameManifest(gameFolderPath);
    } catch (const std::exception& e) {
        log::get("Marketplace")->warn("skipping {}: {}", gameFolderPath, e.what());
        return false;
    }

    auto existing = m_indexById.find(manifest.id);
    if (existing != m_indexById.end()) {
        // Same id already known — update in place. This preserves this
        // game's position in the marketplace's ordering, and is exactly
        // what makes "importing the same game folder twice" a no-op
        // instead of a duplicate entry.
        log::get("Marketplace")->info("re-imported existing game '{}' (was at {}, now at {})",
                                        manifest.id, m_games[existing->second].sourcePath, manifest.sourcePath);
        m_games[existing->second] = manifest;
    } else {
        m_indexById[manifest.id] = m_games.size();
        m_games.push_back(manifest);
        log::get("Marketplace")->info("imported '{}' ({})", manifest.id, manifest.title);
    }
    return true;
}

void MarketplaceIndex::save(const std::string& marketplaceJsonPath) const {
    nlohmann::json json;
    json["games"] = nlohmann::json::array();

    for (const auto& game : m_games) {
        nlohmann::json entry;
        entry["id"] = game.id;
        entry["title"] = game.title;
        entry["description"] = game.description;
        entry["version"] = game.version;
        entry["icon"] = game.icon;
        entry["banner"] = game.banner;
        entry["tags"] = game.tags;
        entry["engine_version"] = game.engineVersion;
        entry["modules"] = game.modules;
        entry["source_path"] = game.sourcePath;
        json["games"].push_back(entry);
    }

    std::ofstream file(marketplaceJsonPath);
    file << json.dump(2);
}

void MarketplaceIndex::load(const std::string& marketplaceJsonPath) {
    std::ifstream file(marketplaceJsonPath);
    if (!file.is_open()) return; // no existing index yet — not an error

    nlohmann::json json;
    file >> json;

    m_games.clear();
    m_indexById.clear();

    if (!json.contains("games") || !json["games"].is_array()) return;

    for (const auto& entry : json["games"]) {
        GameManifest game;
        game.id = entry.value("id", std::string());
        if (game.id.empty()) continue; // corrupt entry — skip rather than crash

        game.title = entry.value("title", std::string());
        game.description = entry.value("description", std::string());
        game.version = entry.value("version", std::string());
        game.icon = entry.value("icon", std::string());
        game.banner = entry.value("banner", std::string());
        game.engineVersion = entry.value("engine_version", std::string());
        game.sourcePath = entry.value("source_path", std::string());
        if (entry.contains("tags")) game.tags = entry["tags"].get<std::vector<std::string>>();
        if (entry.contains("modules")) game.modules = entry["modules"].get<std::vector<std::string>>();

        m_indexById[game.id] = m_games.size();
        m_games.push_back(game);
    }
}

} // namespace kke
