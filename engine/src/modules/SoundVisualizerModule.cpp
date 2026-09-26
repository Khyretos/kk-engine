#include "kke/modules/SoundVisualizerModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/modules/AudioModule.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace kke {

SoundVisualizerModule::SoundVisualizerModule(std::string settingsPath) : m_path(std::move(settingsPath)) {}

void SoundVisualizerModule::init(Application& app) {
    m_app = &app;
    m_audio = app.getModule<AudioModule>();
    if (!m_audio) log::get(name())->warn("No AudioModule: nothing to visualize");
    load();
}

glm::vec2 SoundVisualizerModule::ringPoint(float azimuth, glm::vec2 center, float radius) {
    return center + glm::vec2(std::sin(azimuth), -std::cos(azimuth)) * radius;
}

bool SoundVisualizerModule::load() {
    std::ifstream in(m_path);
    if (!in) return false;
    try {
        nlohmann::json j = nlohmann::json::parse(in, nullptr, true, /*ignore_comments=*/true);
        const auto& v = j.value("soundVisualizer", nlohmann::json::object());
        settings.enabled = v.value("enabled", settings.enabled);
        settings.ringScale = std::clamp(v.value("ringScale", settings.ringScale), 0.1f, 0.5f);
        settings.markScale = std::clamp(v.value("markScale", settings.markScale), 0.25f, 4.0f);
        settings.opacity = std::clamp(v.value("opacity", settings.opacity), 0.1f, 1.0f);
        settings.minLoudness = std::clamp(v.value("minLoudness", settings.minLoudness), 0.0f, 1.0f);
        settings.captions = v.value("captions", settings.captions);
        settings.holdSeconds = std::clamp(v.value("holdSeconds", settings.holdSeconds), 0.1f, 3.0f);
        if (v.contains("categories")) {
            for (int c = 0; c < int(SoundCategory::Count); ++c) {
                const char* n = soundCategoryName(SoundCategory(c));
                if (!v["categories"].contains(n)) continue;
                const auto& e = v["categories"][n];
                settings.showCategory[c] = e.value("show", true);
                if (e.contains("color") && e["color"].is_array() && e["color"].size() == 3)
                    settings.color[c] = glm::vec3(e["color"][0].get<float>(), e["color"][1].get<float>(), e["color"][2].get<float>());
            }
        }
        return true;
    } catch (const std::exception& e) {
        log::get(name())->warn("Ignoring {}: {}", m_path, e.what());
        return false;
    }
}

bool SoundVisualizerModule::save() const {
    nlohmann::json j;
    {
        std::ifstream in(m_path); // keep other sections of the file
        if (in) try { j = nlohmann::json::parse(in, nullptr, true, true); } catch (...) { j = nlohmann::json::object(); }
    }
    nlohmann::json v;
    v["enabled"] = settings.enabled;
    v["ringScale"] = settings.ringScale;
    v["markScale"] = settings.markScale;
    v["opacity"] = settings.opacity;
    v["minLoudness"] = settings.minLoudness;
    v["captions"] = settings.captions;
    v["holdSeconds"] = settings.holdSeconds;
    for (int c = 0; c < int(SoundCategory::Count); ++c) {
        const auto& col = settings.color[c];
        v["categories"][soundCategoryName(SoundCategory(c))] = {{"show", settings.showCategory[c]}, {"color", {col.r, col.g, col.b}}};
    }
    j["soundVisualizer"] = v;
    const std::string tmp = m_path + ".tmp";
    {
        std::ofstream out(tmp);
        if (!out) return false;
        out << j.dump(2) << "\n";
    }
    return std::rename(tmp.c_str(), m_path.c_str()) == 0;
}

void SoundVisualizerModule::renderUi() {
    if (!m_audio) return;
    if (settings.enabled) drawOverlay();
    if (showPanel) drawPanel();
}

