#include "kke/GameManifest.h"
#include "kke/DataFile.h"

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace kke {

GameManifest loadGameManifest(const std::string& gameFolderPath) {
    // game.json, game.yml or game.yaml; the newest wins when they differ.
    if (!datafile::exists(gameFolderPath, "game")) {
        throw std::runtime_error("no game.json (or game.yml) found in: " + gameFolderPath);
    }
    datafile::Loaded loaded;
    std::string error;
    if (!datafile::load(gameFolderPath, "game", loaded, &error)) throw std::runtime_error(error);
    const std::string manifestPath = loaded.file.string();
    const nlohmann::json& json = loaded.data;
    if (!json.is_object()) throw std::runtime_error(manifestPath + " must hold an object of fields");

    GameManifest manifest;

    if (!json.contains("id") || !json["id"].is_string() || json["id"].get<std::string>().empty()) {
        throw std::runtime_error(manifestPath + " is missing a required non-empty \"id\" field");
    }
    manifest.id = json["id"].get<std::string>();

    if (!json.contains("title") || !(json["title"].is_string() || json["title"].is_number()) || datafile::text(json, "title").empty()) {
        throw std::runtime_error(manifestPath + " is missing a required non-empty \"title\" field");
    }
    manifest.title = datafile::text(json, "title");

    manifest.description = datafile::text(json, "description");
    manifest.version = datafile::text(json, "version", "0.1.0");
    manifest.icon = datafile::text(json, "icon");
    manifest.banner = datafile::text(json, "banner");
    manifest.engineVersion = datafile::text(json, "engine_version");

    if (json.contains("tags") && json["tags"].is_array()) {
        for (const auto& tag : json["tags"]) {
            if (tag.is_string()) manifest.tags.push_back(tag.get<std::string>());
        }
    }
    if (json.contains("modules") && json["modules"].is_array()) {
        for (const auto& mod : json["modules"]) {
            if (mod.is_string()) manifest.modules.push_back(mod.get<std::string>());
        }
    }

    // "requirements" is entirely optional — a manifest without it is
    // just a manifest that hasn't declared any, not an error.
    if (json.contains("requirements") && json["requirements"].is_object()) {
        const auto& req = json["requirements"];

        auto parseTier = [](const nlohmann::json& tier) {
            GameManifest::HardwareRequirements result;
            result.vramMb = tier.value("vram_mb", 0);
            result.vulkanApiVersion = datafile::text(tier, "vulkan_api_version");
            if (tier.contains("required_device_features") && tier["required_device_features"].is_array()) {
                for (const auto& feature : tier["required_device_features"]) {
                    if (feature.is_string()) result.requiredDeviceFeatures.push_back(feature.get<std::string>());
                }
            }
            return result;
        };

        if (req.contains("minimum") && req["minimum"].is_object()) {
            manifest.minimumRequirements = parseTier(req["minimum"]);
        }
        if (req.contains("recommended") && req["recommended"].is_object()) {
            manifest.recommendedRequirements = parseTier(req["recommended"]);
        }
        manifest.requirementsNotes = datafile::text(req, "notes");
    }

    manifest.sourcePath = gameFolderPath;
    return manifest;
}

} // namespace kke
