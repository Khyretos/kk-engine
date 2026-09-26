#include "kke/InputMap.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace kke {

bool InputSource::isAnalog() const {
    switch (kind) {
    case SourceKind::GamepadAxis:
    case SourceKind::JoyAxis:
    case SourceKind::Gyro:
    case SourceKind::Accel:
    case SourceKind::TouchX:
    case SourceKind::TouchY:
    case SourceKind::MouseMotion:
    case SourceKind::MouseWheel: return true;
    default: return false;
    }
}

void InputMap::defineAction(const ActionDef& def) {
    auto it = m_actionIndex.find(def.id);
    if (it != m_actionIndex.end()) {
        m_actions[it->second] = def;
        return;
    }
    m_actionIndex[def.id] = m_actions.size();
    m_actions.push_back(def);
    m_states.emplace_back();
}

const ActionDef* InputMap::action(const std::string& id) const {
    auto it = m_actionIndex.find(id);
    return it == m_actionIndex.end() ? nullptr : &m_actions[it->second];
}

size_t InputMap::addBinding(const Binding& b) {
    m_bindings.push_back(b);
    m_runtime.emplace_back();
    return m_bindings.size() - 1;
}

void InputMap::removeBinding(size_t index) {
    if (index >= m_bindings.size()) return;
    m_bindings.erase(m_bindings.begin() + static_cast<std::ptrdiff_t>(index));
    m_runtime.erase(m_runtime.begin() + static_cast<std::ptrdiff_t>(index));
}

void InputMap::clearBindings(const std::string& action) {
    for (size_t i = m_bindings.size(); i-- > 0;)
        if (m_bindings[i].action == action) removeBinding(i);
}

std::vector<size_t> InputMap::bindingsFor(const std::string& action) const {
    std::vector<size_t> out;
    for (size_t i = 0; i < m_bindings.size(); ++i)
        if (m_bindings[i].action == action) out.push_back(i);
    return out;
}

namespace {
bool sameModifiers(const std::vector<InputSource>& a, const std::vector<InputSource>& b) {
    if (a.size() != b.size()) return false;
    for (const InputSource& s : a)
        if (std::find(b.begin(), b.end(), s) == b.end()) return false;
    return true;
}
// All of `small` are in `big`, and `big` has more.
bool strictSuperset(const std::vector<InputSource>& big, const std::vector<InputSource>& small) {
    if (big.size() <= small.size()) return false;
    for (const InputSource& s : small)
        if (std::find(big.begin(), big.end(), s) == big.end()) return false;
    return true;
}
bool isPulse(Trigger t) { return t == Trigger::Tap || t == Trigger::DoubleTap || t == Trigger::Release; }
} // namespace

std::vector<size_t> InputMap::conflicts(size_t index) const {
    std::vector<size_t> out;
    if (index >= m_bindings.size()) return out;
    const Binding& b = m_bindings[index];
    const ActionDef* ab = action(b.action);
    for (size_t i = 0; i < m_bindings.size(); ++i) {
        if (i == index) continue;
        const Binding& o = m_bindings[i];
        const ActionDef* ao = action(o.action);
        if (o.action == b.action || o.source != b.source || o.trigger != b.trigger) continue;
        if (ab && ao && ab->context != ao->context) continue;
        if (sameModifiers(o.modifiers, b.modifiers)) out.push_back(i);
    }
    return out;
}

void InputMap::setContextEnabled(const std::string& context, bool enabled) {
    auto it = std::find(m_disabledContexts.begin(), m_disabledContexts.end(), context);
    if (enabled && it != m_disabledContexts.end()) m_disabledContexts.erase(it);
    if (!enabled && it == m_disabledContexts.end()) m_disabledContexts.push_back(context);
}

bool InputMap::contextEnabled(const std::string& context) const {
    return std::find(m_disabledContexts.begin(), m_disabledContexts.end(), context) == m_disabledContexts.end();
}

float InputMap::sourceValue(const InputState& s, const InputSource& src) const {
    if (!src.valid()) return 0.0f;
    float v = s.value(src, m_devices.empty() ? nullptr : &m_devices);
    if (src.half > 0) v = std::max(v, 0.0f);
    else if (src.half < 0) v = std::max(-v, 0.0f);
    return v;
}

