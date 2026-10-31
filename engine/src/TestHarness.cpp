#include "kke/TestHarness.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>

namespace kke::harness {

namespace {

constexpr const char* kHeader = "kke-input-tape 1";

// Text in a tape line: spaces, backslashes and newlines escaped so the
// line splits on spaces.
std::string escapeText(const char* s) {
    std::string out;
    for (const char* p = s ? s : ""; *p; ++p) {
        if (*p == ' ') out += "\\s";
        else if (*p == '\\') out += "\\\\";
        else if (*p == '\n') out += "\\n";
        else out += *p;
    }
    return out.empty() ? "\\0" : out;
}

std::string unescapeText(const std::string& s) {
    if (s == "\\0") return {};
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char c = s[++i];
            out += c == 's' ? ' ' : c == 'n' ? '\n' : c;
        } else {
            out += s[i];
        }
    }
    return out;
}

} // namespace

bool isTapeEvent(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_MOTION: return true;
    default: return false;
    }
}

std::string encodeEvent(uint64_t frame, const SDL_Event& e) {
    if (!isTapeEvent(e)) return {};
    std::ostringstream o;
    o.precision(9);
    o << frame << ' ';
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        o << (e.type == SDL_EVENT_KEY_DOWN ? "keydown " : "keyup ") << static_cast<int>(e.key.scancode) << ' ' << e.key.key << ' ' << e.key.mod << ' '
          << (e.key.repeat ? 1 : 0);
        break;
    case SDL_EVENT_TEXT_INPUT: o << "text " << escapeText(e.text.text); break;
    case SDL_EVENT_MOUSE_MOTION:
        o << "motion " << e.motion.x << ' ' << e.motion.y << ' ' << e.motion.xrel << ' ' << e.motion.yrel << ' ' << e.motion.state;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        o << (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? "mousedown " : "mouseup ") << static_cast<int>(e.button.button) << ' '
          << static_cast<int>(e.button.clicks) << ' ' << e.button.x << ' ' << e.button.y;
        break;
    case SDL_EVENT_MOUSE_WHEEL: o << "wheel " << e.wheel.x << ' ' << e.wheel.y << ' ' << e.wheel.mouse_x << ' ' << e.wheel.mouse_y; break;
    default: // fingers
        o << (e.type == SDL_EVENT_FINGER_DOWN ? "fingerdown " : e.type == SDL_EVENT_FINGER_UP ? "fingerup " : "fingermove ") << e.tfinger.fingerID << ' '
          << e.tfinger.x << ' ' << e.tfinger.y << ' ' << e.tfinger.dx << ' ' << e.tfinger.dy << ' ' << e.tfinger.pressure;
        break;
    }
    return o.str();
}

