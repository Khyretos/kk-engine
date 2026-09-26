#include "kke/EngineSettings.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace kke {

namespace {

template <typename T>
void readIfPresent(const nlohmann::json& obj, const char* key, T& out) {
    auto it = obj.find(key);
    if (it == obj.end()) return;
    try {
        out = it->get<T>();
    } catch (const nlohmann::json::exception&) {
        // Wrong type for this key (e.g. "vsync": "yes") — keep the default
        // rather than failing the whole file.
    }
}

const nlohmann::json& section(const nlohmann::json& root, const char* name) {
    static const nlohmann::json empty = nlohmann::json::object();
    auto it = root.find(name);
    return (it != root.end() && it->is_object()) ? *it : empty;
}

} // namespace

void EngineSettings::sanitize() {
    graphics.frameRateLimit = std::clamp(graphics.frameRateLimit, 0.0f, 1000.0f);
    if (graphics.frameRateLimit > 0.0f && graphics.frameRateLimit < 15.0f) graphics.frameRateLimit = 15.0f;
    graphics.fieldOfView = std::clamp(graphics.fieldOfView, 30.0f, 120.0f);
    graphics.brightness = std::clamp(graphics.brightness, 0.0f, 3.0f);
    graphics.uiScale = std::clamp(graphics.uiScale, 0.5f, 2.5f);
    audio.master = std::clamp(audio.master, 0, 100);
    audio.music = std::clamp(audio.music, 0, 100);
    audio.effects = std::clamp(audio.effects, 0, 100);
    controls.mouseSensitivity = std::clamp(controls.mouseSensitivity, 0.1f, 5.0f);
    if (gameplay.difficulty != "easy" && gameplay.difficulty != "normal" && gameplay.difficulty != "hard") {
        gameplay.difficulty = "normal";
    }
    gameplay.maxPhysicsStepsPerFrame = std::clamp(gameplay.maxPhysicsStepsPerFrame, 1, 8);
    performance.workerThreads = std::clamp(performance.workerThreads, 0, 256);
    performance.renderScale = std::clamp(performance.renderScale, 0.5f, 1.0f);
    performance.backgroundFrameRate = std::clamp(performance.backgroundFrameRate, 0.0f, 240.0f);
    if (performance.backgroundFrameRate > 0.0f && performance.backgroundFrameRate < 5.0f) performance.backgroundFrameRate = 5.0f;
}

bool EngineSettings::operator==(const EngineSettings& o) const {
    return graphics.fullscreen == o.graphics.fullscreen && graphics.vsync == o.graphics.vsync &&
           graphics.frameRateLimit == o.graphics.frameRateLimit && graphics.fieldOfView == o.graphics.fieldOfView &&
           graphics.shadows == o.graphics.shadows && graphics.brightness == o.graphics.brightness &&
           graphics.uiScale == o.graphics.uiScale && graphics.showDebugOverlay == o.graphics.showDebugOverlay &&
           audio.master == o.audio.master && audio.music == o.audio.music && audio.effects == o.audio.effects &&
           audio.muteWhenUnfocused == o.audio.muteWhenUnfocused &&
           controls.mouseSensitivity == o.controls.mouseSensitivity && controls.invertY == o.controls.invertY &&
           controls.keyBindings == o.controls.keyBindings &&
           gameplay.difficulty == o.gameplay.difficulty &&
           gameplay.maxPhysicsStepsPerFrame == o.gameplay.maxPhysicsStepsPerFrame &&
           gameplay.showDamageNumbers == o.gameplay.showDamageNumbers &&
           performance.useEverything == o.performance.useEverything && performance.workerThreads == o.performance.workerThreads &&
           performance.renderScale == o.performance.renderScale &&
           performance.backgroundFrameRate == o.performance.backgroundFrameRate && custom == o.custom;
}