float InputMap::shaped(const Binding& b, float v) const {
    if (!b.source.isRelative()) {
        const float sign = v < 0.0f ? -1.0f : 1.0f;
        float a = std::abs(v);
        if (b.deadzone > 0.0f) a = a <= b.deadzone ? 0.0f : (a - b.deadzone) / std::max(1e-6f, 1.0f - b.deadzone);
        if (b.curve != 1.0f && a > 0.0f) a = std::pow(a, b.curve);
        v = sign * a;
    }
    if (b.invert) v = -v;
    return v * b.scale;
}

bool InputMap::hasDoubleTapSibling(size_t index) const {
    const Binding& b = m_bindings[index];
    for (size_t i = 0; i < m_bindings.size(); ++i)
        if (i != index && m_bindings[i].trigger == Trigger::DoubleTap && m_bindings[i].source == b.source &&
            sameModifiers(m_bindings[i].modifiers, b.modifiers))
            return true;
    return false;
}

void InputMap::update(const InputState& state, double now) {
    // Per-binding: is its source (with modifiers) active this frame?
    const size_t n = m_bindings.size();
    std::vector<uint8_t> modsHeld(n), rawActive(n), enabled(n);
    std::vector<float> raw(n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        const Binding& b = m_bindings[i];
        const ActionDef* def = action(b.action);
        enabled[i] = def && contextEnabled(def->context);
        bool mods = true;
        for (const InputSource& m : b.modifiers) mods = mods && std::abs(sourceValue(state, m)) >= 0.5f;
        modsHeld[i] = mods;
        raw[i] = sourceValue(state, b.source);
        const float mag = std::abs(raw[i]);
        rawActive[i] = b.source.isRelative() ? mag > 0.0f : b.source.isAnalog() ? mag >= b.threshold : mag >= 0.5f;
    }
    // Ctrl+E suppresses a plain E binding while Ctrl is down (Tarkov-style:
    // the most specific chord wins).
    std::vector<uint8_t> active(n);
    for (size_t i = 0; i < n; ++i) {
        bool suppressed = false;
        for (size_t j = 0; j < n && !suppressed; ++j)
            suppressed = j != i && enabled[j] && modsHeld[j] && m_bindings[j].source == m_bindings[i].source &&
                         strictSuperset(m_bindings[j].modifiers, m_bindings[i].modifiers);
        active[i] = enabled[i] && modsHeld[i] && rawActive[i] && !suppressed;
    }

    // Trigger state machines.
    std::vector<uint8_t> heldOut(n), fired(n);
    for (size_t i = 0; i < n; ++i) {
        const Binding& b = m_bindings[i];
        Runtime& r = m_runtime[i];
        const bool down = active[i];
        const bool rising = down && !r.down, falling = !down && r.down;
        if (rising) {
            r.downAt = now;
            r.holdFired = false;
        }
        const double duration = now - r.downAt;
        switch (b.trigger) {
        case Trigger::Continuous: heldOut[i] = down; fired[i] = rising; break;
        case Trigger::Press: heldOut[i] = down; fired[i] = rising; break;
        case Trigger::Release: fired[i] = falling; heldOut[i] = fired[i]; break;
        case Trigger::Hold:
            if (down && duration >= b.holdTime) {
                heldOut[i] = true;
                if (!r.holdFired) { fired[i] = true; r.holdFired = true; }
            }
            break;
        case Trigger::Tap:
            if (rising && r.pendingTap) {
                // Second press inside the window: that's the double tap's.
                r.pendingTap = false;
                r.swallowNextTap = true;
            }
            if (falling) {
                if (r.swallowNextTap) r.swallowNextTap = false;
                else if (duration < b.tapTime) {
                    if (hasDoubleTapSibling(i)) { r.pendingTap = true; r.pendingAt = now; }
                    else fired[i] = true;
                }
            }
            if (r.pendingTap && !down && now - r.pendingAt > b.doubleTapWindow) {
                r.pendingTap = false;
                fired[i] = true;
            }
            heldOut[i] = fired[i];
            break;
        case Trigger::DoubleTap:
            if (rising && now - r.lastTapAt <= b.doubleTapWindow) {
                fired[i] = true;
                r.lastTapAt = -1e9;
                r.swallowNextTap = true;
            }
            if (falling) {
                if (r.swallowNextTap) r.swallowNextTap = false;
                else if (duration < b.tapTime) r.lastTapAt = now;
            }
            heldOut[i] = fired[i];
            break;
        case Trigger::Toggle:
            if (rising) {
                r.toggled = !r.toggled;
                fired[i] = r.toggled;
            }
            heldOut[i] = r.toggled;
            break;
        }
        r.down = down;
    }

    // Actions.
    const double dt = m_lastUpdate >= 0.0 ? now - m_lastUpdate : 0.0;
    for (size_t a = 0; a < m_actions.size(); ++a) {
        const ActionDef& def = m_actions[a];
        ActionState s;
        bool anyFired = false, anyHeld = false;
        for (size_t i = 0; i < n; ++i) {
            const Binding& b = m_bindings[i];
            if (b.action != def.id) continue;
            anyFired = anyFired || fired[i];
            anyHeld = anyHeld || heldOut[i];
            if (def.type == ActionType::Button) continue;
            // Analog contribution: Continuous analog bindings give their
            // shaped value (whenever their modifiers are held); everything
            // else gives +-scale while its trigger says held.
            glm::vec2 c(0.0f);
            const bool analogFeed = b.trigger == Trigger::Continuous && b.source.isAnalog();
            if (def.type == ActionType::Axis2D && b.sourceY.valid()) {
                if (enabled[i] && modsHeld[i]) {
                    glm::vec2 v(raw[i], sourceValue(state, b.sourceY));
                    float len = glm::length(v);
                    if (len <= b.deadzone || len < 1e-6f) v = glm::vec2(0.0f);
                    else {
                        float l = (len - b.deadzone) / std::max(1e-6f, 1.0f - b.deadzone);
                        if (b.curve != 1.0f) l = std::pow(std::min(l, 1.0f), b.curve);
                        v = v / len * l;
                    }
                    if (b.invert) v.y = -v.y;
                    c = v * b.scale;
                }
            } else {
                float v = 0.0f;
                if (analogFeed) v = enabled[i] && modsHeld[i] ? shaped(b, raw[i]) : 0.0f;
                else if (heldOut[i]) v = (b.invert ? -1.0f : 1.0f) * b.scale;
                if (def.type == ActionType::Axis2D) c[b.component == 1 ? 1 : 0] = v;
                else c.x = v;
            }
            s.value += c.x;
            s.value2 += c;
            if (glm::length(c) > 1e-5f) anyHeld = true;
        }
        if (def.type == ActionType::Button) s.value = anyHeld ? 1.0f : 0.0f;
        if (def.clamp) {
            s.value = std::clamp(s.value, -1.0f, 1.0f);
            if (glm::length(s.value2) > 1.0f) s.value2 = glm::normalize(s.value2);
        }
        if (def.type == ActionType::Axis2D) s.value = glm::length(s.value2);
        const ActionState& p = m_states[a];
        s.held = anyHeld;
        s.pressed = anyFired || (s.held && !p.held);
        s.released = p.held && !s.held;
        s.heldFor = s.held ? (p.held ? p.heldFor + dt : 0.0) : 0.0;
        m_states[a] = s;
    }
    m_lastUpdate = now;
}