void SoundVisualizerModule::drawOverlay() {
    const ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const glm::vec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    const float radius = std::min(io.DisplaySize.x, io.DisplaySize.y) * settings.ringScale;
    const float px = ImGui::GetFontSize() / 13.0f * settings.markScale;
    std::vector<std::string> captions;

    // Impacts last a few tens of milliseconds; a mark that short can't be
    // seen. Each sound's mark jumps to its loudness and then fades over
    // settings.holdSeconds, also after the sound itself has ended.
    const float dt = std::clamp(io.DeltaTime, 0.0f, 0.1f);
    const float fade = std::exp(-dt * 4.6f / std::max(0.05f, settings.holdSeconds)); // -40 dB over holdSeconds
    for (auto& [id, m] : m_marks) m.level *= fade;
    for (const ActiveSound& a : m_audio->mixer().activeSounds()) {
        Mark& m = m_marks[a.id];
        m.sound = a;
        m.level = std::max(m.level, a.loudness);
    }
    for (auto it = m_marks.begin(); it != m_marks.end();)
        it = it->second.level < settings.minLoudness * 0.5f ? m_marks.erase(it) : std::next(it);

    for (const auto& [id, mark] : m_marks) {
        ActiveSound s = mark.sound;
        s.loudness = mark.level;
        const int c = int(s.category);
        if (!settings.showCategory[c] || s.loudness < settings.minLoudness) continue;
        const glm::vec3& col = settings.color[c];
        const float loud = std::clamp(s.loudness, 0.0f, 1.0f);
        const float alpha = settings.opacity * (0.55f + 0.45f * std::sqrt(loud));
        const ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(col.r, col.g, col.b, alpha));
        const std::string label = m_audio->materials().get(s.material).name + " " + soundCategoryName(s.category);
        if (!s.spatial) { captions.push_back(label); continue; }
        // An arc on the ring: wider and thicker when louder.
        const float halfArc = 0.06f + 0.30f * std::sqrt(loud);
        const float thick = (5.0f + 18.0f * std::sqrt(loud)) * px;
        const bool muffled = s.transmission < 0.7f;
        // A dark edge under every mark keeps it readable on snow and sky alike.
        const ImU32 shadow = IM_COL32(0, 0, 0, int(160 * settings.opacity));
        const int segs = 12;
        auto arc = [&] {
            dl->PathClear();
            for (int i = 0; i <= segs; ++i) {
                const glm::vec2 p = ringPoint(s.azimuth - halfArc + 2.0f * halfArc * float(i) / segs, center, radius);
                dl->PathLineTo(ImVec2(p.x, p.y));
            }
        };
        // Through a wall: a thin line instead of a solid bar.
        const float w = muffled ? std::max(2.5f * px, thick * 0.3f) : thick;
        arc();
        dl->PathStroke(shadow, 0, w + 3.0f * px);
        arc();
        dl->PathStroke(color, 0, w);
        if (settings.captions) {
            const glm::vec2 t = ringPoint(s.azimuth, center, radius + thick + 10.0f * px);
            const std::string text = muffled ? label + " (muffled)" : label;
            const ImVec2 sz = ImGui::CalcTextSize(text.c_str());
            const ImVec2 at(t.x - sz.x * 0.5f, t.y - sz.y * 0.5f);
            dl->AddText(ImVec2(at.x + 1, at.y + 1), shadow, text.c_str());
            dl->AddText(at, color, text.c_str());
        }
    }
    if (settings.captions && !captions.empty()) {
        float y = io.DisplaySize.y - 40.0f * px;
        for (const std::string& t : captions) {
            const std::string line = "[" + t + "]";
            const ImVec2 sz = ImGui::CalcTextSize(line.c_str());
            dl->AddText(ImVec2(center.x - sz.x * 0.5f, y), IM_COL32(255, 255, 255, int(255 * settings.opacity)), line.c_str());
            y -= sz.y + 2.0f;
        }
    }
}

void SoundVisualizerModule::drawPanel() {
    const float s = ImGui::GetFontSize() / 13.0f;
    ImGui::SetNextWindowSize(ImVec2(300 * s, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Sound visualizer", &showPanel)) { ImGui::End(); return; }
    ImGui::Checkbox("Show sounds on screen", &settings.enabled);
    ImGui::Checkbox("Captions", &settings.captions);
    ImGui::SliderFloat("Ring size", &settings.ringScale, 0.1f, 0.5f);
    ImGui::SliderFloat("Mark size", &settings.markScale, 0.25f, 4.0f);
    ImGui::SliderFloat("Opacity", &settings.opacity, 0.1f, 1.0f);
    ImGui::SliderFloat("Quietest shown", &settings.minLoudness, 0.0f, 0.5f);
    ImGui::SliderFloat("Mark stays (s)", &settings.holdSeconds, 0.1f, 3.0f);
    for (int c = 0; c < int(SoundCategory::Count); ++c) {
        ImGui::PushID(c);
        ImGui::Checkbox("##show", &settings.showCategory[c]);
        ImGui::SameLine();
        ImGui::ColorEdit3(soundCategoryName(SoundCategory(c)), &settings.color[c].x, ImGuiColorEditFlags_NoInputs);
        ImGui::PopID();
    }
    if (ImGui::Button("Save")) save();
    ImGui::SameLine();
    if (ImGui::Button("Reload")) load();
    ImGui::End();
}

} // namespace kke
