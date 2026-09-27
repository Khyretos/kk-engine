#include "kke/modules/SoundVisualizerModule.h"

#include "kke/Application.h"
#include "kke/DataFile.h"
#include "kke/Log.h"
#include "kke/RmlTextSafety.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/UiModule.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <glm/gtc/constants.hpp>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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
    std::string error;
    bool exists = false;
    nlohmann::json j;
    const bool parsed = datafile::loadPath(m_path, j, &error, &exists); // accessibility.json or .yml
    if (!exists) return false;
    if (!parsed) {
        log::get(name())->warn("Ignoring {}", error);
        return false;
    }
    try {
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
    const std::string target = datafile::saveTarget(m_path).string(); // accessibility.yml stays YAML
    nlohmann::json j;
    if (!datafile::loadPath(target, j) || !j.is_object()) j = nlohmann::json::object(); // keep other sections of the file
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
    const std::string tmp = target + ".tmp";
    {
        std::ofstream out(tmp);
        if (!out) return false;
        out << datafile::dump(j, datafile::formatOf(target).value_or(datafile::Format::Json));
    }
    return std::rename(tmp.c_str(), target.c_str()) == 0;
}

namespace {

// The overlay draws itself (no theme file) and never takes the mouse.
const char* kOverlayRml = R"(
<rml>
<head>
    <title>Sound visualizer</title>
    <style>
        body { font-family: Noto Sans; pointer-events: none; width: 100%; height: 100%; }
        .bar { position: absolute; box-sizing: border-box; border-width: 1.5dp 0; }
        .bar.first { border-left-width: 1.5dp; }
        .bar.last { border-right-width: 1.5dp; }
        .cap { position: absolute; font-size: 13dp; white-space: nowrap; transform: translate(-50%, -50%);
               font-effect: shadow(1dp 1dp #000000a0); }
        #bottom { position: absolute; left: 0; right: 0; bottom: 40dp; text-align: center; font-size: 13dp;
                  font-effect: shadow(1dp 1dp #000000c0); }
    </style>
</head>
<body><div id="marks"></div><div id="bottom"></div></body>
</rml>
)";

std::string rgba(const glm::vec3& c, float alpha) {
    auto b = [](float v) { return std::to_string(int(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f))); };
    return "rgba(" + b(c.r) + "," + b(c.g) + "," + b(c.b) + "," + b(alpha) + ")";
}

std::string pxValue(float v) { return std::to_string(v) + "px"; }

} // namespace

void SoundVisualizerModule::frameStart(const UpdateContext& ctx) {
    if (!m_audio || !makeDocument()) return;
    if (settings.enabled) {
        drawOverlay(ctx.dt);
        return;
    }
    // Off: hide whatever was up.
    for (size_t i = 0; i < m_barsShown; ++i) m_bars[i]->SetProperty("display", "none");
    for (size_t i = 0; i < m_captionsShown; ++i) m_captionEls[i]->SetProperty("display", "none");
    m_barsShown = m_captionsShown = 0;
    if (!m_bottomShown.empty()) m_bottomEl->SetInnerRML("");
    m_bottomShown.clear();
    m_marks.clear();
}

void SoundVisualizerModule::renderUi() {
    // Only the settings window is ImGui (a developer-style panel); the
    // overlay is RmlUi (frameStart).
    if (m_audio && showPanel) drawPanel();
}

void SoundVisualizerModule::shutdown() {
    if (m_doc) m_doc->Close();
    m_doc = nullptr;
    m_marksEl = m_bottomEl = nullptr;
    m_bars.clear();
    m_captionEls.clear();
}

bool SoundVisualizerModule::makeDocument() {
    if (m_doc) return true;
    auto* ui = m_app->getModule<UiModule>();
    if (!ui || !ui->context()) {
        if (!m_noUiLogged && settings.enabled)
            log::get(name())->warn("The sound ring needs a UiModule (RmlUi): add app.addModule<kke::UiModule>()");
        m_noUiLogged = true;
        return false;
    }
    m_doc = ui->context()->LoadDocumentFromMemory(kOverlayRml, "sound_visualizer");
    if (!m_doc) {
        log::get(name())->error("could not build the sound ring document");
        m_noUiLogged = true;
        return false;
    }
    m_marksEl = m_doc->GetElementById("marks");
    m_bottomEl = m_doc->GetElementById("bottom");
    m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    return m_marksEl && m_bottomEl;
}

Rml::Element* SoundVisualizerModule::pooled(std::vector<Rml::Element*>& pool, size_t index, const char* cls) {
    while (pool.size() <= index) {
        Rml::ElementPtr e = m_doc->CreateElement("div");
        e->SetClass(cls, true);
        pool.push_back(m_marksEl->AppendChild(std::move(e)));
    }
    return pool[index];
}

void SoundVisualizerModule::drawOverlay(float frameDt) {
    const Rml::Vector2i size = m_doc->GetContext()->GetDimensions();
    const glm::vec2 screen(float(size.x), float(size.y));
    const glm::vec2 center = screen * 0.5f;
    const float radius = std::min(screen.x, screen.y) * settings.ringScale;
    auto* ui = m_app->getModule<UiModule>();
    const float dp = ui ? ui->dpRatio() : 1.0f;
    const float px = dp * settings.markScale;
    std::vector<std::string> captions;
    // Spatial captions, gathered first: sounds from about the same
    // direction (a wood crate hitting stone is two sounds at one spot)
    // share one caption, "Wood + Stone Impact", instead of printing over
    // each other.
    struct Caption {
        float azimuth = 0.0f, offset = 0.0f;
        SoundCategory category = SoundCategory::Impact;
        std::vector<std::string> materials;
        bool muffled = true; // until one of its sounds is clear
        std::string color;
    };
    std::vector<Caption> spatialCaptions;

    // Impacts last a few tens of milliseconds; a mark that short can't be
    // seen. Each sound's mark jumps to its loudness and then fades over
    // settings.holdSeconds, also after the sound itself has ended.
    const float dt = std::clamp(frameDt, 0.0f, 0.1f);
    const float fade = std::exp(-dt * 4.6f / std::max(0.05f, settings.holdSeconds)); // -40 dB over holdSeconds
    for (auto& [id, m] : m_marks) m.level *= fade;
    for (const ActiveSound& a : m_audio->mixer().activeSounds()) {
        Mark& m = m_marks[a.id];
        m.sound = a;
        m.level = std::max(m.level, a.loudness);
    }
    for (auto it = m_marks.begin(); it != m_marks.end();)
        it = it->second.level < settings.minLoudness * 0.5f ? m_marks.erase(it) : std::next(it);

    size_t bars = 0;
    const std::string shadow = rgba(glm::vec3(0.0f), 0.63f * settings.opacity);
    for (const auto& [id, mark] : m_marks) {
        ActiveSound s = mark.sound;
        s.loudness = mark.level;
        const int c = int(s.category);
        if (!settings.showCategory[c] || s.loudness < settings.minLoudness) continue;
        const float loud = std::clamp(s.loudness, 0.0f, 1.0f);
        const float alpha = settings.opacity * (0.55f + 0.45f * std::sqrt(loud));
        const std::string color = rgba(settings.color[c], alpha);
        const std::string label = m_audio->materials().get(s.material).name + " " + soundCategoryName(s.category);
        if (!s.spatial) { captions.push_back(label); continue; }
        // An arc on the ring, wider and thicker when louder: short rounded
        // bars along it (RmlUi has no paths), each with a dark edge that
        // keeps it readable on snow and sky alike.
        const float halfArc = 0.06f + 0.30f * std::sqrt(loud);
        const float thick = (5.0f + 18.0f * std::sqrt(loud)) * px;
        const bool muffled = s.transmission < 0.7f;
        // Through a wall: a thin line instead of a solid bar.
        const float w = (muffled ? std::max(2.5f * px, thick * 0.3f) : thick) + 3.0f * dp;
        const int segs = 6;
        for (int i = 0; i < segs; ++i) {
            const glm::vec2 p0 = ringPoint(s.azimuth - halfArc + 2.0f * halfArc * float(i) / segs, center, radius);
            const glm::vec2 p1 = ringPoint(s.azimuth - halfArc + 2.0f * halfArc * float(i + 1) / segs, center, radius);
            // Butted end to end (a pixel of overlap), so the joins don't show
            // as darker spots; only the two ends are rounded and edged.
            const bool first = i == 0, last = i == segs - 1;
            const float len = glm::length(p1 - p0) + 1.0f + (first ? w * 0.5f : 0.0f) + (last ? w * 0.5f : 0.0f);
            const glm::vec2 dir = glm::normalize(p1 - p0);
            const glm::vec2 mid = (p0 + p1) * 0.5f + dir * ((first ? -w * 0.25f : 0.0f) + (last ? w * 0.25f : 0.0f));
            const std::string round = pxValue(w * 0.5f);
            Rml::Element* e = pooled(m_bars, bars++, "bar");
            e->SetClass("first", first);
            e->SetClass("last", last);
            e->SetProperty("border-radius", first && last ? round : first ? round + " 0 0 " + round : last ? "0 " + round + " " + round + " 0" : "0");
            e->SetProperty("display", "block");
            e->SetProperty("left", pxValue(mid.x - len * 0.5f));
            e->SetProperty("top", pxValue(mid.y - w * 0.5f));
            e->SetProperty("width", pxValue(len));
            e->SetProperty("height", pxValue(w));
            e->SetProperty("background-color", color);
            e->SetProperty("border-color", shadow);
            e->SetProperty("transform", "rotate(" + std::to_string(glm::degrees(std::atan2(p1.y - p0.y, p1.x - p0.x))) + "deg)");
        }
        if (settings.captions) {
            const std::string& material = m_audio->materials().get(s.material).name;
            Caption* same = nullptr;
            for (Caption& cap : spatialCaptions) {
                const float d = std::fabs(std::remainder(cap.azimuth - s.azimuth, glm::two_pi<float>()));
                if (cap.category == s.category && d < 0.25f) same = &cap;
            }
            if (!same) {
                spatialCaptions.push_back(Caption{s.azimuth, thick + 10.0f * px, s.category, {}, true, color});
                same = &spatialCaptions.back();
            }
            if (std::find(same->materials.begin(), same->materials.end(), material) == same->materials.end()) same->materials.push_back(material);
            same->muffled = same->muffled && muffled;
            same->offset = std::max(same->offset, thick + 10.0f * px);
        }
    }
    for (size_t i = bars; i < m_barsShown; ++i) m_bars[i]->SetProperty("display", "none");
    m_barsShown = bars;

    // Different kinds of sound from about the same direction: stacked outward.
    const float lineHeight = 17.0f * dp;
    for (size_t i = 0; i < spatialCaptions.size(); ++i)
        for (size_t j = 0; j < i; ++j)
            if (std::fabs(std::remainder(spatialCaptions[i].azimuth - spatialCaptions[j].azimuth, glm::two_pi<float>())) < 0.25f)
                spatialCaptions[i].offset = std::max(spatialCaptions[i].offset, spatialCaptions[j].offset + lineHeight);
    size_t shown = 0;
    for (const Caption& cap : spatialCaptions) {
        std::string text;
        for (const std::string& m : cap.materials) text += (text.empty() ? "" : " + ") + m;
        text += std::string(" ") + soundCategoryName(cap.category);
        if (cap.muffled) text += " (muffled)";
        const glm::vec2 t = ringPoint(cap.azimuth, center, radius + cap.offset);
        Rml::Element* e = pooled(m_captionEls, shown++, "cap");
        e->SetProperty("display", "block");
        e->SetProperty("left", pxValue(t.x));
        e->SetProperty("top", pxValue(t.y));
        e->SetProperty("color", cap.color);
        const std::string rml = escapeRmlText(text);
        if (e->GetInnerRML() != rml) e->SetInnerRML(rml);
    }
    for (size_t i = shown; i < m_captionsShown; ++i) m_captionEls[i]->SetProperty("display", "none");
    m_captionsShown = shown;

    // Sounds with no place (UI, music): captioned at the bottom, newest lowest.
    std::string bottom;
    if (settings.captions)
        for (const std::string& t : captions) bottom = "[" + escapeRmlText(t) + "]" + (bottom.empty() ? "" : "<br/>") + bottom;
    if (bottom != m_bottomShown) {
        m_bottomEl->SetInnerRML(bottom);
        m_bottomShown = bottom;
    }
    m_bottomEl->SetProperty("color", rgba(glm::vec3(1.0f), settings.opacity));
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