const ActionState& InputMap::state(const std::string& action) const {
    auto it = m_actionIndex.find(action);
    return it == m_actionIndex.end() ? m_none : m_states[it->second];
}

void InputMap::resetStates() {
    for (Runtime& r : m_runtime) r = Runtime{};
    for (ActionState& s : m_states) s = ActionState{};
}

void InputMap::restoreDefaults(const std::string& action) {
    if (action.empty()) {
        m_bindings = m_defaults;
        m_runtime.assign(m_bindings.size(), Runtime{});
        return;
    }
    clearBindings(action);
    for (const Binding& b : m_defaults)
        if (b.action == action) addBinding(b);
}

// ---------------------------------------------------------------- JSON

namespace {
constexpr const char* kKindNames[] = { "none", "key", "mouseButton", "mouseWheel", "mouseMotion", "gamepadButton", "gamepadAxis",
                                       "joyButton", "joyAxis", "joyHat", "gyro", "accel", "touchX", "touchY" };
constexpr const char* kTriggerNames[] = { "press", "release", "hold", "tap", "doubleTap", "continuous", "toggle" };
} // namespace

const char* toString(SourceKind k) { return kKindNames[static_cast<int>(k)]; }
const char* toString(Trigger t) { return kTriggerNames[static_cast<int>(t)]; }
const char* toString(ActionType t) { return t == ActionType::Button ? "button" : t == ActionType::Axis1D ? "axis" : "axis2d"; }

