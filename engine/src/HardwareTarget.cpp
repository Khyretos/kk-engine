#include "kke/HardwareTarget.h"

#include "kke/DataFile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <stdexcept>

#ifndef KKE_DEFAULT_TARGET
#define KKE_DEFAULT_TARGET ""
#endif

namespace kke {

namespace {

// The built-in tiers. Numbers are starting points chosen from the
// device's screen and GPU class, not measurements; kke_bench's hardware
// profiles (benchmarks/docker/compose.yml) are where they get checked.
std::vector<HardwareTarget> builtinTargets() {
    return {
        { "desktop", "Desktop PC", "Engine defaults: any Windows, Linux or macOS PC with a real GPU.", 60.0f, "{}" },
        { "desktop-low", "Low-end PC",
          "Old laptops, integrated graphics, 2 cores or less than 6 GB of RAM: no MSAA or shadows, 75% render scale, 60 fps cap.", 60.0f,
          R"({ "graphics": { "msaa": 1, "shadows": false, "frameRateLimit": 60 },
               "performance": { "renderScale": 0.75 } })" },
        { "steam-deck", "Steam Deck",
          "Valve Steam Deck (LCD and OLED): fullscreen at the 1280x800 panel, 2x MSAA, 60 fps cap, UI 25% larger for a 7-inch screen.", 60.0f,
          R"({ "graphics": { "fullscreen": true, "vsync": true, "msaa": 2, "frameRateLimit": 60, "uiScale": 1.25 },
               "performance": { "renderScale": 1.0, "backgroundFrameRate": 10 } })" },
        { "handheld-pc", "PC handheld",
          "ROG Ally, Legion Go, MSI Claw and similar: 1080p+ panels on a laptop-class iGPU, so 75% render scale and a 60 fps cap.", 60.0f,
          R"({ "graphics": { "fullscreen": true, "vsync": true, "msaa": 2, "frameRateLimit": 60, "uiScale": 1.25 },
               "performance": { "renderScale": 0.75, "backgroundFrameRate": 10 } })" },
        { "android", "Android phone/tablet",
          "Vulkan phones and tablets: 70% render scale, no shadows, 30 fps cap to keep heat and battery in check, UI 50% larger.", 30.0f,
          R"({ "graphics": { "fullscreen": true, "vsync": true, "msaa": 2, "shadows": false, "frameRateLimit": 30, "uiScale": 1.5 },
               "performance": { "renderScale": 0.7 } })" },
        { "ios", "iPhone/iPad",
          "iOS and iPadOS through MoltenVK: same tier as Android, 75% render scale.", 30.0f,
          R"({ "graphics": { "fullscreen": true, "vsync": true, "msaa": 2, "shadows": false, "frameRateLimit": 30, "uiScale": 1.5 },
               "performance": { "renderScale": 0.75 } })" },
    };
}

std::mutex g_mutex;

// A deque, so the pointers findHardwareTarget() hands out stay valid when
// a target is added later.
std::deque<HardwareTarget>& registry() {
    static std::deque<HardwareTarget> targets = [] {
        std::vector<HardwareTarget> v = builtinTargets();
        return std::deque<HardwareTarget>(v.begin(), v.end());
    }();
    return targets;
}

bool contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

} // namespace

std::vector<HardwareTarget> hardwareTargets() {
    std::lock_guard lock(g_mutex);
    return { registry().begin(), registry().end() };
}

const HardwareTarget* findHardwareTarget(const std::string& name) {
    std::lock_guard lock(g_mutex);
    for (const HardwareTarget& t : registry())
        if (t.name == name) return &t;
    return nullptr;
}

void registerHardwareTarget(const HardwareTarget& target) {
    std::lock_guard lock(g_mutex);
    auto& targets = registry();
    auto it = std::find_if(targets.begin(), targets.end(), [&](const HardwareTarget& t) { return t.name == target.name; });
    if (it != targets.end()) *it = target;
    else targets.push_back(target);
}

int loadHardwareTargetsFile(const std::string& path, std::string* error) {
    nlohmann::json j;
    bool exists = false;
    if (!datafile::loadPath(path, j, error, &exists)) return 0;
    const auto list = j.is_object() ? j.find("targets") : j.end();
    if (list == j.end() || !list->is_array()) {
        if (error) *error = path + ": expected { \"targets\": [ ... ] }";
        return 0;
    }
    int added = 0;
    for (const auto& entry : *list) {
        if (!entry.is_object() || !entry.contains("name") || !entry["name"].is_string()) {
            if (error) *error = path + ": every target needs a \"name\"";
            continue;
        }
        HardwareTarget t;
        t.name = entry["name"].get<std::string>();
        if (const HardwareTarget* existing = findHardwareTarget(t.name)) t = *existing; // extend a built-in
        t.displayName = datafile::text(entry, "displayName", t.displayName.empty() ? t.name : t.displayName);
        t.description = datafile::text(entry, "description", t.description);
        if (auto fps = entry.find("targetFps"); fps != entry.end() && fps->is_number()) t.targetFps = fps->get<float>();
        if (auto s = entry.find("settings"); s != entry.end() && s->is_object()) t.settings = s->dump();
        registerHardwareTarget(t);
        ++added;
    }
    return added;
}

EngineSettings settingsForTarget(const HardwareTarget& target) {
    if (target.settings.empty()) return EngineSettings{};
    try {
        return settingsFromJson(target.settings);
    } catch (const std::exception&) {
        // A broken override must not stop the game from starting; the
        // tests keep the built-in ones valid.
        return EngineSettings{};
    }
}

TargetChoice chooseHardwareTarget(const platform::DeviceHints& h, const std::string& requested, const std::string& buildDefault) {
    auto pick = [](const char* name, std::string reason) { return TargetChoice{ findHardwareTarget(name), std::move(reason) }; };
    if (!requested.empty()) {
        if (const HardwareTarget* t = findHardwareTarget(requested)) return { t, "KKE_TARGET=" + requested };
    }
    if (!buildDefault.empty()) {
        if (const HardwareTarget* t = findHardwareTarget(buildDefault)) return { t, "built with KKE_DEFAULT_TARGET=" + buildDefault };
    }
    if (h.os == "Android") return pick("android", "Android");
    if (h.os == "iOS" || h.os == "iPadOS") return pick("ios", h.os);
    if ((h.vendor == "Valve" && (h.productName == "Jupiter" || h.productName == "Galileo")) || h.steamDeckMode)
        return pick("steam-deck", h.steamDeckMode ? "Steam Game Mode (SteamDeck=1)" : "DMI product " + h.productName);
    if (contains(h.productName, "ROG Ally") || contains(h.productName, "Claw") ||
        (contains(h.vendor, "LENOVO") && contains(h.productName, "83E1")))
        return pick("handheld-pc", "DMI product " + h.productName);
    if ((h.logicalCores > 0 && h.logicalCores <= 2) || (h.systemRamMb > 0 && h.systemRamMb < 6 * 1024))
        return pick("desktop-low", std::to_string(h.logicalCores) + " cores, " + std::to_string(h.systemRamMb) + " MB RAM");
    return pick("desktop", "no specific device recognised");
}

TargetChoice detectHardwareTarget() {
    const char* env = std::getenv("KKE_TARGET");
    TargetChoice c = chooseHardwareTarget(platform::deviceHints(), env ? env : "", KKE_DEFAULT_TARGET);
    if (env && *env && !findHardwareTarget(env)) c.reason += " (KKE_TARGET=" + std::string(env) + " is not a known target)";
    return c;
}

} // namespace kke
