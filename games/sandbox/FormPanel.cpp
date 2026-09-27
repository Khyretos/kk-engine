#include "FormPanel.h"

#include "kke/Log.h"
#include "kke/RmlTextSafety.h"
#include "kke/modules/DemoPanelModule.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/EventListener.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace kke_sandbox {

namespace {

// The dark look of DemoPanelModule's panels, with bigger targets: a
// gamepad's cursor has to land on them.
const char* kDocument = R"RML(<rml>
<head>
<title>Build</title>
<style>
body { font-family: Noto Sans; color: #e8ecf4; pointer-events: none; width: 100%; height: 100%; }
div { display: block; }
#panel { position: absolute; max-height: 94%; overflow-y: auto; pointer-events: auto;
         padding: 6dp 10dp 8dp 10dp; border-radius: 10dp; background-color: #0e1322eb; border: 1dp #2f3a5c; font-size: 13dp; }
/* The panel scrolls when taller than the screen; RmlUi has no default
   scrollbar, and without a size the rows are laid out a word per line. */
scrollbarvertical { width: 8dp; }
scrollbarvertical slidertrack { background-color: #00000000; }
scrollbarvertical sliderbar { background-color: #3a4670; border-radius: 4dp; min-height: 24dp; }
scrollbarvertical sliderbar:hover { background-color: #5b6ca8; }
scrollbarvertical sliderarrowdec, scrollbarvertical sliderarrowinc { height: 0; }
.section { font-size: 12dp; letter-spacing: 2dp; color: #9fb4e0; margin-top: 8dp; padding: 3dp 4dp; border-radius: 5dp;
           cursor: pointer; background-color: #161d33; }
.section:hover { background-color: #1d2742; }
.fold { display: inline-block; width: 14dp; color: #56a8ff; }
.text { padding: 2dp 4dp; color: #cfd6e6; }
.text.muted { color: #8b93aa; font-size: 12dp; }
.text.good { color: #8cff8c; }
.text.warn { color: #ffb070; }
.button { display: inline-block; padding: 4dp 10dp; margin: 3dp 4dp 1dp 0; border-radius: 5dp; background-color: #28314f;
          cursor: pointer; color: #ffffff; }
.button:hover { background-color: #34406a; }
.button:active { background-color: #4f7cff; }
.button.disabled { color: #5d6580; background-color: #1a2038; }
.button.key { width: 22dp; padding: 5dp 0; margin: 2dp 2dp 0 0; text-align: center; }
.toggle { padding: 3dp 4dp; cursor: pointer; border-radius: 5dp; }
.toggle:hover { background-color: #1d2742; }
.box { display: inline-block; width: 12dp; height: 12dp; margin-right: 7dp; vertical-align: -2dp; border-radius: 3dp;
       border: 2dp #4a5888; background-color: #0b0f1c; }
.box.on { background-color: #4f7cff; border-color: #cfd9ff; }
.line { padding: 3dp 4dp 1dp 4dp; }
.label { display: inline-block; width: 44%; color: #cfd6e6; }
.ctl { display: inline-block; width: 56%; text-align: right; white-space: nowrap; }
.val { display: inline-block; color: #ffffff; padding: 0 4dp; }
.val.num { min-width: 64dp; text-align: center; }
.val.press { cursor: pointer; }
.step { display: inline-block; width: 20dp; text-align: center; border-radius: 4dp; background-color: #28314f; cursor: pointer;
        color: #9fc6ff; font-weight: bold; }
.step:hover { background-color: #34406a; }
.step:active { background-color: #4f7cff; color: #ffffff; }
.sl { position: relative; height: 18dp; margin: 1dp 4dp 3dp 4dp; }
.sl .step { position: absolute; top: 0; }
.sl .left { left: 0; }
.sl .right { right: 0; }
.bar { position: absolute; left: 24dp; right: 24dp; top: 2dp; height: 14dp; border-radius: 4dp; background-color: #0b0f1c;
       border: 1dp #3a4670; cursor: pointer; }
.fill { height: 100%; border-radius: 3dp; background-color: #7a9bff; }
.r .fill { background-color: #ff6a6a; }
.g .fill { background-color: #6aff8a; }
.b .fill { background-color: #6a9bff; }
.swatch { display: inline-block; width: 44dp; height: 14dp; border-radius: 3dp; border: 1dp #cfd6e6; vertical-align: -2dp; }
input.field { display: inline-block; width: 56%; padding: 2dp 5dp; border-radius: 4dp; background-color: #0b0f1c;
              border: 1dp #3a4670; color: #ffffff; cursor: text; }
input.field:focus { border-color: #7a9bff; }
.tile { display: inline-block; width: 88dp; margin: 4dp 3dp 0 0; vertical-align: top; cursor: pointer; }
.pic { width: 88dp; height: 88dp; border-radius: 8dp; background-color: #1a2140; border: 2dp #262f52; text-align: center; }
.tile:hover .pic { border-color: #56a8ff; }
.tile.on .pic { border-color: #8cff8c; background-color: #243a2c; }
.pic img { width: 84dp; height: 84dp; }
.word { display: block; line-height: 84dp; color: #8b93aa; font-size: 12dp; }
.name { font-size: 11dp; color: #cfd6e6; height: 16dp; overflow: hidden; white-space: nowrap; text-align: center; }
</style>
</head>
<body><div id="panel"><div id="rows"></div></div></body>
</rml>)RML";

std::string esc(const std::string& s) { return kke::escapeRmlText(s); }

std::string hex(const glm::vec3& c) {
    char buf[8];
    const auto b = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", b(c.r), b(c.g), b(c.b));
    return buf;
}

} // namespace

class FormPanelListener : public Rml::EventListener {
public:
    explicit FormPanelListener(FormPanel& p) : m_panel(p) {}
    void ProcessEvent(Rml::Event& event) override {
        Rml::Element* e = event.GetTargetElement();
        for (; e && !e->HasAttribute("data-part"); e = e->GetParentNode()) {}
        if (!e) return;
        const size_t row = static_cast<size_t>(e->GetAttribute<int>("data-row", 0));
        const Rml::String part = e->GetAttribute<Rml::String>("data-part", "");
        if (event.GetId() == Rml::EventId::Change) {
            if (part == "text") m_panel.typed(row, event.GetParameter<Rml::String>("value", ""));
            return;
        }
        if (event.GetParameter<int>("button", 0) != 0) return;
        const FormPanel::Part p = part == "minus" ? FormPanel::Part::Minus
                                  : part == "plus" ? FormPanel::Part::Plus
                                  : part == "bar"  ? FormPanel::Part::Bar
                                                   : FormPanel::Part::Press;
        if (part == "text") return; // a click in a field only gives it the keyboard
        m_panel.pressed(row, p, e->GetAttribute<int>("data-sub", 0), event.GetParameter<float>("mouse_x", 0.0f));
    }

private:
    FormPanel& m_panel;
};

FormPanel::FormPanel(std::string name, std::string style) : m_name(std::move(name)), m_style(std::move(style)) {}

FormPanel::~FormPanel() { detach(); }

void FormPanel::attach(Rml::Context* context, float pixelsPerPoint) {
    m_context = context;
    m_ppp = pixelsPerPoint > 0.0f ? pixelsPerPoint : 1.0f;
    if (!m_context || m_doc) return;
    m_doc = m_context->LoadDocumentFromMemory(kDocument, "build-" + m_name);
    if (!m_doc) {
        kke::log::get("Sandbox")->warn("the {} panel's document didn't load", m_name);
        return;
    }
    m_doc->GetElementById("panel")->SetAttribute("style", m_style);
    m_listener = std::make_unique<FormPanelListener>(*this);
    m_doc->AddEventListener(Rml::EventId::Mousedown, m_listener.get());
    m_doc->AddEventListener(Rml::EventId::Change, m_listener.get());
    for (Row& r : m_rows) r.builtSig.clear(); // build on the next end()
    if (m_visible) m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    else m_doc->Hide();
}

void FormPanel::detach() {
    if (m_doc) {
        m_doc->RemoveEventListener(Rml::EventId::Mousedown, m_listener.get());
        m_doc->RemoveEventListener(Rml::EventId::Change, m_listener.get());
        m_doc->Close();
        m_doc = nullptr;
    }
    m_listener.reset();
    m_context = nullptr;
    for (Row& r : m_rows) {
        r.builtSig.clear();
        r.valueEl = r.stateEl = nullptr;
        r.fillEl[0] = r.fillEl[1] = r.fillEl[2] = nullptr;
    }
}

void FormPanel::setVisible(bool visible) {
    if (visible == m_visible) return;
    m_visible = visible;
    if (!m_doc) return;
    if (visible) {
        m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    } else {
        if (Rml::Element* focus = m_context->GetFocusElement(); focus && focus->GetOwnerDocument() == m_doc) focus->Blur();
        m_doc->Hide();
    }
}

// ---------------------------------------------------------------- frame

void FormPanel::begin(double now, glm::vec2 mouse, bool mouseDown) {
    m_now = now;
    m_mouse = mouse;
    m_count = 0;
    if (!mouseDown) {
        m_dragRow = -1;
        m_holdRow = -1;
    }
    if (m_dragRow >= 0) {
        Event e;
        e.row = static_cast<size_t>(m_dragRow);
        e.part = Part::Bar;
        e.sub = m_dragSub;
        e.fraction = barFraction(e.row, m_dragSub, mouse.x * m_ppp);
        m_events.push_back(e);
    }
    if (m_holdRow >= 0 && now >= m_holdNext) {
        Event e;
        e.row = static_cast<size_t>(m_holdRow);
        e.part = m_holdPart;
        m_events.push_back(e);
        m_holdNext = now + 0.06;
    }
}

void FormPanel::end() {
    m_rows.resize(m_count);
    bool rebuild = false;
    for (const Row& r : m_rows) rebuild |= r.sig != r.builtSig;
    if (m_doc && rebuild) build();
    else if (m_doc) refresh();
    m_events.clear();
}

size_t FormPanel::add(Kind kind, std::string sig) {
    const size_t i = m_count++;
    if (i >= m_rows.size()) m_rows.emplace_back();
    Row& r = m_rows[i];
    r.kind = kind;
    r.sig = std::to_string(static_cast<int>(kind)) + ':' + sig;
    return i;
}

std::vector<FormPanel::Event> FormPanel::take(size_t row) {
    std::vector<Event> out;
    if (m_rows[row].sig != m_rows[row].builtSig) return out; // pressed on a row that isn't this one any more
    for (const Event& e : m_events)
        if (e.row == row) out.push_back(e);
    return out;
}

// ---------------------------------------------------------------- rows

bool FormPanel::section(const std::string& title, bool openByDefault) {
    if (m_seenSections.insert(title).second && !openByDefault) m_folded.insert(title);
    const size_t i = add(Kind::Section, title);
    for (const Event& e : take(i)) {
        if (!e.start) continue;
        if (!m_folded.erase(title)) m_folded.insert(title);
    }
    const bool open = !m_folded.count(title);
    Row& r = m_rows[i];
    r.label = title;
    r.value = open ? "-" : "+";
    return open;
}

void FormPanel::text(const std::string& text, Tone tone) {
    const size_t i = add(Kind::Text, std::to_string(static_cast<int>(tone)));
    Row& r = m_rows[i];
    r.tone = tone;
    r.value = text;
}

bool FormPanel::button(const std::string& label, bool enabled) {
    const size_t i = add(Kind::Button, label);
    bool pressed = false;
    for (const Event& e : take(i)) pressed |= e.start && enabled;
    Row& r = m_rows[i];
    r.label = label;
    r.enabled = enabled;
    return pressed;
}

bool FormPanel::key(const std::string& label) {
    const size_t i = add(Kind::Key, label);
    bool pressed = false;
    for (const Event& e : take(i)) pressed |= e.start;
    Row& r = m_rows[i];
    r.label = label;
    r.enabled = true;
    return pressed;
}

void FormPanel::newline() { add(Kind::Break, {}); }

bool FormPanel::toggle(const std::string& label, bool& value) {
    const size_t i = add(Kind::Toggle, label);
    bool changed = false;
    for (const Event& e : take(i)) {
        if (!e.start) continue;
        value = !value;
        changed = true;
    }
    Row& r = m_rows[i];
    r.label = label;
    r.on = value;
    return changed;
}

bool FormPanel::choice(const std::string& label, int& index, const std::vector<std::string>& options) {
    const size_t i = add(Kind::Choice, label);
    const int n = static_cast<int>(options.size());
    bool changed = false;
    if (n > 0) {
        for (const Event& e : take(i)) {
            const int step = e.part == Part::Minus ? -1 : 1;
            index = ((std::clamp(index, 0, n - 1) + step) % n + n) % n;
            changed = true;
        }
    }
    Row& r = m_rows[i];
    r.label = label;
    r.value = n > 0 ? options[static_cast<size_t>(std::clamp(index, 0, n - 1))] : std::string();
    return changed;
}

bool FormPanel::slider(const std::string& label, float& value, float min, float max, const std::string& format, float step,
                       bool* started) {
    const size_t i = add(Kind::Slider, label);
    if (started) *started = false;
    if (step <= 0.0f) step = (max - min) / 50.0f;
    bool changed = false;
    for (const Event& e : take(i)) {
        if (e.start && started) *started = true;
        float v = value;
        if (e.part == Part::Bar) v = min + e.fraction * (max - min);
        else if (e.part == Part::Minus) v -= step;
        else if (e.part == Part::Plus) v += step;
        v = std::clamp(v, min, max);
        if (v != value) {
            value = v;
            changed = true;
        }
    }
    Row& r = m_rows[i];
    r.label = label;
    r.value = kke::DemoPanelModule::formatValue(format, value);
    r.fill[0] = max > min ? std::clamp((value - min) / (max - min), 0.0f, 1.0f) : 0.0f;
    return changed;
}

bool FormPanel::slider(const std::string& label, int& value, int min, int max) {
    float v = static_cast<float>(value);
    if (!slider(label, v, static_cast<float>(min), static_cast<float>(max), "%.0f", 1.0f)) return false;
    const int rounded = static_cast<int>(std::lround(v));
    if (rounded == value) return false;
    value = rounded;
    return true;
}

bool FormPanel::number(const std::string& label, float& value, float step, const std::string& format, bool* started) {
    const size_t i = add(Kind::Number, label);
    if (started) *started = false;
    bool changed = false;
    for (const Event& e : take(i)) {
        if (e.start && started) *started = true;
        if (e.part == Part::Minus) value -= step;
        else if (e.part == Part::Plus) value += step;
        else continue;
        changed = true;
    }
    Row& r = m_rows[i];
    r.label = label;
    r.value = kke::DemoPanelModule::formatValue(format, value);
    return changed;
}

bool FormPanel::colour(const std::string& label, glm::vec3& value) {
    const size_t i = add(Kind::Colour, label);
    bool changed = false;
    for (const Event& e : take(i)) {
        const int c = std::clamp(e.sub, 0, 2);
        float v = value[c];
        if (e.part == Part::Bar) v = e.fraction;
        else if (e.part == Part::Minus) v -= 0.02f;
        else if (e.part == Part::Plus) v += 0.02f;
        v = std::clamp(v, 0.0f, 1.0f);
        if (v != value[c]) {
            value[c] = v;
            changed = true;
        }
    }
    Row& r = m_rows[i];
    r.label = label;
    r.swatch = value;
    for (int c = 0; c < 3; ++c) r.fill[c] = std::clamp(value[c], 0.0f, 1.0f);
    return changed;
}

bool FormPanel::textField(const std::string& label, std::string& value) {
    const size_t i = add(Kind::TextField, label);
    bool changed = false;
    for (const Event& e : take(i)) {
        if (e.part != Part::Text || e.text == value) continue;
        value = e.text;
        changed = true;
    }
    Row& r = m_rows[i];
    r.label = label;
    r.value = value;
    return changed;
}

bool FormPanel::tile(const std::string& label, const std::string& image, const std::string& word, bool on) {
    const size_t i = add(Kind::Tile, label + '\x1e' + image + '\x1e' + word);
    bool pressed = false;
    for (const Event& e : take(i)) pressed |= e.start;
    Row& r = m_rows[i];
    r.label = label;
    r.image = image;
    r.word = word;
    r.on = on;
    return pressed;
}

// ---------------------------------------------------------------- document

std::string FormPanel::rowRml(size_t i) const {
    const Row& r = m_rows[i];
    const std::string n = std::to_string(i);
    const std::string at = " data-row=\"" + n + "\"";
    auto bar = [&](int sub, const char* cls) {
        const std::string s = std::to_string(sub);
        return "<div class=\"sl " + std::string(cls) + "\"><span class=\"step left\"" + at + " data-part=\"minus\" data-sub=\"" + s +
               "\">-</span><div class=\"bar\" id=\"b" + n + "_" + s + "\"" + at + " data-part=\"bar\" data-sub=\"" + s +
               "\"><div class=\"fill\" id=\"f" + n + "_" + s + "\"></div></div><span class=\"step right\"" + at +
               " data-part=\"plus\" data-sub=\"" + s + "\">+</span></div>";
    };
    const std::string label = "<span class=\"label\">" + esc(r.label) + "</span>";
    switch (r.kind) {
    case Kind::Section:
        return "<div class=\"section\"" + at + " data-part=\"press\"><span class=\"fold\" id=\"v" + n + "\"></span>" + esc(r.label) +
               "</div>";
    case Kind::Text: {
        const char* tone = r.tone == Tone::Muted ? " muted" : r.tone == Tone::Good ? " good" : r.tone == Tone::Warn ? " warn" : "";
        return "<div class=\"text" + std::string(tone) + "\" id=\"v" + n + "\"></div>";
    }
    case Kind::Button:
        return "<div class=\"button\" id=\"s" + n + "\"" + at + " data-part=\"press\">" + esc(r.label) + "</div>";
    case Kind::Key:
        return "<div class=\"button key\" id=\"s" + n + "\"" + at + " data-part=\"press\">" + esc(r.label) + "</div>";
    case Kind::Break:
        return "<div></div>";
    case Kind::Toggle:
        return "<div class=\"toggle\"" + at + " data-part=\"press\"><span class=\"box\" id=\"s" + n + "\"></span>" + esc(r.label) +
               "</div>";
    case Kind::Choice:
        return "<div class=\"line\">" + label + "<span class=\"ctl\"><span class=\"step\"" + at +
               " data-part=\"minus\">&lt;</span><span class=\"val press\" id=\"v" + n + "\"" + at +
               " data-part=\"plus\"></span><span class=\"step\"" + at + " data-part=\"plus\">&gt;</span></span></div>";
    case Kind::Slider:
        return "<div class=\"line\">" + label + "<span class=\"ctl\"><span class=\"val\" id=\"v" + n + "\"></span></span></div>" +
               bar(0, "");
    case Kind::Number:
        return "<div class=\"line\">" + label + "<span class=\"ctl\"><span class=\"step\"" + at +
               " data-part=\"minus\">-</span><span class=\"val num\" id=\"v" + n + "\"></span><span class=\"step\"" + at +
               " data-part=\"plus\">+</span></span></div>";
    case Kind::Colour:
        return "<div class=\"line\">" + label + "<span class=\"ctl\"><span class=\"swatch\" id=\"s" + n + "\"></span></span></div>" +
               bar(0, "r") + bar(1, "g") + bar(2, "b");
    case Kind::TextField:
        return "<div class=\"line\">" + label + "<input type=\"text\" class=\"field\" id=\"v" + n + "\"" + at + " data-part=\"text\"/></div>";
    case Kind::Tile: {
        std::string pic = r.image.empty() ? "<span class=\"word\">" + esc(r.word) + "</span>" : "<img src=\"" + esc(r.image) + "\"/>";
        return "<div class=\"tile\" id=\"s" + n + "\"" + at + " data-part=\"press\"><div class=\"pic\">" + pic +
               "</div><div class=\"name\">" + esc(r.label) + "</div></div>";
    }
    }
    return {};
}

void FormPanel::build() {
    std::string rml;
    for (size_t i = 0; i < m_rows.size(); ++i) {
        // Buttons flow side by side and tiles in rows, but pictures start
        // on a line of their own, under the buttons.
        const auto inlineKind = [](Kind k) { return k == Kind::Button || k == Kind::Key || k == Kind::Tile; };
        if (i > 0 && inlineKind(m_rows[i].kind) && inlineKind(m_rows[i - 1].kind) && m_rows[i].kind != m_rows[i - 1].kind)
            rml += "<div></div>";
        rml += rowRml(i);
    }
    m_doc->GetElementById("rows")->SetInnerRML(rml);
    for (size_t i = 0; i < m_rows.size(); ++i) {
        Row& r = m_rows[i];
        const std::string n = std::to_string(i);
        r.builtSig = r.sig;
        r.valueEl = m_doc->GetElementById("v" + n);
        r.stateEl = m_doc->GetElementById("s" + n);
        for (int c = 0; c < 3; ++c) {
            r.fillEl[c] = m_doc->GetElementById("f" + n + "_" + std::to_string(c));
            r.shownFill[c] = -1.0f;
        }
        r.shownValue.clear();
        r.shownState = -1;
    }
    // Presses were on the old rows.
    m_dragRow = -1;
    m_holdRow = -1;
    refresh();
}

void FormPanel::refresh() {
    Rml::Element* focus = m_context ? m_context->GetFocusElement() : nullptr;
    for (Row& r : m_rows) {
        if (r.kind == Kind::TextField) {
            // The field is the value while it has the keyboard.
            if (r.valueEl && r.valueEl != focus && r.valueEl->GetAttribute<Rml::String>("value", "") != r.value)
                r.valueEl->SetAttribute("value", r.value);
        } else if (r.valueEl && r.shownValue != r.value) {
            r.valueEl->SetInnerRML(esc(r.value));
            r.shownValue = r.value;
        }
        for (int c = 0; c < 3; ++c) {
            if (!r.fillEl[c] || std::abs(r.shownFill[c] - r.fill[c]) < 1e-4f) continue;
            r.fillEl[c]->SetProperty("width", std::to_string(r.fill[c] * 100.0f) + "%");
            r.shownFill[c] = r.fill[c];
        }
        if (!r.stateEl) continue;
        if (r.kind == Kind::Colour) {
            const std::string h = hex(r.swatch);
            if (h != r.shownValue) {
                r.stateEl->SetProperty("background-color", h);
                r.shownValue = h;
            }
            continue;
        }
        const bool isButton = r.kind == Kind::Button || r.kind == Kind::Key;
        const int state = isButton ? (r.enabled ? 1 : 0) : (r.on ? 1 : 0);
        if (state == r.shownState) continue;
        r.shownState = state;
        if (isButton) r.stateEl->SetClass("disabled", !r.enabled);
        else r.stateEl->SetClass("on", r.on);
    }
}

float FormPanel::barFraction(size_t row, int sub, float pixelX) const {
    if (!m_doc) return 0.0f;
    Rml::Element* bar = m_doc->GetElementById("b" + std::to_string(row) + "_" + std::to_string(sub));
    if (!bar) return 0.0f;
    const float x = bar->GetAbsoluteOffset(Rml::BoxArea::Border).x;
    const float w = bar->GetBox().GetSize(Rml::BoxArea::Border).x;
    return w > 0.0f ? std::clamp((pixelX - x) / w, 0.0f, 1.0f) : 0.0f;
}

void FormPanel::pressed(size_t row, Part part, int sub, float pixelX) {
    if (row >= m_rows.size()) return;
    Event e;
    e.row = row;
    e.part = part;
    e.sub = sub;
    e.start = true;
    if (part == Part::Bar) {
        e.fraction = barFraction(row, sub, pixelX);
        m_dragRow = static_cast<long>(row);
        m_dragSub = sub;
    } else if (part == Part::Minus || part == Part::Plus) {
        m_holdRow = static_cast<long>(row);
        m_holdPart = part;
        m_holdNext = m_now + 0.4;
    }
    m_events.push_back(std::move(e));
}

void FormPanel::typed(size_t row, std::string text) {
    if (row >= m_rows.size()) return;
    Event e;
    e.row = row;
    e.part = Part::Text;
    e.text = std::move(text);
    m_events.push_back(std::move(e));
}

bool FormPanel::contains(glm::vec2 point) const {
    if (!m_doc || !m_visible) return false;
    Rml::Element* panel = m_doc->GetElementById("panel");
    const Rml::Vector2f o = panel->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f s = panel->GetBox().GetSize(Rml::BoxArea::Border);
    const glm::vec2 px = point * m_ppp;
    return px.x >= o.x && px.y >= o.y && px.x <= o.x + s.x && px.y <= o.y + s.y;
}

void FormPanel::scroll(float points) {
    if (!m_doc || !m_visible) return;
    Rml::Element* panel = m_doc->GetElementById("panel");
    panel->SetScrollTop(panel->GetScrollTop() + points * m_ppp);
}

bool FormPanel::typing() const {
    if (!m_doc || !m_visible || !m_context) return false;
    const Rml::Element* focus = m_context->GetFocusElement();
    return focus && focus->GetOwnerDocument() == m_doc && focus->GetTagName() == "input";
}

void FormPanel::blur() {
    if (!typing()) return;
    m_context->GetFocusElement()->Blur();
}

} // namespace kke_sandbox
