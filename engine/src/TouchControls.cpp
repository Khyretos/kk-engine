#include "kke/TouchControls.h"

#include "kke/InputMap.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace kke {

namespace {

// SDL_GamepadAxis and SDL_GamepadButton numbers (kept free of SDL here).
constexpr int kLeftX = 0, kLeftY = 1, kRightX = 2, kRightY = 3, kLeftTrigger = 4, kRightTrigger = 5;
constexpr int kPadBack = 4, kPadGuide = 5, kPadStart = 6;

constexpr float kStickReach = 1.6f;  // a stick takes thumbs this many radii from its centre
constexpr float kButtonReach = 1.1f;
constexpr float kMinSize = 0.07f, kMaxSize = 0.42f;

bool menuAction(const ActionDef& a) {
    const std::string& id = a.id;
    auto starts = [&](const char* p) { return id.rfind(p, 0) == 0; };
    if (a.context == "ui" || a.context == "shell" || starts("ui.") || starts("shell.") || starts("pause.")) return true;
    if (id == "panels" || id == "voice.talk" || id == "audio.ping") return true;
    std::string cat = a.category;
    std::transform(cat.begin(), cat.end(), cat.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return cat.find("debug") != std::string::npos || cat.find("developer") != std::string::npos;
}

bool onStickAxis(const InputSource& s, int x, int y) {
    return s.kind == SourceKind::GamepadAxis && (s.code == x || s.code == y);
}

// How much a controller player leans on a pad control: A first, then the
// other face buttons, the triggers, the shoulders, the stick clicks, the
// d-pad. Chords (LB+A) and Start/Back/Guide are not a button's job.
int padRank(const Binding& b) {
    int rank = -1;
    if (b.source.kind == SourceKind::GamepadButton) {
        static constexpr int kRank[] = { 0, 2, 1, 3, -1, -1, -1, 8, 9, 7, 6, 10, 11, 12, 13 };
        if (b.source.code >= 0 && b.source.code < static_cast<int>(std::size(kRank))) rank = kRank[b.source.code];
        else rank = 20;
        if (b.source.code == kPadBack || b.source.code == kPadGuide || b.source.code == kPadStart) rank = -1;
    } else if (b.source.kind == SourceKind::GamepadAxis && (b.source.code == kRightTrigger || b.source.code == kLeftTrigger)) {
        rank = b.source.code == kRightTrigger ? 4 : 5;
    }
    if (rank >= 0 && !b.modifiers.empty()) rank += 30;
    return rank;
}

glm::vec2 vec(const nlohmann::json& j, glm::vec2 fallback) {
    if (j.is_array() && j.size() == 2 && j[0].is_number() && j[1].is_number()) return { j[0].get<float>(), j[1].get<float>() };
    return fallback;
}

} // namespace

const char* TouchControls::kindName(TouchControl::Kind kind) {
    switch (kind) {
    case TouchControl::Kind::Stick: return "stick";
    case TouchControl::Kind::Button: return "button";
    case TouchControl::Kind::Look: return "look";
    case TouchControl::Kind::Pause: return "pause";
    }
    return "button";
}

// ---------------------------------------------------------------- layout

std::vector<std::string> TouchControls::buttonActions(const InputMap& map) {
    std::vector<std::string> out;
    for (const ActionDef& a : map.actions()) {
        if (menuAction(a) || a.type == ActionType::Axis2D) continue;
        if (a.type == ActionType::Axis1D) {
            // A held button makes sense for a trigger or a key, not for a stick's axis.
            bool pressable = false;
            for (size_t i : map.bindingsFor(a.id)) {
                const Binding& b = map.bindings()[i];
                if (onStickAxis(b.source, kLeftX, kLeftY) || onStickAxis(b.source, kRightX, kRightY)) continue;
                if (b.source.kind != SourceKind::MouseMotion && b.source.kind != SourceKind::MouseWheel) pressable = true;
            }
            if (!pressable) continue;
        }
        out.push_back(a.id);
    }
    return out;
}

std::string TouchControls::lookAction(const InputMap& map) {
    if (const ActionDef* a = map.action("look.rate"); a && a->type == ActionType::Axis2D && !menuAction(*a)) return a->id;
    for (const ActionDef& a : map.actions()) {
        if (a.type != ActionType::Axis2D || menuAction(a) || a.id.rfind("camera.", 0) == 0) continue;
        for (size_t i : map.bindingsFor(a.id))
            if (onStickAxis(map.bindings()[i].source, kRightX, kRightY)) return a.id;
    }
    return {};
}

std::vector<TouchControl> TouchControls::autoLayout(const InputMap& map, const TouchLayoutOptions& options) {
    bool left = false, right = false;
    for (const Binding& b : map.bindings()) {
        const ActionDef* a = map.action(b.action);
        if (!a || menuAction(*a)) continue;
        for (const InputSource* s : { &b.source, &b.sourceY }) {
            left = left || onStickAxis(*s, kLeftX, kLeftY);
            right = right || (onStickAxis(*s, kRightX, kRightY) && a->id.rfind("camera.", 0) != 0);
        }
    }
    std::vector<TouchControl> out;
    if (left) {
        TouchControl c;
        c.kind = TouchControl::Kind::Stick;
        c.stick = 0;
        c.anchor = { 0.0f, 1.0f };
        c.offset = { 0.21f, -0.23f };
        c.size = 0.27f;
        out.push_back(c);
    }
    const std::string look = lookAction(map);
    const bool rightStick = right && options.look == TouchLayoutOptions::Look::Stick;
    if (!look.empty() && options.look == TouchLayoutOptions::Look::Area) {
        TouchControl c;
        c.kind = TouchControl::Kind::Look;
        c.action = look;
        out.push_back(c);
    }
    if (rightStick) {
        TouchControl c;
        c.kind = TouchControl::Kind::Stick;
        c.stick = 1;
        c.anchor = { 1.0f, 1.0f };
        c.offset = { -0.21f, -0.23f };
        c.size = 0.27f;
        out.push_back(c);
    }

    // The buttons: the game's list, or the actions a controller player
    // presses most, best pad button first.
    std::vector<std::string> buttons = options.buttons;
    if (buttons.empty()) {
        std::vector<std::pair<int, std::string>> ranked;
        for (const std::string& id : buttonActions(map)) {
            int best = -1;
            for (size_t i : map.bindingsFor(id)) {
                const int r = padRank(map.bindings()[i]);
                if (r >= 0 && (best < 0 || r < best)) best = r;
            }
            if (best >= 0) ranked.emplace_back(best, id);
        }
        std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (auto& r : ranked) buttons.push_back(r.second);
    }
    // Slots from the bottom-right corner, the thumb's home first; above
    // the right stick when there is one.
    static const struct { glm::vec2 offset; float size; } kArc[] = {
        { { -0.13f, -0.15f }, 0.17f }, { { -0.33f, -0.11f }, 0.14f }, { { -0.11f, -0.36f }, 0.14f },
        { { -0.30f, -0.30f }, 0.14f }, { { -0.50f, -0.10f }, 0.13f }, { { -0.10f, -0.55f }, 0.13f },
    };
    static const struct { glm::vec2 offset; float size; } kAbove[] = {
        { { -0.12f, -0.50f }, 0.13f }, { { -0.30f, -0.50f }, 0.13f }, { { -0.12f, -0.67f }, 0.12f }, { { -0.30f, -0.67f }, 0.12f },
    };
    const size_t slots = rightStick ? std::size(kAbove) : std::size(kArc);
    const size_t count = std::min({ buttons.size(), slots, static_cast<size_t>(std::max(0, options.maxButtons)) });
    for (size_t i = 0; i < count; ++i) {
        const ActionDef* a = map.action(buttons[i]);
        if (!a) continue;
        TouchControl c;
        c.kind = TouchControl::Kind::Button;
        c.action = a->id;
        c.anchor = { 1.0f, 1.0f };
        c.offset = rightStick ? kAbove[i].offset : kArc[i].offset;
        c.size = rightStick ? kAbove[i].size : kArc[i].size;
        // What a pad toggles (crouch, sprint) a tap latches.
        for (size_t b : map.bindingsFor(a->id))
            if (map.bindings()[b].trigger == Trigger::Toggle) c.latch = true;
        out.push_back(c);
    }
    TouchControl pause;
    pause.kind = TouchControl::Kind::Pause;
    pause.anchor = { 1.0f, 0.0f };
    pause.offset = { -0.06f, 0.06f };
    pause.size = 0.09f;
    out.push_back(pause);
    return out;
}

void TouchControls::setDefaults(std::vector<TouchControl> controls) {
    const bool following = !m_haveDefaults || m_layout == m_defaults;
    m_defaults = std::move(controls);
    m_haveDefaults = true;
    if (following) setLayout(m_defaults);
}

void TouchControls::setLayout(std::vector<TouchControl> controls) {
    releaseAll();
    m_layout = std::move(controls);
    m_latched.assign(m_layout.size(), 0);
    if (m_selected >= static_cast<int>(m_layout.size())) m_selected = -1;
}

void TouchControls::resetToDefaults() {
    setLayout(m_defaults);
    opacity = 0.6f;
    lookSpeed = 1.0f;
    m_selected = -1;
}

// ---------------------------------------------------------------- screen

void TouchControls::setScreen(const glm::vec2& size, const ScreenRect& safe) {
    m_size = size;
    m_safe = safe.w > 0.0f && safe.h > 0.0f ? safe : ScreenRect{ 0.0f, 0.0f, size.x, size.y };
}

float TouchControls::shortSide() const { return std::max(1.0f, std::min(m_safe.w, m_safe.h)); }

float TouchControls::radius(const TouchControl& c) const { return 0.5f * c.size * shortSide(); }

glm::vec2 TouchControls::centre(const TouchControl& c) const {
    const glm::vec2 origin(m_safe.x, m_safe.y), extent(m_safe.w, m_safe.h);
    glm::vec2 p = origin + c.anchor * extent + c.offset * shortSide();
    // Always whole on screen, whatever shape the screen turned to.
    const float r = radius(c);
    p.x = std::clamp(p.x, origin.x + r, std::max(origin.x + r, origin.x + extent.x - r));
    p.y = std::clamp(p.y, origin.y + r, std::max(origin.y + r, origin.y + extent.y - r));
    return p;
}

int TouchControls::controlAt(const glm::vec2& px) const {
    int best = -1;
    float bestFit = 0.0f;
    for (size_t i = 0; i < m_layout.size(); ++i) {
        const TouchControl& c = m_layout[i];
        if (c.kind == TouchControl::Kind::Look) continue;
        const float reach = m_editing ? 1.0f : c.kind == TouchControl::Kind::Stick ? kStickReach : kButtonReach;
        const float fit = glm::length(px - centre(c)) / std::max(1.0f, radius(c));
        if (fit <= reach && (best < 0 || fit < bestFit)) {
            best = static_cast<int>(i);
            bestFit = fit;
        }
    }
    return best;
}

bool TouchControls::wouldTake(const glm::vec2& px) const {
    if (controlAt(px) >= 0) return true;
    if (m_editing) return false;
    return std::any_of(m_layout.begin(), m_layout.end(), [](const TouchControl& c) { return c.kind == TouchControl::Kind::Look; });
}

// ---------------------------------------------------------------- fingers

TouchControls::Finger* TouchControls::finger(uint64_t id) {
    for (Finger& f : m_fingers)
        if (f.id == id) return &f;
    return nullptr;
}

bool TouchControls::owns(uint64_t id) const {
    return std::any_of(m_fingers.begin(), m_fingers.end(), [&](const Finger& f) { return f.id == id; });
}

void TouchControls::edge(const std::string& action, bool down) {
    if (!action.empty()) m_edges.emplace_back(action, down);
}

bool TouchControls::fingerDown(uint64_t id, const glm::vec2& px) {
    if (owns(id)) fingerUp(id);
    const int at = controlAt(px);
    if (at < 0 && !wouldTake(px)) return false;
    Finger f;
    f.id = id;
    f.control = at;
    f.pos = px;
    if (m_editing) {
        m_selected = at;
        f.grab = px - centre(m_layout[static_cast<size_t>(at)]);
        m_fingers.push_back(f);
        return true;
    }
    m_fingers.push_back(f);
    if (at < 0) return true; // the look drag
    TouchControl& c = m_layout[static_cast<size_t>(at)];
    if (c.kind == TouchControl::Kind::Button) {
        if (c.latch) {
            uint8_t& on = m_latched[static_cast<size_t>(at)];
            on = !on;
            edge(c.action, on != 0);
        } else {
            edge(c.action, true);
        }
    } else if (c.kind == TouchControl::Kind::Pause) {
        m_pause = true;
    }
    return true;
}

bool TouchControls::fingerMove(uint64_t id, const glm::vec2& px) {
    Finger* f = finger(id);
    if (!f) return false;
    if (m_editing) {
        if (f->control >= 0) moveTo(m_layout[static_cast<size_t>(f->control)], px - f->grab);
    } else if (f->control < 0) {
        m_lookMoved += px - f->pos;
    }
    f->pos = px;
    return true;
}

bool TouchControls::fingerUp(uint64_t id) {
    auto it = std::find_if(m_fingers.begin(), m_fingers.end(), [&](const Finger& f) { return f.id == id; });
    if (it == m_fingers.end()) return false;
    const int at = it->control;
    m_fingers.erase(it);
    if (!m_editing && at >= 0) {
        const TouchControl& c = m_layout[static_cast<size_t>(at)];
        // A second finger may still hold the same button.
        const bool stillHeld = std::any_of(m_fingers.begin(), m_fingers.end(), [&](const Finger& f) { return f.control == at; });
        if (c.kind == TouchControl::Kind::Button && !c.latch && !stillHeld) edge(c.action, false);
    }
    return true;
}

void TouchControls::releaseAll() {
    for (size_t i = 0; i < m_layout.size(); ++i) {
        const TouchControl& c = m_layout[i];
        if (c.kind != TouchControl::Kind::Button) continue;
        const bool down = (i < m_latched.size() && m_latched[i]) ||
                          std::any_of(m_fingers.begin(), m_fingers.end(), [&](const Finger& f) { return f.control == static_cast<int>(i); });
        if (down) edge(c.action, false);
    }
    m_fingers.clear();
    std::fill(m_latched.begin(), m_latched.end(), uint8_t{ 0 });
    m_lookMoved = glm::vec2(0.0f);
}

// ---------------------------------------------------------------- output

glm::vec2 TouchControls::stick(int s) const {
    if (m_editing) return glm::vec2(0.0f);
    for (const Finger& f : m_fingers) {
        if (f.control < 0) continue;
        const TouchControl& c = m_layout[static_cast<size_t>(f.control)];
        if (c.kind != TouchControl::Kind::Stick || c.stick != s) continue;
        glm::vec2 v = (f.pos - centre(c)) / std::max(1.0f, radius(c));
        const float len = glm::length(v);
        if (len > 1.0f) v /= len;
        return v;
    }
    return glm::vec2(0.0f);
}

float TouchControls::sourceValue(const InputSource& s) const {
    if (s.kind != SourceKind::GamepadAxis || s.device != 0) return 0.0f;
    switch (s.code) {
    case kLeftX: return stick(0).x;
    case kLeftY: return stick(0).y;
    case kRightX: return stick(1).x;
    case kRightY: return stick(1).y;
    default: return 0.0f;
    }
}

bool TouchControls::held(int index) const {
    if (index < 0 || index >= static_cast<int>(m_layout.size())) return false;
    if (static_cast<size_t>(index) < m_latched.size() && m_latched[static_cast<size_t>(index)]) return true;
    return !m_editing && std::any_of(m_fingers.begin(), m_fingers.end(), [&](const Finger& f) { return f.control == index; });
}

void TouchControls::apply(InputMap& map, float dt) {
    // Buttons, in the order they went down and up (a tap between two
    // updates still counts: InputMap::setScreenButton).
    for (const auto& [action, down] : m_edges) {
        const ActionDef* a = map.action(action);
        if (!a) continue;
        if (a->type == ActionType::Button) {
            map.setScreenButton(action, down);
        } else if (a->type == ActionType::Axis1D) {
            map.setScreenAxis(action, glm::vec2(down ? 1.0f : 0.0f, 0.0f));
        }
    }
    m_edges.clear();

    // The look drag: as fast as the finger moves, so a flick turns fast
    // and a slow slide aims finely. Drag up looks up (axes are +y up).
    std::string look;
    bool dragging = false;
    for (const TouchControl& c : m_layout)
        if (c.kind == TouchControl::Kind::Look) look = c.action;
    for (const Finger& f : m_fingers) dragging = dragging || f.control < 0;
    if (!m_lookAppliedTo.empty() && m_lookAppliedTo != look) map.setScreenAxis(m_lookAppliedTo, glm::vec2(0.0f));
    m_lookAppliedTo = look;
    if (!look.empty() && !m_editing && (dragging || m_lookWasOn)) {
        const glm::vec2 rate = dt > 1e-4f ? m_lookMoved / (shortSide() * dt) * lookSpeed : glm::vec2(0.0f);
        map.setScreenAxis(look, { rate.x, -rate.y });
        m_lookWasOn = glm::length(rate) > 0.0f;
    }
    m_lookMoved = glm::vec2(0.0f);
}

bool TouchControls::takePause() {
    const bool p = m_pause;
    m_pause = false;
    return p;
}

// ---------------------------------------------------------------- editing

void TouchControls::setEditing(bool editing) {
    if (editing == m_editing) return;
    releaseAll();
    m_editing = editing;
    m_pause = false;
    if (!editing) m_selected = -1;
}

void TouchControls::select(int index) { m_selected = index >= 0 && index < static_cast<int>(m_layout.size()) ? index : -1; }

void TouchControls::moveTo(TouchControl& c, const glm::vec2& centrePx) const {
    // Keep to the nearest corner or edge, so the layout survives the
    // screen turning and other screen shapes.
    const glm::vec2 origin(m_safe.x, m_safe.y), extent(std::max(1.0f, m_safe.w), std::max(1.0f, m_safe.h));
    const glm::vec2 rel = (centrePx - origin) / extent;
    auto third = [](float v) { return v < 1.0f / 3.0f ? 0.0f : v > 2.0f / 3.0f ? 1.0f : 0.5f; };
    c.anchor = { third(rel.x), third(rel.y) };
    const glm::vec2 anchorPx = origin + c.anchor * extent;
    c.offset = (centrePx - anchorPx) / shortSide();
    c.offset = (centre(c) - anchorPx) / shortSide(); // kept whole on screen
}

void TouchControls::resize(int index, float factor) {
    if (index < 0 || index >= static_cast<int>(m_layout.size())) return;
    TouchControl& c = m_layout[static_cast<size_t>(index)];
    if (c.kind == TouchControl::Kind::Look) return;
    c.size = std::clamp(c.size * factor, kMinSize, kMaxSize);
}

void TouchControls::setAction(int index, const std::string& action) {
    if (index < 0 || index >= static_cast<int>(m_layout.size())) return;
    TouchControl& c = m_layout[static_cast<size_t>(index)];
    if (c.kind != TouchControl::Kind::Button && c.kind != TouchControl::Kind::Look) return;
    if (c.kind == TouchControl::Kind::Button && static_cast<size_t>(index) < m_latched.size() && m_latched[static_cast<size_t>(index)]) {
        edge(c.action, false);
        m_latched[static_cast<size_t>(index)] = 0;
    }
    c.action = action;
    c.label.clear();
}

void TouchControls::setLatch(int index, bool latch) {
    if (index < 0 || index >= static_cast<int>(m_layout.size())) return;
    m_layout[static_cast<size_t>(index)].latch = latch;
    if (!latch && static_cast<size_t>(index) < m_latched.size() && m_latched[static_cast<size_t>(index)]) {
        edge(m_layout[static_cast<size_t>(index)].action, false);
        m_latched[static_cast<size_t>(index)] = 0;
    }
}

int TouchControls::addButton(const std::string& action) {
    TouchControl c;
    c.kind = TouchControl::Kind::Button;
    c.action = action;
    c.anchor = { 0.5f, 0.5f };
    c.offset = { 0.0f, 0.0f };
    c.size = 0.14f;
    m_layout.push_back(c);
    m_latched.push_back(0);
    m_selected = static_cast<int>(m_layout.size()) - 1;
    return m_selected;
}

void TouchControls::remove(int index) {
    if (index < 0 || index >= static_cast<int>(m_layout.size())) return;
    releaseAll();
    m_layout.erase(m_layout.begin() + index);
    m_latched.erase(m_latched.begin() + index);
    m_selected = -1;
}

// ---------------------------------------------------------------- JSON

nlohmann::json TouchControls::save() const {
    nlohmann::json j{ { "version", 1 }, { "opacity", opacity }, { "lookSpeed", lookSpeed } };
    if (!m_haveDefaults || m_layout != m_defaults) {
        nlohmann::json list = nlohmann::json::array();
        for (const TouchControl& c : m_layout) {
            nlohmann::json o{ { "kind", kindName(c.kind) },
                              { "anchor", { c.anchor.x, c.anchor.y } },
                              { "offset", { c.offset.x, c.offset.y } },
                              { "size", c.size } };
            if (c.kind == TouchControl::Kind::Stick) o["stick"] = c.stick == 1 ? "right" : "left";
            if (!c.action.empty()) o["action"] = c.action;
            if (!c.label.empty()) o["label"] = c.label;
            if (c.latch) o["latch"] = true;
            list.push_back(std::move(o));
        }
        j["controls"] = std::move(list);
    }
    return j;
}

bool TouchControls::load(const nlohmann::json& j, const InputMap& map) {
    if (!j.is_object()) return false;
    opacity = std::clamp(j.value("opacity", 0.6f), 0.15f, 1.0f);
    lookSpeed = std::clamp(j.value("lookSpeed", 1.0f), 0.1f, 5.0f);
    const auto list = j.find("controls");
    if (list == j.end() || !list->is_array()) return true; // the game's own layout
    std::vector<TouchControl> controls;
    for (const nlohmann::json& o : *list) {
        if (!o.is_object()) continue;
        TouchControl c;
        const std::string kind = o.value("kind", std::string("button"));
        c.kind = kind == "stick" ? TouchControl::Kind::Stick : kind == "look" ? TouchControl::Kind::Look
               : kind == "pause" ? TouchControl::Kind::Pause : TouchControl::Kind::Button;
        c.stick = o.value("stick", std::string("left")) == "right" ? 1 : 0;
        c.action = o.value("action", std::string());
        c.label = o.value("label", std::string());
        c.anchor = vec(o.value("anchor", nlohmann::json()), glm::vec2(1.0f));
        c.offset = vec(o.value("offset", nlohmann::json()), glm::vec2(0.0f));
        c.anchor = glm::clamp(c.anchor, glm::vec2(0.0f), glm::vec2(1.0f));
        c.offset = glm::clamp(c.offset, glm::vec2(-2.0f), glm::vec2(2.0f));
        c.size = std::clamp(o.value("size", 0.14f), kMinSize, kMaxSize);
        c.latch = o.value("latch", false);
        // A game update may have renamed or dropped an action.
        if ((c.kind == TouchControl::Kind::Button || c.kind == TouchControl::Kind::Look) && !map.action(c.action)) continue;
        controls.push_back(std::move(c));
    }
    setLayout(std::move(controls));
    return true;
}

} // namespace kke
