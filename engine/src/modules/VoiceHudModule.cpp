#include "kke/modules/VoiceHudModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/RmlTextSafety.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/UiModule.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <typeindex>

namespace kke {

namespace {

// Draws itself (no theme file) and never takes the mouse.
const char* kHudRml = R"(
<rml>
<head>
    <title>Voices</title>
    <style>
        body { font-family: Noto Sans; pointer-events: none; width: 100%; height: 100%; }
        .mark, .arrow { position: absolute; width: 0; height: 0; }
        .ring { position: absolute; border-radius: 50%; border-color: #60e080; background-color: #60e08040; }
        .nm { position: absolute; white-space: nowrap; font-size: 13dp; color: #ffffff; transform: translate(-50%, 0);
              font-effect: shadow(1dp 1dp #000000c0); }
        .chev { position: absolute; width: 14dp; height: 14dp; margin-left: -7dp; margin-top: -7dp;
                border-top-width: 4dp; border-right-width: 4dp; border-color: #60e080; }
        #list { position: absolute; width: 240dp; }
        .slot { display: block; height: 26dp; margin-bottom: 3dp; padding: 3dp 8dp; border-radius: 6dp;
                background-color: #10141ab0; color: #ffffff; font-size: 14dp; }
        .slot.hidden, .hidden { display: none; }
        .slot.talking { background-color: #1d4a2ad0; }
        .slot.muted { color: #9aa0a8; }
        .num { display: inline-block; width: 16dp; color: #a0c8ff; }
        .dir { display: inline-block; position: relative; width: 20dp; height: 18dp; vertical-align: middle; }
        .dir .chev { left: 10dp; top: 10dp; width: 8dp; height: 8dp; margin-left: -4dp; margin-top: -4dp;
                     border-top-width: 2.5dp; border-right-width: 2.5dp; border-color: #ffffff; }
        .slot.muted .dir .chev { border-color: #9aa0a8; }
        .who { display: inline-block; width: 110dp; white-space: nowrap; overflow: hidden; }
        .info { display: inline-block; width: 60dp; font-size: 12dp; color: #c8d0d8; }
        .lvl { display: inline-block; width: 18dp; height: 6dp; margin-top: 7dp; background-color: #ffffff30; border-radius: 3dp; }
        .fill { height: 6dp; width: 0; background-color: #60e080; border-radius: 3dp; }
        #self { display: block; margin-bottom: 3dp; padding: 3dp 8dp; border-radius: 6dp; background-color: #1d4a2ad0;
                color: #ffffff; font-size: 13dp; }
    </style>
</head>
<body><div id="marks"></div><div id="list"><div id="self" class="hidden">You are talking</div></div></body>
</rml>
)";

std::string px(float v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.1fpx", static_cast<double>(v));
    return b;
}

std::string deg(float radians) {
    char b[32];
    std::snprintf(b, sizeof(b), "rotate(%.1fdeg)", static_cast<double>(glm::degrees(radians)));
    return b;
}

void show(Rml::Element* e, bool on) {
    if (e) e->SetClass("hidden", !on);
}

} // namespace

std::vector<ModuleDependency> VoiceHudModule::dependencies() const {
    return { { std::type_index(typeid(VoiceModule)), true, "who is talking, and where" },
             { std::type_index(typeid(UiModule)), false, "draws the markers and the list" },
             { std::type_index(typeid(DemoPanelModule)), false, "the mute rows in the pause menu" } };
}

void VoiceHudModule::init(Application& app) {
    m_app = &app;
    m_voice = app.getModule<VoiceModule>();
    settings.slots = std::clamp(settings.slots, 1, kMaxSlots);
    if (auto* panel = app.getModule<DemoPanelModule>(); panel && m_voice && !settings.panelSection.empty()) addToPanel(*panel, settings.panelSection);
}

void VoiceHudModule::shutdown() {
    if (m_doc) m_doc->Close();
    m_doc = nullptr;
    m_marksEl = m_listEl = m_selfEl = nullptr;
    m_marks.clear();
    m_arrows.clear();
    m_slotEls = {};
}

void VoiceHudModule::assignSlots(std::array<Slot, kMaxSlots>& slots, const std::vector<VoiceModule::Talker>& talkers, int count, bool frozen) {
    count = std::clamp(count, 0, kMaxSlots);
    std::array<bool, kMaxSlots> seen{};
    std::vector<const VoiceModule::Talker*> fresh;
    for (const VoiceModule::Talker& t : talkers) {
        bool placed = false;
        for (int i = 0; i < count; ++i)
            if (slots[size_t(i)].used && slots[size_t(i)].talker.id == t.id) {
                slots[size_t(i)].talker = t;
                seen[size_t(i)] = true;
                placed = true;
            }
        if (!placed) fresh.push_back(&t); // talkers come nearest first
    }
    for (int i = 0; i < kMaxSlots; ++i) {
        Slot& s = slots[size_t(i)];
        if (i >= count) s = Slot{};
        else if (s.used && !seen[size_t(i)] && !frozen) s = Slot{};
        else if (s.used && !seen[size_t(i)]) s.talker.speaking = false; // gone quiet, kept while the menu is open
    }
    if (frozen) return; // no new faces under the cursor either
    size_t next = 0;
    for (int i = 0; i < count && next < fresh.size(); ++i)
        if (!slots[size_t(i)].used) slots[size_t(i)] = Slot{ true, *fresh[next++] };
}

bool VoiceHudModule::project(const Camera& camera, float aspect, const glm::vec3& point, glm::vec2& ndc) {
    glm::vec3 fwd = camera.target - camera.position;
    if (glm::dot(fwd, fwd) < 1e-12f) return false;
    fwd = glm::normalize(fwd);
    glm::vec3 right = glm::cross(fwd, camera.up);
    if (glm::dot(right, right) < 1e-12f) return false;
    right = glm::normalize(right);
    const glm::vec3 up = glm::cross(right, fwd);
    const glm::vec3 d = point - camera.position;
    const float z = glm::dot(d, fwd);
    if (z < std::max(camera.nearPlane, 0.05f)) return false;
    const float t = std::tan(glm::radians(camera.fovDegrees) * 0.5f);
    ndc.x = glm::dot(d, right) / (z * t * std::max(aspect, 1e-3f));
    ndc.y = -glm::dot(d, up) / (z * t);
    return true;
}

glm::vec2 VoiceHudModule::edgePoint(float azimuth, glm::vec2 size, float inset) {
    const glm::vec2 c = size * 0.5f;
    const glm::vec2 half = glm::max(c - glm::vec2(inset), glm::vec2(1.0f));
    const glm::vec2 dir(std::sin(azimuth), -std::cos(azimuth));
    // Out from the centre along dir until it meets the inset rectangle.
    const float sx = std::fabs(dir.x) > 1e-6f ? half.x / std::fabs(dir.x) : 1e9f;
    const float sy = std::fabs(dir.y) > 1e-6f ? half.y / std::fabs(dir.y) : 1e9f;
    return c + dir * std::min(sx, sy);
}

const char* VoiceHudModule::directionWord(float azimuth) {
    static const char* const kWords[8] = { "ahead", "ahead right", "right", "behind right", "behind", "behind left", "left", "ahead left" };
    const float eighth = glm::two_pi<float>() / 8.0f;
    int i = static_cast<int>(std::lround(std::remainder(azimuth, glm::two_pi<float>()) / eighth));
    i = ((i % 8) + 8) % 8;
    return kWords[i];
}

void VoiceHudModule::addToPanel(DemoPanelModule& panel, const std::string& title) {
    m_panel = &panel;
    DemoPanelModule::Section& s = panel.section(title);
    s.sectionIf([this] {
        auto* net = m_app ? m_app->getModule<NetModule>() : nullptr;
        return m_voice && m_voice->enabled() && net && net->connected();
    });
    for (int i = 0; i < settings.slots; ++i) {
        const size_t k = size_t(i);
        s.toggle(
            "Mute",
            [this, k]() -> bool* {
                if (!m_voice || !m_slots[k].used) return nullptr; // an empty slot: no row
                m_muteShown[k] = m_voice->muted(m_slots[k].talker.id);
                return &m_muteShown[k];
            },
            [this, k] {
                if (m_voice && m_slots[k].used) m_voice->mute(m_slots[k].talker.id, m_muteShown[k]);
            });
        s.labelLive([this, k] {
            if (!m_slots[k].used) return std::string();
            const VoiceModule::Talker& t = m_slots[k].talker;
            std::string where;
            if (t.placed) where = std::to_string(int(std::lround(t.distance))) + " m " + directionWord(t.azimuth);
            return std::to_string(k + 1) + "  Mute " + t.name + (where.empty() ? "" : "  (" + where + ")");
        });
    }
    s.text([this] {
        const bool any = std::any_of(m_slots.begin(), m_slots.end(), [](const Slot& sl) { return sl.used; });
        return any ? std::string() : std::string("Nobody near you is talking.");
    });
    s.showIf([this] { return std::none_of(m_slots.begin(), m_slots.end(), [](const Slot& sl) { return sl.used; }); });
    s.note("Muting is only for you. A muted player's slot stays grey while they talk, so you can unmute them.");
    s.choice(
        "Talk",
        [this]() -> int* {
            if (!m_voice) return nullptr;
            m_talkMode = m_voice->settings.mode == VoiceModule::TalkMode::PushToTalk ? 0 : 1;
            return &m_talkMode;
        },
        { "Hold to talk", "When I speak" },
        [this] {
            if (m_voice) m_voice->settings.mode = m_talkMode == 0 ? VoiceModule::TalkMode::PushToTalk : VoiceModule::TalkMode::VoiceActivated;
        });
    s.toggle("Show where voices come from", &settings.markers);
    if (auto* audio = m_app ? m_app->getModule<AudioModule>() : nullptr) {
        s.toggle(
            "Headphones: sharper direction",
            [this, audio]() -> bool* {
                m_headphones = audio->spatialMode() != SpatialMode::Stereo;
                return &m_headphones;
            },
            [this, audio] { audio->setSpatialMode(m_headphones ? SpatialMode::Binaural : SpatialMode::Stereo); });
    }
}

bool VoiceHudModule::makeDocument() {
    if (m_doc) return true;
    auto* ui = m_app->getModule<UiModule>();
    if (!ui || !ui->context()) {
        if (!m_noUiLogged) log::get(name())->warn("Voice markers need a UiModule (RmlUi): add app.addModule<kke::UiModule>()");
        m_noUiLogged = true;
        return false;
    }
    m_doc = ui->context()->LoadDocumentFromMemory(kHudRml, "voice_hud");
    if (!m_doc) {
        log::get(name())->error("could not build the voice markers document");
        m_noUiLogged = true;
        return false;
    }
    m_marksEl = m_doc->GetElementById("marks");
    m_listEl = m_doc->GetElementById("list");
    m_selfEl = m_doc->GetElementById("self");
    if (!m_marksEl || !m_listEl || !m_selfEl) return false;
    for (int i = 0; i < kMaxSlots; ++i) {
        Rml::ElementPtr row = m_doc->CreateElement("div");
        row->SetClass("slot", true);
        row->SetClass("hidden", true);
        row->SetInnerRML("<span class=\"num\">" + std::to_string(i + 1) +
                         "</span><span class=\"dir\"><div class=\"chev\"></div></span><span class=\"who\"></span><span class=\"info\"></span>"
                         "<span class=\"lvl\"><div class=\"fill\"></div></span>");
        SlotEls& e = m_slotEls[size_t(i)];
        e.row = m_listEl->AppendChild(std::move(row));
        e.chev = e.row->GetChild(1)->GetChild(0);
        e.name = e.row->GetChild(2);
        e.info = e.row->GetChild(3);
        e.fill = e.row->GetChild(4)->GetChild(0);
    }
    m_builtCorner = settings.corner == Corner::BottomLeft ? Corner::TopLeft : Corner::BottomLeft; // placed on the first draw
    m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    return true;
}

Rml::Element* VoiceHudModule::pooled(std::vector<Rml::Element*>& pool, size_t index, const char* cls) {
    while (pool.size() <= index) {
        Rml::ElementPtr e = m_doc->CreateElement("div");
        e->SetClass(cls, true);
        e->SetInnerRML(std::string(cls) == "mark" ? "<div class=\"ring\"></div><div class=\"nm\"></div>" : "<div class=\"chev\"></div><div class=\"nm\"></div>");
        pool.push_back(m_marksEl->AppendChild(std::move(e)));
    }
    return pool[index];
}

void VoiceHudModule::frameStart(const UpdateContext&) {
    if (!m_voice) return;
    auto* net = m_app->getModule<NetModule>();
    const bool online = net && net->connected() && m_voice->enabled();
    const bool frozen = m_panel && m_panel->active();
    const std::vector<VoiceModule::Talker> talkers = online ? m_voice->talkers(settings.holdSeconds) : std::vector<VoiceModule::Talker>{};
    assignSlots(m_slots, talkers, settings.slots, frozen);
    if (!makeDocument()) return;
    draw();
}

void VoiceHudModule::draw() {
    auto* ui = m_app->getModule<UiModule>();
    const float dp = ui ? ui->dpRatio() : 1.0f;
    const Rml::Vector2i ctxSize = m_doc->GetContext()->GetDimensions();
    const glm::vec2 size(float(ctxSize.x), float(ctxSize.y));
    const glm::vec2 frame = ui && ui->frameSize().x > 0.0f ? ui->frameSize() : size;
    const glm::vec2 origin = ui ? ui->frameOrigin() : glm::vec2(0.0f);
    const bool on = visible && m_voice->enabled();

    // Markers: over the heads of the people talking, or at the edge.
    size_t marks = 0, arrows = 0;
    if (on && settings.markers) {
        const float s = dp * settings.markerScale;
        for (const Slot& slot : m_slots) {
            const VoiceModule::Talker& t = slot.talker;
            if (!slot.used || !t.placed || t.muted || t.quietFor > 0.6f) continue;
            const float fade = std::clamp(1.0f - (t.quietFor - 0.3f) / 0.3f, 0.0f, 1.0f); // eases out after the last word
            glm::vec2 ndc;
            const bool inFront = project(m_app->camera(), frame.x / std::max(frame.y, 1.0f), t.mouth + glm::vec3(0.0f, 0.55f, 0.0f), ndc);
            const glm::vec2 at = (ndc * 0.5f + 0.5f) * frame - origin;
            const float margin = 24.0f * dp;
            if (inFront && at.x > margin && at.y > margin && at.x < size.x - margin && at.y < size.y - margin) {
                // Nearer is bigger; louder is a thicker ring.
                const float r = s * std::clamp(16.0f * 6.0f / std::max(t.distance, 1.0f), 9.0f, 22.0f);
                const float w = s * (2.0f + 6.0f * t.level);
                Rml::Element* m = pooled(m_marks, marks++, "mark");
                m->SetClass("hidden", false);
                m->SetProperty("left", px(at.x));
                m->SetProperty("top", px(at.y));
                m->SetProperty("opacity", std::to_string(fade));
                Rml::Element* ring = m->GetChild(0);
                ring->SetProperty("left", px(-r));
                ring->SetProperty("top", px(-r));
                ring->SetProperty("width", px(2.0f * r));
                ring->SetProperty("height", px(2.0f * r));
                ring->SetProperty("border-width", px(w));
                Rml::Element* nm = m->GetChild(1);
                nm->SetProperty("top", px(r + 2.0f * dp));
                const std::string rml = escapeRmlText(t.name);
                if (nm->GetInnerRML() != rml) nm->SetInnerRML(rml);
            } else {
                // Ahead but off the side: the way it went off screen; behind: by the ears' direction.
                const float angle = inFront ? std::atan2(at.x - size.x * 0.5f, size.y * 0.5f - at.y) : t.azimuth;
                const glm::vec2 p = edgePoint(angle, size, settings.edgeInset * std::min(size.x, size.y));
                Rml::Element* a = pooled(m_arrows, arrows++, "arrow");
                a->SetClass("hidden", false);
                a->SetProperty("left", px(p.x));
                a->SetProperty("top", px(p.y));
                a->SetProperty("opacity", std::to_string(fade * (0.6f + 0.4f * t.level)));
                a->GetChild(0)->SetProperty("transform", deg(angle - glm::quarter_pi<float>()));
                Rml::Element* nm = a->GetChild(1);
                // The name on the inside of the arrow, so it stays on screen.
                const glm::vec2 in = glm::normalize(size * 0.5f - p) * 22.0f * dp;
                nm->SetProperty("left", px(in.x));
                nm->SetProperty("top", px(in.y - 8.0f * dp));
                const std::string rml = escapeRmlText(t.name);
                if (nm->GetInnerRML() != rml) nm->SetInnerRML(rml);
            }
        }
    }
    for (size_t i = marks; i < m_marksShown; ++i) m_marks[i]->SetClass("hidden", true);
    for (size_t i = arrows; i < m_arrowsShown; ++i) m_arrows[i]->SetClass("hidden", true);
    m_marksShown = marks;
    m_arrowsShown = arrows;

    // The list.
    if (m_builtCorner != settings.corner) {
        m_builtCorner = settings.corner;
        const bool left = settings.corner == Corner::TopLeft || settings.corner == Corner::BottomLeft;
        const bool top = settings.corner == Corner::TopLeft || settings.corner == Corner::TopRight;
        m_listEl->SetProperty(left ? "left" : "right", "16dp");
        m_listEl->SetProperty(left ? "right" : "left", "auto");
        m_listEl->SetProperty(top ? "top" : "bottom", top ? "16dp" : "96dp");
        m_listEl->SetProperty(top ? "bottom" : "top", "auto");
    }
    show(m_listEl, on && settings.list);
    show(m_selfEl, m_voice->talking());
    for (int i = 0; i < kMaxSlots; ++i) {
        const Slot& slot = m_slots[size_t(i)];
        SlotEls& e = m_slotEls[size_t(i)];
        if (!e.row) continue;
        e.row->SetClass("hidden", !slot.used);
        if (!slot.used) continue;
        const VoiceModule::Talker& t = slot.talker;
        e.row->SetClass("talking", t.speaking && !t.muted);
        e.row->SetClass("muted", t.muted);
        show(e.chev, t.placed);
        if (t.placed) e.chev->SetProperty("transform", deg(t.azimuth - glm::quarter_pi<float>()));
        const std::string nm = escapeRmlText(t.name);
        if (nm != e.shownName) e.name->SetInnerRML(e.shownName = nm);
        const std::string info = t.muted ? "muted" : t.placed ? std::to_string(int(std::lround(t.distance))) + " m" : "radio";
        if (info != e.shownInfo) e.info->SetInnerRML(e.shownInfo = info);
        e.fill->SetProperty("width", px(18.0f * dp * std::clamp(t.level, 0.0f, 1.0f)));
    }
}

} // namespace kke