std::optional<TapeEvent> decodeEvent(const std::string& line, std::string* error) {
    auto fail = [&](const std::string& why) -> std::optional<TapeEvent> {
        if (error) *error = why + ": '" + line + "'";
        return std::nullopt;
    };
    std::istringstream in(line);
    TapeEvent t;
    std::string kind;
    if (!(in >> t.frame >> kind)) return fail("expected '<frame> <kind>'");
    SDL_Event& e = t.event;
    bool ok = false;
    if (kind == "keydown" || kind == "keyup") {
        int scancode = 0, repeat = 0;
        unsigned key = 0, mod = 0;
        ok = static_cast<bool>(in >> scancode >> key >> mod >> repeat);
        e.type = kind == "keydown" ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.scancode = static_cast<SDL_Scancode>(scancode);
        e.key.key = key;
        e.key.mod = static_cast<SDL_Keymod>(mod);
        e.key.repeat = repeat != 0;
        e.key.down = e.type == SDL_EVENT_KEY_DOWN;
    } else if (kind == "text") {
        std::string word;
        ok = static_cast<bool>(in >> word);
        e.type = SDL_EVENT_TEXT_INPUT;
        t.text = unescapeText(word);
    } else if (kind == "motion") {
        ok = static_cast<bool>(in >> e.motion.x >> e.motion.y >> e.motion.xrel >> e.motion.yrel >> e.motion.state);
        e.type = SDL_EVENT_MOUSE_MOTION;
    } else if (kind == "mousedown" || kind == "mouseup") {
        int button = 0, clicks = 0;
        ok = static_cast<bool>(in >> button >> clicks >> e.button.x >> e.button.y);
        e.type = kind == "mousedown" ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.button = static_cast<Uint8>(button);
        e.button.clicks = static_cast<Uint8>(clicks);
        e.button.down = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    } else if (kind == "wheel") {
        ok = static_cast<bool>(in >> e.wheel.x >> e.wheel.y >> e.wheel.mouse_x >> e.wheel.mouse_y);
        e.type = SDL_EVENT_MOUSE_WHEEL;
        e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    } else if (kind == "fingerdown" || kind == "fingerup" || kind == "fingermove") {
        ok = static_cast<bool>(in >> e.tfinger.fingerID >> e.tfinger.x >> e.tfinger.y >> e.tfinger.dx >> e.tfinger.dy >> e.tfinger.pressure);
        e.type = kind == "fingerdown" ? SDL_EVENT_FINGER_DOWN : kind == "fingerup" ? SDL_EVENT_FINGER_UP : SDL_EVENT_FINGER_MOTION;
    } else {
        return fail("unknown event kind '" + kind + "'");
    }
    if (!ok) return fail("missing fields for '" + kind + "'");
    return t;
}

void InputTape::record(uint64_t frame, const SDL_Event& e) {
    if (!isTapeEvent(e)) return;
    TapeEvent t;
    t.frame = frame;
    t.event = e;
    if (e.type == SDL_EVENT_TEXT_INPUT) {
        t.text = e.text.text ? e.text.text : "";
        t.event.text.text = nullptr; // SDL's copy is freed after the frame
    }
    m_events.push_back(std::move(t));
}

std::string InputTape::serialize() const {
    std::string out = std::string(kHeader) + "\n";
    for (const TapeEvent& t : m_events) {
        SDL_Event e = t.event;
        if (e.type == SDL_EVENT_TEXT_INPUT) e.text.text = t.text.c_str();
        out += encodeEvent(t.frame, e) + "\n";
    }
    return out;
}

bool InputTape::parse(const std::string& text, std::string* error) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line.rfind(kHeader, 0) != 0) {
        if (error) *error = std::string("not an input tape (first line must be '") + kHeader + "')";
        return false;
    }
    std::vector<TapeEvent> events;
    int lineNo = 1;
    while (std::getline(in, line)) {
        ++lineNo;
        if (line.empty() || line[0] == '#') continue;
        std::string why;
        std::optional<TapeEvent> t = decodeEvent(line, &why);
        if (!t) {
            if (error) *error = "line " + std::to_string(lineNo) + ": " + why;
            return false;
        }
        events.push_back(std::move(*t));
    }
    // Hand-written tapes may be out of order; equal frames keep their order.
    std::stable_sort(events.begin(), events.end(), [](const TapeEvent& a, const TapeEvent& b) { return a.frame < b.frame; });
    m_events = std::move(events);
    m_next = 0;
    return true;
}

bool InputTape::save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    f << serialize();
    return static_cast<bool>(f);
}

bool InputTape::load(const std::string& path, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "can't open " + path;
        return false;
    }
    std::stringstream s;
    s << f.rdbuf();
    return parse(s.str(), error);
}

std::vector<TapeEvent*> InputTape::due(uint64_t frame) {
    std::vector<TapeEvent*> out;
    while (m_next < m_events.size() && m_events[m_next].frame <= frame) {
        TapeEvent& t = m_events[m_next++];
        if (t.event.type == SDL_EVENT_TEXT_INPUT) t.event.text.text = t.text.c_str();
        out.push_back(&t);
    }
    return out;
}