std::string settingsToJson(const EngineSettings& s) {
    nlohmann::json j;
    j["version"] = 1;
    j["graphics"] = {
        {"fullscreen", s.graphics.fullscreen}, {"vsync", s.graphics.vsync},
        {"frameRateLimit", s.graphics.frameRateLimit}, {"fieldOfView", s.graphics.fieldOfView},
        {"shadows", s.graphics.shadows}, {"brightness", s.graphics.brightness},
        {"uiScale", s.graphics.uiScale}, {"showDebugOverlay", s.graphics.showDebugOverlay},
    };
    j["audio"] = {
        {"master", s.audio.master}, {"music", s.audio.music}, {"effects", s.audio.effects},
        {"muteWhenUnfocused", s.audio.muteWhenUnfocused},
    };
    j["controls"] = {
        {"mouseSensitivity", s.controls.mouseSensitivity}, {"invertY", s.controls.invertY},
        {"keyBindings", s.controls.keyBindings},
    };
    j["gameplay"] = {
        {"difficulty", s.gameplay.difficulty}, {"maxPhysicsStepsPerFrame", s.gameplay.maxPhysicsStepsPerFrame},
        {"showDamageNumbers", s.gameplay.showDamageNumbers},
    };
    j["performance"] = {
        {"useEverything", s.performance.useEverything}, {"workerThreads", s.performance.workerThreads},
        {"renderScale", s.performance.renderScale}, {"backgroundFrameRate", s.performance.backgroundFrameRate},
    };
    j["custom"] = s.custom;
    return j.dump(2);
}

EngineSettings settingsFromJson(const std::string& text) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(text);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(std::string("settings: invalid JSON: ") + e.what());
    }
    if (!j.is_object()) throw std::runtime_error("settings: top level must be a JSON object");

    EngineSettings s;
    const auto& g = section(j, "graphics");
    readIfPresent(g, "fullscreen", s.graphics.fullscreen);
    readIfPresent(g, "vsync", s.graphics.vsync);
    readIfPresent(g, "frameRateLimit", s.graphics.frameRateLimit);
    readIfPresent(g, "fieldOfView", s.graphics.fieldOfView);
    readIfPresent(g, "shadows", s.graphics.shadows);
    readIfPresent(g, "brightness", s.graphics.brightness);
    readIfPresent(g, "uiScale", s.graphics.uiScale);
    readIfPresent(g, "showDebugOverlay", s.graphics.showDebugOverlay);
    const auto& a = section(j, "audio");
    readIfPresent(a, "master", s.audio.master);
    readIfPresent(a, "music", s.audio.music);
    readIfPresent(a, "effects", s.audio.effects);
    readIfPresent(a, "muteWhenUnfocused", s.audio.muteWhenUnfocused);
    const auto& c = section(j, "controls");
    readIfPresent(c, "mouseSensitivity", s.controls.mouseSensitivity);
    readIfPresent(c, "invertY", s.controls.invertY);
    // Merge, don't replace: a binding added in a newer build keeps its
    // default when an older file doesn't mention it.
    std::map<std::string, std::string> bindings;
    readIfPresent(c, "keyBindings", bindings);
    for (auto& [action, key] : bindings) s.controls.keyBindings[action] = key;
    const auto& gp = section(j, "gameplay");
    readIfPresent(gp, "difficulty", s.gameplay.difficulty);
    readIfPresent(gp, "maxPhysicsStepsPerFrame", s.gameplay.maxPhysicsStepsPerFrame);
    readIfPresent(gp, "showDamageNumbers", s.gameplay.showDamageNumbers);
    const auto& pf = section(j, "performance");
    readIfPresent(pf, "useEverything", s.performance.useEverything);
    readIfPresent(pf, "workerThreads", s.performance.workerThreads);
    readIfPresent(pf, "renderScale", s.performance.renderScale);
    readIfPresent(pf, "backgroundFrameRate", s.performance.backgroundFrameRate);
    readIfPresent(j, "custom", s.custom);
    s.sanitize();
    return s;
}

EngineSettings loadSettingsFile(const std::string& path, std::string* errorOut) {
    std::ifstream in(path);
    if (!in) {
        if (errorOut) *errorOut = "no settings file at '" + path + "' (using defaults)";
        return EngineSettings{};
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    try {
        return settingsFromJson(buffer.str());
    } catch (const std::exception& e) {
        if (errorOut) *errorOut = e.what();
        return EngineSettings{};
    }
}

bool saveSettingsFile(const EngineSettings& settings, const std::string& path) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << settingsToJson(settings) << '\n';
    return static_cast<bool>(out);
}

} // namespace kke
