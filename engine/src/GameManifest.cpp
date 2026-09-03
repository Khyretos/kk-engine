#include "kke/GameManifest.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>

namespace kke {

GameManifest loadGameManifest(const std::string& gameFolderPath) {
    std::string manifestPath = gameFolderPath + "/game.json";

    std::ifstream file(manifestPath);
    if (!file.is_open()) {
        throw std::runtime_error("no game.json found at: " + manifestPath);
    }

    nlohmann::json json;
    try {
        file >> json;
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error("invalid JSON in " + manifestPath + ": " + e.what());
    }

    GameManifest manifest;

    if (!json.contains("id") || !json["id"].is_string() || json["id"].get<std::string>().empty()) {
        throw std::runtime_error(manifestPath + " is missing a required non-empty \"id\" field");
    }
    manifest.id = json["id"].get<std::string>();

    if (!json.contains("title") || !json["title"].is_string() || json["title"].get<std::string>().empty()) {
        throw std::runtime_error(manifestPath + " is missing a required non-empty \"title\" field");
    }
    manifest.title = json["title"].get<std::string>();

    manifest.description = json.value("description", std::string());
    manifest.version = json.value("version", std::string("0.1.0"));
    manifest.icon = json.value("icon", std::string());
    manifest.banner = json.value("banner", std::string());
    manifest.engineVersion = json.value("engine_version", std::string());

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
            result.vulkanApiVersion = tier.value("vulkan_api_version", std::string());
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
        manifest.requirementsNotes = req.value("notes", std::string());
    }

    manifest.sourcePath = gameFolderPath;
    return manifest;
}

} // namespace kke