std::optional<std::vector<ShotPlan>> parseShotPlan(const std::string& spec, std::string* error) {
    std::vector<ShotPlan> plan;
    std::stringstream list(spec);
    for (std::string item; std::getline(list, item, ',');) {
        if (item.empty()) continue;
        const size_t colon = item.find(':');
        char* end = nullptr;
        const unsigned long long frame = colon == std::string::npos ? 0 : std::strtoull(item.c_str(), &end, 10);
        if (colon == std::string::npos || colon == 0 || end != item.c_str() + colon || colon + 1 >= item.size()) {
            if (error) *error = "expected '<frame>:<file>', got '" + item + "'";
            return std::nullopt;
        }
        plan.push_back({ frame, item.substr(colon + 1) });
    }
    std::stable_sort(plan.begin(), plan.end(), [](const ShotPlan& a, const ShotPlan& b) { return a.frame < b.frame; });
    return plan;
}

std::optional<float> parseFixedDt(const std::string& spec) {
    char* end = nullptr;
    const double v = std::strtod(spec.c_str(), &end);
    if (end == spec.c_str() || *end != '\0' || !std::isfinite(v) || v <= 0.0) return std::nullopt;
    return static_cast<float>(v > 1.0 ? 1.0 / v : v);
}

ImageDiff compareImages(const std::vector<uint8_t>& a, uint32_t aw, uint32_t ah, const std::vector<uint8_t>& b, uint32_t bw, uint32_t bh, int tolerance) {
    ImageDiff d;
    d.width = aw;
    d.height = ah;
    const size_t pixels = static_cast<size_t>(aw) * ah;
    if (aw != bw || ah != bh || a.size() < pixels * 4 || b.size() < pixels * 4) {
        d.sameSize = false;
        d.differingFraction = 1.0;
        return d;
    }
    double sumAbs = 0.0, sumSq = 0.0;
    size_t differing = 0;
    for (size_t p = 0; p < pixels; ++p) {
        bool over = false;
        for (int c = 0; c < 3; ++c) {
            const int diff = std::abs(static_cast<int>(a[p * 4 + c]) - static_cast<int>(b[p * 4 + c]));
            sumAbs += diff;
            sumSq += static_cast<double>(diff) * diff;
            d.maxAbs = std::max(d.maxAbs, diff);
            over = over || diff > tolerance;
        }
        if (over) ++differing;
    }
    const double samples = static_cast<double>(pixels) * 3.0;
    d.meanAbs = pixels ? sumAbs / samples : 0.0;
    d.differingFraction = pixels ? static_cast<double>(differing) / static_cast<double>(pixels) : 0.0;
    const double mse = pixels ? sumSq / samples : 0.0;
    d.psnr = mse > 0.0 ? 10.0 * std::log10(255.0 * 255.0 / mse) : std::numeric_limits<double>::infinity();
    return d;
}

bool imagesMatch(const ImageDiff& d, double maxFraction) { return d.sameSize && d.differingFraction <= maxFraction; }

std::vector<uint8_t> diffImage(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, uint32_t w, uint32_t h, int tolerance) {
    const size_t pixels = static_cast<size_t>(w) * h;
    if (a.size() < pixels * 4 || b.size() < pixels * 4) return {};
    std::vector<uint8_t> out(pixels * 4);
    for (size_t p = 0; p < pixels; ++p) {
        bool over = false;
        for (int c = 0; c < 3; ++c) over = over || std::abs(static_cast<int>(a[p * 4 + c]) - static_cast<int>(b[p * 4 + c])) > tolerance;
        for (int c = 0; c < 3; ++c) out[p * 4 + c] = over ? (c == 1 ? 0 : 255) : static_cast<uint8_t>(a[p * 4 + c] / 4);
        out[p * 4 + 3] = 255;
    }
    return out;
}

} // namespace kke::harness
