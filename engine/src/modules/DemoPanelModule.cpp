#include "kke/modules/DemoPanelModule.h"

#include "kke/Application.h"
#include "kke/DebugUi.h"
#include "kke/DevTools.h"
#include "kke/Log.h"
#include "kke/RmlTextSafety.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/UiModule.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/EventListener.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <typeindex>

namespace kke {

namespace {

// The panel draws itself (no theme file): it has to look the same in
// every game that adds it. Only the panel takes the mouse; the rest of
// the screen stays the game's.
const char* kPanelRml = R"(
<rml>
<head>
    <title>Panel</title>
    <style>
        body { font-family: Noto Sans; color: #e8ecf4; pointer-events: none; width: 100%; height: 100%; }
        div { display: block; }
        #panel { position: absolute; top: 12dp; max-height: 94%; overflow-y: auto; pointer-events: auto;
                 padding: 8dp 10dp 8dp 10dp; border-radius: 10dp; background-color: #0e1322e6; border: 1dp #2f3a5c; }
        #panel.active { border-color: #7a9bff; }
        #title { font-size: 14dp; font-weight: bold; letter-spacing: 1dp; color: #ffffff; padding: 2dp 4dp; cursor: pointer; }
        #hint { font-size: 12dp; color: #aab3cc; padding: 2dp 4dp 4dp 4dp; }
        #hint img { font-size: 12dp; }
        .row { font-size: 13dp; padding: 3dp 6dp; margin-top: 1dp; border-radius: 5dp; color: #cfd6e6; }
        .row.focused { background-color: #2a3a66; color: #ffffff; }
        .row.hidden { display: none; }
        .row .label { display: inline-block; width: 48%; }
        .row .value { display: inline-block; width: 52%; text-align: right; color: #ffffff; }
        .row img { font-size: 13dp; }
        .heading { font-size: 12dp; letter-spacing: 2dp; color: #9fb4e0; margin-top: 8dp; }
        .note { font-size: 12dp; color: #8b93aa; }
        .separator { height: 1dp; padding: 0; margin: 5dp 4dp; background-color: #2f3a5c; }
        .slider, .choice, .toggle, .button { cursor: pointer; }
        .slider:hover, .choice:hover, .toggle:hover, .button:hover { background-color: #1d2742; }
        .slider.focused:hover, .choice.focused:hover, .toggle.focused:hover, .button.focused:hover { background-color: #2a3a66; }
        .bar { height: 6dp; margin: 3dp 0 2dp 0; border-radius: 3dp; background-color: #0b0f1c; border: 1dp #3a4670; }
        .fill { height: 100%; border-radius: 3dp; background-color: #7a9bff; }
        .arrow { color: #56a8ff; }
        .box { display: inline-block; width: 11dp; height: 11dp; margin-right: 6dp; vertical-align: -1dp; border-radius: 3dp;
               border: 2dp #4a5888; background-color: #0b0f1c; }
        .box.on { background-color: #4f7cff; border-color: #cfd9ff; }
        .button { text-align: center; background-color: #28314f; margin-top: 3dp; }
        .hide { color: #8b93aa; }
    </style>
</head>
<body>
    <div id="panel">
        <div id="title"></div>
        <div id="hint"></div>
        <div id="rows"></div>
    </div>
</body>
</rml>
)";

} // namespace

// ---------------------------------------------------------------- Section

DemoPanelModule::Section& DemoPanelModule::Section::add(Row row) {
    m_owner.m_rows.push_back(std::move(row));
    m_rows.push_back(m_owner.m_rows.size() - 1);
    m_owner.m_dirty = true;
    return *this;
}

DemoPanelModule::Section& DemoPanelModule::Section::text(std::string fixed) {
    return text([t = std::move(fixed)] { return t; });
}

DemoPanelModule::Section& DemoPanelModule::Section::text(std::function<std::string()> live) {
    Row r;
    r.kind = Row::Kind::Text;
    r.live = std::move(live);
    return add(std::move(r));
}

DemoPanelModule::Section& DemoPanelModule::Section::hint(std::string keyboardMouse, std::string controller) {
    DemoPanelModule* owner = &m_owner;
    return text([owner, k = std::move(keyboardMouse), c = std::move(controller)] {
        const bool keyboard = !owner->m_input || owner->m_input->promptStyle() == PromptStyle::Keyboard;
        return keyboard ? k : c;
    });
}

DemoPanelModule::Section& DemoPanelModule::Section::note(std::string fixed) {
    Row r;
    r.kind = Row::Kind::Note;
    r.live = [t = std::move(fixed)] { return t; };
    return add(std::move(r));
}

DemoPanelModule::Section& DemoPanelModule::Section::heading(std::string title) {
    Row r;
    r.kind = Row::Kind::Heading;
    r.label = std::move(title);
    return add(std::move(r));
}

DemoPanelModule::Section& DemoPanelModule::Section::separator() {
    Row r;
    r.kind = Row::Kind::Separator;
    return add(std::move(r));
}

DemoPanelModule::Section& DemoPanelModule::Section::slider(std::string label, float* value, float min, float max, std::string format,
                                                           std::function<void()> onChange, float step) {
    return slider(std::move(label), Ref<float>([value] { return value; }), min, max, std::move(format), std::move(onChange), step);
}

DemoPanelModule::Section& DemoPanelModule::Section::slider(std::string label, Ref<float> value, float min, float max, std::string format,
                                                           std::function<void()> onChange, float step) {
    Row r;
    r.kind = Row::Kind::SliderF;
    r.label = std::move(label);
    r.f = std::move(value);
    r.min = min;
    r.max = max;
    r.step = step > 0.0f ? step : (max - min) / 50.0f;
    r.format = std::move(format);
    r.onChange = std::move(onChange);
    return add(std::move(r));
}

DemoPanelModule::Section& DemoPanelModule::Section::slider(std::string label, int* value, int min, int max, std::function<void()> onChange) {
    return slider(std::move(label), Ref<int>([value] { return value; }), min, max, std::move(onChange));
}

DemoPanelModule::Section& DemoPanelModule::Section::slider(std::string label, Ref<int> value, int min, int max, std::function<void()> onChange) {
    Row r;
    r.kind = Row::Kind::SliderI;
    r.label = std::move(label);
    r.i = std::move(value);
    r.min = static_cast<float>(min);
    r.max = static_cast<float>(max);
    r.step = 1.0f;
    r.onChange = std::move(onChange);
    return add(std::move(r));
}

DemoPanelModule::Section& DemoPanelModule::Section::choice(std::string label, int* value, std::vector<std::string> options,
                                                           std::function<void()> onChange) {
    return choice(std::move(label), Ref<int>([value] { return value; }), std::move(options), std::move(onChange));
}

DemoPanelModule::Section& DemoPanelModule::Section::choice(std::string label, Ref<int> value, std::vector<std::string> options,
                                                           std::function<void()> onChange) {
    Row r;
    r.kind = Row::Kind::Choice;
    r.label = std::move(label);
    r.i = std::move(value);
    r.options = std::move(options);
    r.onChange = std::move(onChange);
    return add(std::move(r));
}

DemoPanelModule::Section& DemoPanelModule::Section::toggle(std::string label, bool* value, std::function<void()> onChange) {
    return toggle(std::move(label), Ref<bool>([value] { return value; }), std::move(onChange));
}

DemoPanelModule::Section& DemoPanelModule::Section::toggle(std::string label, Ref<bool> value, std::function<void()> onChange) {
    Row r;
    r.kind = Row::Kind::Toggle;
    r.label = std::move(label);
    r.b = std::move(value);
    r.onChange = std::move(onChange);
    return add(std::move(r));
}

DemoPanelModule::Section& DemoPanelModule::Section::button(std::string label, std::function<void()> onPress) {
    Row r;
    r.kind = Row::Kind::Button;
    r.label = std::move(label);
    r.onChange = std::move(onPress);
    return add(std::move(r));
}

DemoPanelModule::Section& DemoPanelModule::Section::showIf(std::function<bool()> visible) {
    if (!m_rows.empty()) m_owner.m_rows[m_rows.back()].visible = std::move(visible);
    return *this;
}

DemoPanelModule::Section& DemoPanelModule::Section::sectionIf(std::function<bool()> visible) {
    m_visible = std::move(visible);
    return *this;
}

// ---------------------------------------------------------------- clicks

class DemoPanelModule::Listener : public Rml::EventListener {
public:
    explicit Listener(DemoPanelModule& p) : m_panel(p) {}
    void ProcessEvent(Rml::Event& event) override {
        const float x = event.GetParameter<float>("mouse_x", 0.0f);
        m_panel.onClick(event.GetTargetElement(), x, event.GetType() == "mousedown");
    }

private:
    DemoPanelModule& m_panel;
};

// ---------------------------------------------------------------- module

DemoPanelModule::DemoPanelModule(std::string title, Side side) : m_title(std::move(title)), m_side(side) {}
DemoPanelModule::~DemoPanelModule() = default;

std::vector<ModuleDependency> DemoPanelModule::dependencies() const {
    return { { std::type_index(typeid(InputModule)), true, "the panel is driven by actions and shows button prompts" },
             { std::type_index(typeid(UiModule)), true, "draws the panel" } };
}

DemoPanelModule::Section& DemoPanelModule::section(const std::string& title) {
    for (Section& s : m_sections)
        if (s.m_title == title) return s;
    m_sections.push_back(Section(*this, title));
    m_dirty = true;
    return m_sections.back();
}

void DemoPanelModule::init(Application& app) {
    m_app = &app;
    m_input = app.getModule<InputModule>();
    // One action opens and closes it; the rows use the built-in ui.*
    // actions (d-pad / left stick, A, B), remappable like any other.
    for (int p = 0; m_input && p < m_input->players(); ++p) {
        InputMap& m = m_input->map(p);
        if (!m.action("panel.toggle")) m.defineAction({ "panel.toggle", "Settings panel", "Menus", "panel" });
        if (m.bindingsFor("panel.toggle").empty()) {
            m.addBinding(InputModule::bind("panel.toggle", InputModule::pad(SDL_GAMEPAD_BUTTON_BACK)));
            m.addBinding(InputModule::bind("panel.toggle", InputModule::key(SDL_SCANCODE_F3)));
        }
    }
    if (m_devPanelsKey) app.debugUi().setVisible(false); // F1 shows them (onEvent)
    // The last row of every panel.
    Row hide;
    hide.kind = Row::Kind::Button;
    hide.label = "Hide panel";
    hide.onChange = [this] { setState(State::Collapsed); };
    m_rows.push_back(std::move(hide));
    m_hideRow = m_rows.size() - 1;

    auto* ui = app.getModule<UiModule>();
    if (!ui || !ui->context()) return;
    m_doc = ui->context()->LoadDocumentFromMemory(kPanelRml, "demo_panel");
    if (!m_doc) {
        log::get(name())->error("could not build the panel document");
        return;
    }
    m_listener = std::make_unique<Listener>(*this);
    m_doc->AddEventListener("click", m_listener.get());
    m_doc->AddEventListener("mousedown", m_listener.get());
    m_titleEl = m_doc->GetElementById("title");
    m_hint = m_doc->GetElementById("hint");
    m_rowsEl = m_doc->GetElementById("rows");
    m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    m_dirty = true;
}

void DemoPanelModule::shutdown() {
    if (m_doc) {
        if (m_listener) {
            m_doc->RemoveEventListener("click", m_listener.get());
            m_doc->RemoveEventListener("mousedown", m_listener.get());
        }
        m_doc->Close();
        m_doc = nullptr;
    }
    if (m_input && m_state == State::Active) m_input->map(0).setContextEnabled("game", true);
    m_listener.reset();
}

void DemoPanelModule::setTitle(std::string title) {
    m_title = std::move(title);
    m_dirty = true;
}

void DemoPanelModule::setVisible(bool visible) {
    m_visible = visible;
    if (!visible && m_state == State::Active) setState(State::Open);
}

void DemoPanelModule::setState(State s) {
    if (s == m_state) return;
    const bool wasActive = m_state == State::Active;
    m_state = s;
    if (m_input && wasActive != (s == State::Active)) {
        // While the panel has the controls the game doesn't hear them.
        // (No resetStates(): it would make the held toggle button press
        // again on the next frame.)
        m_input->map(0).setContextEnabled("game", s != State::Active);
    }
    if (s == State::Active) {
        // Land on the first row that does something.
        const std::vector<size_t> rows = order();
        if (std::find(rows.begin(), rows.end(), m_focus) == rows.end() || !m_rows[m_focus].focusable() || !rowVisible(m_focus)) {
            m_focus = m_hideRow;
            for (size_t r : rows)
                if (m_rows[r].focusable() && rowVisible(r)) { m_focus = r; break; }
        }
    }
    applyState();
}

std::vector<size_t> DemoPanelModule::order() const {
    std::vector<size_t> out;
    for (const Section& s : m_sections)
        for (size_t r : s.m_rows) out.push_back(r);
    out.push_back(m_hideRow);
    return out;
}

bool DemoPanelModule::rowVisible(size_t index) const {
    const Row& r = m_rows[index];
    if (r.hidden) return false;
    if (r.visible && !r.visible()) return false;
    if (r.f && !r.f()) return false;
    if (r.i && !r.i()) return false;
    if (r.b && !r.b()) return false;
    return true;
}

std::string DemoPanelModule::prompts(const std::string& text) const {
    return m_input ? m_input->promptText(text) : escapeRmlText(text);
}

void DemoPanelModule::build() {
    m_dirty = false;
    if (!m_rowsEl) return;
    for (Row& r : m_rows) {
        r.el = r.value = r.fill = nullptr;
        r.shown.clear();
        r.shownFill = -1.0f;
    }
    std::string rml;
    auto open = [&](size_t i, const char* cls) {
        rml += "<div class=\"row " + std::string(cls) + "\" id=\"r" + std::to_string(i) + "\" data-row=\"" + std::to_string(i) + "\">";
    };
    size_t sectionIndex = 0;
    for (const Section& s : m_sections) {
        // A section heading, unless it repeats the panel's title.
        const bool titled = s.m_title != m_title && !s.m_title.empty();
        if (titled) rml += "<div class=\"row heading\" id=\"s" + std::to_string(sectionIndex) + "\">" + escapeRmlText(s.m_title) + "</div>";
        ++sectionIndex;
        for (size_t i : s.m_rows) {
            const Row& r = m_rows[i];
            const std::string id = std::to_string(i);
            switch (r.kind) {
            case Row::Kind::Heading:
                open(i, "heading");
                rml += escapeRmlText(r.label) + "</div>";
                break;
            case Row::Kind::Text:
                open(i, "text");
                rml += "<span id=\"v" + id + "\"></span></div>";
                break;
            case Row::Kind::Note:
                open(i, "note");
                rml += "<span id=\"v" + id + "\"></span></div>";
                break;
            case Row::Kind::Separator:
                open(i, "separator");
                rml += "</div>";
                break;
            case Row::Kind::SliderF:
            case Row::Kind::SliderI:
                open(i, "slider");
                rml += "<span class=\"label\">" + escapeRmlText(r.label) + "</span><span class=\"value\" id=\"v" + id +
                       "\"></span><div class=\"bar\" id=\"b" + id + "\"><div class=\"fill\" id=\"f" + id + "\"></div></div></div>";
                break;
            case Row::Kind::Choice:
                open(i, "choice");
                rml += "<span class=\"label\">" + escapeRmlText(r.label) + "</span><span class=\"value\"><span class=\"arrow\" data-dir=\"-1\">&lt; </span><span id=\"v" +
                       id + "\"></span><span class=\"arrow\" data-dir=\"1\"> &gt;</span></span></div>";
                break;
            case Row::Kind::Toggle:
                open(i, "toggle");
                rml += "<span class=\"box\" id=\"v" + id + "\"></span>" + escapeRmlText(r.label) + "</div>";
                break;
            case Row::Kind::Button:
                open(i, "button");
                rml += escapeRmlText(r.label) + "</div>";
                break;
            }
        }
    }
    open(m_hideRow, "button hide");
    rml += escapeRmlText(m_rows[m_hideRow].label) + "</div>";
    m_rowsEl->SetInnerRML(rml);
    for (size_t i = 0; i < m_rows.size(); ++i) {
        const std::string id = std::to_string(i);
        m_rows[i].el = m_doc->GetElementById("r" + id);
        m_rows[i].value = m_doc->GetElementById("v" + id);
        m_rows[i].fill = m_doc->GetElementById("f" + id);
    }
    if (m_titleEl) m_titleEl->SetInnerRML(escapeRmlText(m_title));
    if (m_doc) {
        m_doc->GetElementById("panel")->SetProperty("width", std::to_string(static_cast<int>(m_width)) + "dp");
        m_doc->GetElementById("panel")->SetProperty(m_side == Side::Left ? "left" : "right", "12dp");
    }
    m_hintShown.clear();
    applyState();
}

void DemoPanelModule::applyState() {
    if (!m_doc) return;
    Rml::Element* panel = m_doc->GetElementById("panel");
    if (panel) panel->SetClass("active", m_state == State::Active);
    if (m_rowsEl) m_rowsEl->SetProperty("display", m_state == State::Collapsed ? "none" : "block");
    for (size_t i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].el) m_rows[i].el->SetClass("focused", m_state == State::Active && i == m_focus);
    if (m_state == State::Active && m_focus < m_rows.size() && m_rows[m_focus].el) m_rows[m_focus].el->ScrollIntoView(false);
}

float DemoPanelModule::stepValue(float value, float min, float max, float step, int direction) {
    // Snap to the step grid first, so a value typed in elsewhere (0.3333)
    // lands on a round number after one press.
    const float steps = std::round((value - min) / step);
    float v = min + (steps + static_cast<float>(direction)) * step;
    return std::clamp(v, min, max);
}

int DemoPanelModule::cycle(int value, int count, int direction) {
    if (count <= 0) return 0;
    return ((value + direction) % count + count) % count;
}

std::string DemoPanelModule::formatValue(const std::string& format, float value) {
    // One printf-style number in the game's text ("%.1f m/s", "%d %%",
    // "%.0f C"), done by hand: the format is data, so it never reaches
    // snprintf as a format string.
    const std::string f = format.empty() ? "%.2f" : format;
    std::string out;
    bool done = false;
    for (size_t i = 0; i < f.size(); ++i) {
        if (f[i] != '%') {
            out += f[i];
            continue;
        }
        if (i + 1 < f.size() && f[i + 1] == '%') {
            out += '%';
            ++i;
            continue;
        }
        size_t j = i + 1;
        int precision = -1;
        if (j < f.size() && f[j] == '.') {
            precision = 0;
            for (++j; j < f.size() && f[j] >= '0' && f[j] <= '9'; ++j) precision = precision * 10 + (f[j] - '0');
        }
        if (j >= f.size() || done) {
            out += f[i];
            continue;
        }
        const char conv = f[j];
        char buf[48];
        if (conv == 'd' || conv == 'i') {
            std::snprintf(buf, sizeof(buf), "%ld", std::lround(value));
        } else if (conv == 'f' || conv == 'g') {
            std::snprintf(buf, sizeof(buf), "%.*f", precision < 0 ? 2 : std::min(precision, 9), static_cast<double>(value));
        } else {
            out += f[i];
            continue;
        }
        out += buf;
        done = true;
        i = j;
    }
    return out;
}

void DemoPanelModule::change(size_t index, int direction) {
    Row& r = m_rows[index];
    switch (r.kind) {
    case Row::Kind::SliderF:
        if (float* v = r.f ? r.f() : nullptr) {
            const float nv = stepValue(*v, r.min, r.max, r.step, direction);
            if (nv != *v) {
                *v = nv;
                if (r.onChange) r.onChange();
            }
        }
        break;
    case Row::Kind::SliderI:
        if (int* v = r.i ? r.i() : nullptr) {
            const int nv = std::clamp(*v + direction, static_cast<int>(r.min), static_cast<int>(r.max));
            if (nv != *v) {
                *v = nv;
                if (r.onChange) r.onChange();
            }
        }
        break;
    case Row::Kind::Choice:
        if (int* v = r.i ? r.i() : nullptr) {
            *v = cycle(*v, static_cast<int>(r.options.size()), direction);
            if (r.onChange) r.onChange();
        }
        break;
    case Row::Kind::Toggle:
        press(index);
        break;
    default:
        break;
    }
}

void DemoPanelModule::press(size_t index) {
    Row& r = m_rows[index];
    switch (r.kind) {
    case Row::Kind::Toggle:
        if (bool* v = r.b ? r.b() : nullptr) {
            *v = !*v;
            if (r.onChange) r.onChange();
        }
        break;
    case Row::Kind::Button:
        if (r.onChange) r.onChange();
        break;
    case Row::Kind::Choice:
        change(index, 1);
        break;
    default:
        break;
    }
}

void DemoPanelModule::move(int direction) {
    const std::vector<size_t> rows = order();
    std::vector<size_t> focusable;
    for (size_t r : rows)
        if (m_rows[r].focusable() && rowVisible(r)) focusable.push_back(r);
    if (focusable.empty()) return;
    auto it = std::find(focusable.begin(), focusable.end(), m_focus);
    int at = it == focusable.end() ? 0 : static_cast<int>(it - focusable.begin());
    at = cycle(at, static_cast<int>(focusable.size()), direction);
    m_focus = focusable[static_cast<size_t>(at)];
    applyState();
}

void DemoPanelModule::change(int direction) {
    if (m_focus < m_rows.size()) change(m_focus, direction);
}

void DemoPanelModule::press() {
    if (m_focus < m_rows.size()) press(m_focus);
}

void DemoPanelModule::setSliderFromMouse(size_t index, float mouseX) {
    Row& r = m_rows[index];
    Rml::Element* bar = m_doc ? m_doc->GetElementById("b" + std::to_string(index)) : nullptr;
    if (!bar) return;
    const float left = bar->GetAbsoluteLeft() + bar->GetClientLeft();
    const float width = bar->GetClientWidth();
    if (width <= 0.0f) return;
    const float t = std::clamp((mouseX - left) / width, 0.0f, 1.0f);
    if (r.kind == Row::Kind::SliderF) {
        if (float* v = r.f ? r.f() : nullptr) {
            float nv = r.min + t * (r.max - r.min);
            if (r.step > 0.0f) nv = stepValue(nv, r.min, r.max, r.step, 0);
            if (nv != *v) {
                *v = nv;
                if (r.onChange) r.onChange();
            }
        }
    } else if (int* v = r.i ? r.i() : nullptr) {
        const int nv = static_cast<int>(std::lround(r.min + t * (r.max - r.min)));
        if (nv != *v) {
            *v = nv;
            if (r.onChange) r.onChange();
        }
    }
}

void DemoPanelModule::onClick(Rml::Element* target, float mouseX, bool down) {
    if (!target) return;
    if (target->GetId() == "title") {
        if (!down) setState(m_state == State::Collapsed ? State::Open : State::Collapsed);
        return;
    }
    int dir = 0;
    Rml::Element* rowEl = target;
    for (; rowEl && !rowEl->HasAttribute("data-row"); rowEl = rowEl->GetParentNode())
        if (dir == 0 && rowEl->HasAttribute("data-dir")) dir = rowEl->GetAttribute<int>("data-dir", 1);
    if (!rowEl) return;
    const size_t index = static_cast<size_t>(rowEl->GetAttribute<int>("data-row", -1));
    if (index >= m_rows.size()) return;
    const Row& r = m_rows[index];
    if (r.kind == Row::Kind::SliderF || r.kind == Row::Kind::SliderI) {
        if (down) {
            m_dragRow = static_cast<long long>(index);
            setSliderFromMouse(index, mouseX);
        }
        return;
    }
    if (down) return;
    if (r.kind == Row::Kind::Choice) change(index, dir == 0 ? 1 : dir);
    else press(index);
}

void DemoPanelModule::onEvent(const SDL_Event& e) {
    if (e.type == SDL_EVENT_MOUSE_MOTION && m_dragRow >= 0) {
        const float ppp = m_app ? m_app->window().pixelsPerPoint() : 1.0f;
        setSliderFromMouse(static_cast<size_t>(m_dragRow), e.motion.x * ppp);
    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        m_dragRow = -1;
    }
    // Developer builds only: shipping builds never show the ImGui panels.
    if (dev::kEnabled && m_devPanelsKey && m_app && e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.key == SDLK_F1)
        m_app->debugUi().setVisible(!m_app->debugUi().visible());
    if (e.type != SDL_EVENT_KEY_DOWN || m_state != State::Active || !m_visible) return;
    // The keyboard reaches the panel directly (ui.* are pad-only by
    // default, see InputModule::defineUiActions); key repeat repeats.
    switch (e.key.key) {
    case SDLK_UP: move(-1); break;
    case SDLK_DOWN: move(1); break;
    case SDLK_LEFT: change(-1); break;
    case SDLK_RIGHT: change(1); break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (!e.key.repeat) press();
        break;
    case SDLK_ESCAPE:
        if (!e.key.repeat) setState(State::Open);
        break;
    default: break;
    }
}

void DemoPanelModule::frameStart(const UpdateContext& ctx) {
    // frameStart runs while the game is paused too, so the panel works then.
    input(ctx.dt);
    draw();
}

void DemoPanelModule::input(float dt) {
    if (!m_input || !m_visible || !uiVisible()) return;
    const InputMap& m = m_input->map(0);
    if (m.pressed("panel.toggle")) {
        setState(m_state == State::Active ? State::Open : State::Active);
        return;
    }
    if (m_state != State::Active) return;
    if (m.pressed("ui.back")) {
        setState(State::Open);
        return;
    }
    if (m.pressed("ui.accept")) press();
    // Directions: once on the press, then repeating while held (faster
    // for left/right, so a slider sweeps).
    auto repeat = [&](const char* action, float& held, float& at, float first, float every) {
        if (m.pressed(action)) {
            held = 0.0f;
            at = first;
            return true;
        }
        if (!m.held(action)) return false;
        held += dt;
        if (held < at) return false;
        at += every;
        return true;
    };
    if (repeat("ui.up", m_vHeld[0], m_vRepeatAt[0], 0.4f, 0.12f)) move(-1);
    if (repeat("ui.down", m_vHeld[1], m_vRepeatAt[1], 0.4f, 0.12f)) move(1);
    if (repeat("ui.left", m_held[0], m_repeatAt[0], 0.35f, 0.05f)) change(-1);
    if (repeat("ui.right", m_held[1], m_repeatAt[1], 0.35f, 0.05f)) change(1);
}

void DemoPanelModule::draw() {
    if (!m_doc) return;
    // KKE_HIDE_UI (clean screenshots) hides it like any module's panels.
    const bool show = m_visible && uiVisible();
    if (show != m_docShown) {
        m_docShown = show;
        m_doc->SetProperty("visibility", show ? "visible" : "hidden");
    }
    if (m_dirty) build();
    if (show) refresh();
}

void DemoPanelModule::refresh() {
    // The hint under the title: how to reach the panel from the device
    // the player is holding.
    const bool keyboard = !m_input || m_input->promptStyle() == PromptStyle::Keyboard;
    std::string hint;
    if (m_state == State::Active)
        hint = keyboard ? "{key:Up}{key:Down} choose  {key:Left}{key:Right} change  {key:Enter} press  {key:Escape} back to the game"
                        : "{ui.up}{ui.down} choose  {ui.left}{ui.right} change  {ui.accept} press  {ui.back} back to the game";
    else if (m_state == State::Collapsed)
        hint = "{panel.toggle} open settings";
    else
        hint = keyboard ? "{panel.toggle} use with the keyboard, or click" : "{panel.toggle} change settings";
    hint = prompts(hint);
    if (hint != m_hintShown && m_hint) {
        m_hintShown = hint;
        m_hint->SetInnerRML(hint);
    }
    if (m_state == State::Collapsed) return;

    // Sections that come and go.
    size_t sectionIndex = 0;
    for (Section& s : m_sections) {
        const bool show = !s.m_visible || s.m_visible();
        for (size_t i : s.m_rows) m_rows[i].hidden = !show;
        if (Rml::Element* h = m_doc->GetElementById("s" + std::to_string(sectionIndex))) h->SetClass("hidden", !show);
        ++sectionIndex;
    }
    bool focusLost = false;
    for (size_t i = 0; i < m_rows.size(); ++i) {
        Row& r = m_rows[i];
        if (!r.el) continue;
        const bool vis = rowVisible(i);
        r.el->SetClass("hidden", !vis);
        if (!vis) {
            focusLost |= i == m_focus;
            continue;
        }
        std::string shown;
        float fill = -1.0f;
        switch (r.kind) {
        case Row::Kind::Text:
        case Row::Kind::Note:
            shown = prompts(r.live ? r.live() : std::string());
            break;
        case Row::Kind::SliderF: {
            const float v = *r.f();
            shown = escapeRmlText(formatValue(r.format, v));
            fill = r.max > r.min ? (v - r.min) / (r.max - r.min) : 0.0f;
            break;
        }
        case Row::Kind::SliderI: {
            const int v = *r.i();
            shown = std::to_string(v);
            fill = r.max > r.min ? (static_cast<float>(v) - r.min) / (r.max - r.min) : 0.0f;
            break;
        }
        case Row::Kind::Choice: {
            const int v = *r.i();
            shown = v >= 0 && v < static_cast<int>(r.options.size()) ? escapeRmlText(r.options[static_cast<size_t>(v)]) : "-";
            break;
        }
        case Row::Kind::Toggle:
            shown = *r.b() ? "on" : "off";
            break;
        default:
            continue;
        }
        if (r.value && shown != r.shown) {
            r.shown = shown;
            if (r.kind == Row::Kind::Toggle) r.value->SetClass("on", shown == "on");
            else r.value->SetInnerRML(shown);
        }
        if (r.fill && fill >= 0.0f && std::abs(fill - r.shownFill) > 1e-4f) {
            r.shownFill = fill;
            char pct[32];
            std::snprintf(pct, sizeof(pct), "%.2f%%", static_cast<double>(std::clamp(fill, 0.0f, 1.0f) * 100.0f));
            r.fill->SetProperty("width", pct);
        }
    }
    if (focusLost && m_state == State::Active) move(1);
}

} // namespace kke
