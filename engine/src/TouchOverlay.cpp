#include "TouchOverlay.h"

#include "kke/InputMap.h"
#include "kke/RmlTextSafety.h"
#include "kke/modules/InputModule.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/EventListener.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace kke {

namespace {

// Round controls in the corners, the edit bar along the top. Nothing but
// the bar takes the pointer: the fingers on a control are InputModule's.
const char* kOverlayRml = R"(
<rml>
<head>
    <title>Touch controls</title>
    <style>
        body { font-family: Noto Sans; color: #ffffff; pointer-events: none; width: 100%; height: 100%; }
        div { display: block; }
        #root { position: absolute; left: 0; top: 0; width: 100%; height: 100%; }
        .tc { position: absolute; display: flex; align-items: center; justify-content: center; border-width: 3dp; border-color: #ffffffb0;
              background-color: #0b0f1c70; text-align: center; font-weight: bold; color: #ffffff;
              font-effect: outline(1dp #000000c0); }
        .tc.held { background-color: #ffffffb0; border-color: #ffffff; color: #0b0f1c; }
        .tc.on { background-color: #ffcf5c90; border-color: #ffcf5c; }
        .tc.stick { background-color: #0b0f1c50; }
        .tc.pause { background-color: #0b0f1c90; }
        .tc.selected { border-color: #ffcf5c; border-width: 5dp; }
        .knob { position: absolute; background-color: #ffffffa0; border-width: 2dp; border-color: #ffffff; }
        .tc .label { width: 88%; }
        .c0 { border-bottom-color: #2e9e5b; } .c1 { border-bottom-color: #c9405a; } .c2 { border-bottom-color: #2f6fd6; }
        .c3 { border-bottom-color: #d9a52b; } .c4 { border-bottom-color: #8a4fd0; } .c5 { border-bottom-color: #d9772b; }
        #look { position: absolute; right: 4%; top: 40%; width: 36%; text-align: center; font-size: 16dp; color: #ffffffd0;
                padding: 10dp; border-radius: 10dp; background-color: #0b0f1c90; display: none; }
        #bar { position: absolute; left: 4%; right: 12%; top: 1%; padding: 8dp 10dp; border-radius: 12dp;
               background-color: #0b0f1cf0; border-top: 3dp #56a8ff; pointer-events: auto; text-align: center; display: none; }
        #info { font-size: 15dp; color: #eef1f8; margin-bottom: 6dp; }
        #info .hint { color: #aab3cc; font-size: 13dp; }
        #buttons { display: flex; flex-wrap: wrap; justify-content: center; }
        .cmd { display: block; min-width: 44dp; height: 26dp; line-height: 26dp; margin: 3dp; padding: 6dp 10dp; border-radius: 8dp;
               font-size: 15dp; background-color: #2a3a66; color: #ffffff; cursor: pointer; pointer-events: auto; }
        .cmd:hover { background-color: #3a4f88; }
        .cmd.done { background-color: #2f8a4c; font-weight: bold; }
        .cmd.off { background-color: #1a2138; color: #6b7590; }
    </style>
</head>
<body>
    <div id="root">
        <div id="controls"></div>
        <div id="look">Look: drag anywhere free</div>
    </div>
    <div id="bar">
        <div id="info"></div>
        <div id="buttons">
            <div class="cmd done" data-kke-touch-ui="done">Done</div>
            <div class="cmd" data-kke-touch-ui="smaller">Smaller</div>
            <div class="cmd" data-kke-touch-ui="bigger">Bigger</div>
            <div class="cmd" data-kke-touch-ui="prev">&lt; Action</div>
            <div class="cmd" data-kke-touch-ui="next">Action &gt;</div>
            <div class="cmd" data-kke-touch-ui="latch">Hold / Toggle</div>
            <div class="cmd" data-kke-touch-ui="remove">Remove</div>
            <div class="cmd" data-kke-touch-ui="add">Add button</div>
            <div class="cmd" data-kke-touch-ui="fade-">Fainter</div>
            <div class="cmd" data-kke-touch-ui="fade+">Clearer</div>
            <div class="cmd" data-kke-touch-ui="look-">Look slower</div>
            <div class="cmd" data-kke-touch-ui="look+">Look faster</div>
            <div class="cmd" data-kke-touch-ui="reset">Reset</div>
        </div>
    </div>
</body>
</rml>
)";

std::string px(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1fpx", static_cast<double>(v));
    return buf;
}

} // namespace

class TouchOverlay::Listener : public Rml::EventListener {
public:
    explicit Listener(TouchOverlay& o) : m_overlay(o) {}
    void ProcessEvent(Rml::Event& event) override {
        for (Rml::Element* e = event.GetTargetElement(); e; e = e->GetParentNode())
            if (e->HasAttribute("data-kke-touch-ui")) {
                m_overlay.command(e->GetAttribute<Rml::String>("data-kke-touch-ui", ""));
                return;
            }
    }

private:
    TouchOverlay& m_overlay;
};

TouchOverlay::TouchOverlay(Rml::Context* context, InputModule* input) : m_context(context), m_input(input) {
    if (!m_context || !m_input) return;
    m_doc = m_context->LoadDocumentFromMemory(kOverlayRml, "touch_controls");
    if (!m_doc) return;
    m_root = m_doc->GetElementById("root");
    m_controls = m_doc->GetElementById("controls");
    m_bar = m_doc->GetElementById("bar");
    m_info = m_doc->GetElementById("info");
    m_lookHint = m_doc->GetElementById("look");
    m_listener = std::make_unique<Listener>(*this);
    m_bar->AddEventListener("click", m_listener.get());
    m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    m_doc->SetProperty("visibility", "hidden");
}

TouchOverlay::~TouchOverlay() {
    if (m_doc) {
        if (m_bar && m_listener) m_bar->RemoveEventListener("click", m_listener.get());
        m_doc->Close();
    }
}

std::string TouchOverlay::labelOf(const TouchControl& c, bool keepColon) const {
    if (c.kind == TouchControl::Kind::Pause) return "II";
    if (c.kind != TouchControl::Kind::Button) return {};
    if (!c.label.empty()) return c.label;
    const ActionDef* a = m_input->map(0).action(c.action);
    std::string l = a && !a->label.empty() ? a->label : c.action;
    // "Interact / push" -> "Interact", "Look (stick)" -> "Look": a thumb's
    // worth of words. "Left hand: reach" keeps its colon when "Left hand"
    // alone would name two buttons.
    for (const char* cut : { " / ", " (", " - ", ": " }) {
        if (keepColon && std::string_view(cut) == ": ") continue;
        if (const size_t at = l.find(cut); at != std::string::npos && at > 0) l.resize(at);
    }
    return l;
}

std::vector<std::string> TouchOverlay::labelsOf(const std::vector<TouchControl>& layout) const {
    std::vector<std::string> out;
    for (const TouchControl& c : layout) out.push_back(labelOf(c));
    const std::vector<std::string> shortOnes = out;
    for (size_t i = 0; i < layout.size(); ++i)
        if (!out[i].empty() && std::count(shortOnes.begin(), shortOnes.end(), shortOnes[i]) > 1) out[i] = labelOf(layout[i], true);
    return out;
}

void TouchOverlay::rebuild() {
    const TouchControls& t = m_input->touch();
    m_built = t.layout();
    m_builtLabels.clear();
    std::string rml;
    m_builtLabels = labelsOf(m_built);
    for (size_t i = 0; i < m_built.size(); ++i) {
        const TouchControl& c = m_built[i];
        const std::string& label = m_builtLabels[i];
        if (c.kind == TouchControl::Kind::Look) {
            rml += "<div class=\"look-slot\"></div>";
            continue;
        }
        const char* kind = c.kind == TouchControl::Kind::Stick ? "stick" : c.kind == TouchControl::Kind::Pause ? "pause" : "button";
        rml += std::string("<div class=\"tc ") + kind + " c" + std::to_string(i % 6) + "\">";
        if (c.kind == TouchControl::Kind::Stick) rml += "<div class=\"knob\"></div>";
        else rml += "<div class=\"label\">" + escapeRmlText(label) + "</div>";
        rml += "</div>";
    }
    m_controls->SetInnerRML(rml);
    m_elements.assign(m_built.size(), nullptr);
    m_knobs.assign(m_built.size(), nullptr);
    for (int i = 0; i < m_controls->GetNumChildren() && i < static_cast<int>(m_built.size()); ++i) {
        Rml::Element* e = m_controls->GetChild(i);
        m_elements[static_cast<size_t>(i)] = e;
        if (m_built[static_cast<size_t>(i)].kind == TouchControl::Kind::Stick && e->GetNumChildren() > 0) m_knobs[static_cast<size_t>(i)] = e->GetChild(0);
    }
}

void TouchOverlay::update(float originX, float originY) {
    if (!m_doc || !m_input) return;
    const bool shown = m_input->touchShown();
    if (shown != m_shown) {
        m_shown = shown;
        m_doc->SetProperty("visibility", shown ? "visible" : "hidden");
    }
    if (!shown) return;
    // Over the game's HUD: what a finger sees is what it presses (menus
    // hide the controls while they have the screen).
    m_doc->PullToFront();

    const TouchControls& t = m_input->touch();
    if (t.layout().size() != m_built.size() || labelsOf(t.layout()) != m_builtLabels) rebuild();
    for (size_t i = 0; i < t.layout().size() && i < m_built.size(); ++i) {
        const TouchControl& c = t.layout()[i];
        m_built[i] = c;
        Rml::Element* e = m_elements[i];
        if (!e || c.kind == TouchControl::Kind::Look) continue;
        const glm::vec2 centre = t.centre(c) - glm::vec2(originX, originY);
        const float r = t.radius(c);
        e->SetProperty("left", px(centre.x - r));
        e->SetProperty("top", px(centre.y - r));
        e->SetProperty("width", px(2.0f * r));
        e->SetProperty("height", px(2.0f * r));
        e->SetProperty("border-radius", px(r));
        e->SetClass("held", t.held(static_cast<int>(i)) && !c.latch);
        e->SetClass("on", t.held(static_cast<int>(i)) && c.latch);
        e->SetClass("selected", t.editing() && t.selected() == static_cast<int>(i));
        if (c.kind == TouchControl::Kind::Stick) {
            if (Rml::Element* k = m_knobs[i]) {
                const float kr = r * 0.42f;
                const glm::vec2 v = t.stick(c.stick) * (r - kr);
                k->SetProperty("left", px(r + v.x - kr - 3.0f));
                k->SetProperty("top", px(r + v.y - kr - 3.0f));
                k->SetProperty("width", px(2.0f * kr));
                k->SetProperty("height", px(2.0f * kr));
                k->SetProperty("border-radius", px(kr));
            }
        } else if (Rml::Element* label = e->GetNumChildren() > 0 ? e->GetChild(0) : nullptr) {
            // Text sized to the button: bigger buttons, bigger words.
            const size_t letters = m_builtLabels[i].size();
            const float scale = c.kind == TouchControl::Kind::Pause ? 0.8f : letters > 8 ? 0.30f : 0.40f;
            label->SetProperty("font-size", px(std::clamp(r * scale, 9.0f, 40.0f)));
        }
    }
    char opacity[16];
    std::snprintf(opacity, sizeof(opacity), "%.2f", static_cast<double>(t.editing() ? std::max(0.75f, t.opacity) : t.opacity));
    m_root->SetProperty("opacity", opacity);

    // The edit bar.
    const bool editing = t.editing();
    if (editing != m_barShown) {
        m_barShown = editing;
        m_bar->SetProperty("display", editing ? "block" : "none");
        bool look = false;
        for (const TouchControl& c : t.layout()) look = look || c.kind == TouchControl::Kind::Look;
        m_lookHint->SetProperty("display", editing && look ? "block" : "none");
    }
    if (editing) {
        std::string info;
        const int sel = t.selected();
        if (sel >= 0 && sel < static_cast<int>(t.layout().size())) {
            const TouchControl& c = t.layout()[static_cast<size_t>(sel)];
            switch (c.kind) {
            case TouchControl::Kind::Stick: info = c.stick == 1 ? "Right stick" : "Left stick"; break;
            case TouchControl::Kind::Pause: info = "Pause button"; break;
            case TouchControl::Kind::Look: info = "Look"; break;
            case TouchControl::Kind::Button: {
                const ActionDef* a = m_input->map(0).action(c.action);
                info = "Button: " + (a && !a->label.empty() ? a->label : c.action) + (c.latch ? "  ·  tap on, tap off" : "  ·  held while touched");
                break;
            }
            }
        } else {
            info = "Drag a control to move it, tap one to change it";
        }
        char extra[96];
        std::snprintf(extra, sizeof(extra), "  ·  look speed %.1f  ·  %d%% clear", static_cast<double>(t.lookSpeed),
                      static_cast<int>(std::lround(t.opacity * 100.0f)));
        info = "<span>" + escapeRmlText(info) + "</span><span class=\"hint\">" + escapeRmlText(extra) + "</span>";
        if (info != m_infoShown) {
            m_infoShown = info;
            m_info->SetInnerRML(info);
        }
    }
}

void TouchOverlay::command(const std::string& what) {
    TouchControls& t = m_input->touch();
    const int sel = t.selected();
    const TouchControl* c = sel >= 0 && sel < static_cast<int>(t.layout().size()) ? &t.layout()[static_cast<size_t>(sel)] : nullptr;
    if (what == "done") {
        m_input->editTouch(false);
    } else if (what == "reset") {
        t.resetToDefaults();
    } else if (what == "smaller" || what == "bigger") {
        t.resize(sel, what == "bigger" ? 1.15f : 1.0f / 1.15f);
    } else if ((what == "prev" || what == "next") && c) {
        if (c->kind == TouchControl::Kind::Stick) {
            std::vector<TouchControl> l = t.layout();
            l[static_cast<size_t>(sel)].stick = 1 - c->stick; // left <-> right
            t.setLayout(std::move(l));
            t.select(sel);
        } else if (c->kind == TouchControl::Kind::Button) {
            const std::vector<std::string> actions = TouchControls::buttonActions(m_input->map(0));
            if (actions.empty()) return;
            auto it = std::find(actions.begin(), actions.end(), c->action);
            int i = it == actions.end() ? 0 : static_cast<int>(it - actions.begin());
            i = (i + (what == "next" ? 1 : -1) + static_cast<int>(actions.size())) % static_cast<int>(actions.size());
            t.setAction(sel, actions[static_cast<size_t>(i)]);
        }
    } else if (what == "latch" && c && c->kind == TouchControl::Kind::Button) {
        t.setLatch(sel, !c->latch);
    } else if (what == "remove" && c) {
        t.remove(sel);
    } else if (what == "add") {
        // The first action no button has yet.
        const std::vector<std::string> actions = TouchControls::buttonActions(m_input->map(0));
        std::string pick = actions.empty() ? std::string() : actions.front();
        for (const std::string& a : actions)
            if (std::none_of(t.layout().begin(), t.layout().end(), [&](const TouchControl& b) { return b.action == a; })) {
                pick = a;
                break;
            }
        if (!pick.empty()) t.addButton(pick);
    } else if (what == "fade-" || what == "fade+") {
        t.opacity = std::clamp(t.opacity + (what == "fade+" ? 0.1f : -0.1f), 0.15f, 1.0f);
    } else if (what == "look-" || what == "look+") {
        t.lookSpeed = std::clamp(t.lookSpeed * (what == "look+" ? 1.25f : 0.8f), 0.1f, 5.0f);
    }
}

} // namespace kke