SourceKind sourceKindFromString(const std::string& s) {
    for (size_t i = 0; i < std::size(kKindNames); ++i)
        if (s == kKindNames[i]) return static_cast<SourceKind>(i);
    return SourceKind::None;
}

Trigger triggerFromString(const std::string& s) {
    for (size_t i = 0; i < std::size(kTriggerNames); ++i)
        if (s == kTriggerNames[i]) return static_cast<Trigger>(i);
    return Trigger::Press;
}

nlohmann::json toJson(const InputSource& s) {
    nlohmann::json j{ { "kind", toString(s.kind) }, { "code", s.code } };
    if (s.device) j["device"] = s.device;
    if (s.half) j["half"] = s.half;
    return j;
}

InputSource sourceFromJson(const nlohmann::json& j) {
    InputSource s;
    if (!j.is_object()) return s;
    s.kind = sourceKindFromString(j.value("kind", "none"));
    s.code = j.value("code", 0);
    s.device = j.value("device", 0u);
    s.half = static_cast<int8_t>(std::clamp(j.value("half", 0), -1, 1));
    return s;
}

nlohmann::json InputMap::save() const {
    nlohmann::json list = nlohmann::json::array();
    const Binding d{};
    for (const Binding& b : m_bindings) {
        nlohmann::json j{ { "action", b.action }, { "source", toJson(b.source) }, { "trigger", toString(b.trigger) } };
        if (b.sourceY.valid()) j["sourceY"] = toJson(b.sourceY);
        if (!b.modifiers.empty()) {
            j["modifiers"] = nlohmann::json::array();
            for (const InputSource& m : b.modifiers) j["modifiers"].push_back(toJson(m));
        }
        // Only what differs from the defaults: the file stays readable.
        if (b.component != d.component) j["component"] = b.component;
        if (b.scale != d.scale) j["scale"] = b.scale;
        if (b.deadzone != d.deadzone) j["deadzone"] = b.deadzone;
        if (b.curve != d.curve) j["curve"] = b.curve;
        if (b.invert) j["invert"] = true;
        if (b.threshold != d.threshold) j["threshold"] = b.threshold;
        if (b.holdTime != d.holdTime) j["holdTime"] = b.holdTime;
        if (b.tapTime != d.tapTime) j["tapTime"] = b.tapTime;
        if (b.doubleTapWindow != d.doubleTapWindow) j["doubleTapWindow"] = b.doubleTapWindow;
        list.push_back(std::move(j));
    }
    return nlohmann::json{ { "version", 1 }, { "bindings", std::move(list) } };
}

int InputMap::load(const nlohmann::json& j) {
    if (!j.is_object() || !j.contains("bindings") || !j["bindings"].is_array()) return -1;
    m_bindings.clear();
    m_runtime.clear();
    int dropped = 0;
    const Binding d{};
    for (const nlohmann::json& e : j["bindings"]) {
        if (!e.is_object()) { ++dropped; continue; }
        Binding b;
        b.action = e.value("action", "");
        b.source = sourceFromJson(e.value("source", nlohmann::json()));
        if (!action(b.action) || !b.source.valid()) { ++dropped; continue; }
        if (e.contains("sourceY")) b.sourceY = sourceFromJson(e["sourceY"]);
        if (e.contains("modifiers") && e["modifiers"].is_array())
            for (const nlohmann::json& m : e["modifiers"]) {
                InputSource s = sourceFromJson(m);
                if (s.valid()) b.modifiers.push_back(s);
            }
        b.trigger = triggerFromString(e.value("trigger", "press"));
        b.component = e.value("component", d.component);
        b.scale = e.value("scale", d.scale);
        b.deadzone = std::clamp(e.value("deadzone", d.deadzone), 0.0f, 0.95f);
        b.curve = std::clamp(e.value("curve", d.curve), 0.1f, 10.0f);
        b.invert = e.value("invert", false);
        b.threshold = std::clamp(e.value("threshold", d.threshold), 0.01f, 1.0f);
        b.holdTime = std::max(0.0f, e.value("holdTime", d.holdTime));
        b.tapTime = std::max(0.01f, e.value("tapTime", d.tapTime));
        b.doubleTapWindow = std::max(0.01f, e.value("doubleTapWindow", d.doubleTapWindow));
        addBinding(b);
    }
    return dropped;
}

} // namespace kke
