#include "kke/FaceRig.h"

#include "kke/Bidi.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace kke {

namespace {

using Pairs = std::vector<std::pair<std::string, float>>;
struct Recipes {
    const char* expression;
    float rate; // how fast it eases, per second
    std::vector<Pairs> options;
};

// What each expression is called in the common rigs, best first. Apple ARKit's 52 blend shapes,
// VRM's presets, VRoid's Fcl_* shapes, the Oculus/Ready Player Me visemes, and plain names.
const std::vector<Recipes>& catalog() {
    static const std::vector<Recipes> list = {
        { "happy", 7.0f, { { { "happy", 1 } }, { { "joy", 1 } }, { { "Fcl_ALL_Joy", 1 } }, { { "mouthSmileLeft", 1 }, { "mouthSmileRight", 1 }, { "cheekSquintLeft", 0.5f }, { "cheekSquintRight", 0.5f } }, { { "smile", 1 } } } },
        { "angry", 7.0f, { { { "angry", 1 } }, { { "Fcl_ALL_Angry", 1 } }, { { "browDownLeft", 1 }, { "browDownRight", 1 }, { "mouthFrownLeft", 0.4f }, { "mouthFrownRight", 0.4f }, { "noseSneerLeft", 0.5f }, { "noseSneerRight", 0.5f } } } },
        { "sad", 7.0f, { { { "sad", 1 } }, { { "sorrow", 1 } }, { { "Fcl_ALL_Sorrow", 1 } }, { { "browInnerUp", 1 }, { "mouthFrownLeft", 0.7f }, { "mouthFrownRight", 0.7f } } } },
        { "surprised", 12.0f, { { { "surprised", 1 } }, { { "Fcl_ALL_Surprised", 1 } }, { { "eyeWideLeft", 1 }, { "eyeWideRight", 1 }, { "browInnerUp", 1 }, { "browOuterUpLeft", 1 }, { "browOuterUpRight", 1 }, { "jawOpen", 0.4f } } } },
        { "relaxed", 5.0f, { { { "relaxed", 1 } }, { { "fun", 1 } }, { { "Fcl_ALL_Fun", 1 } }, { { "mouthSmileLeft", 0.3f }, { "mouthSmileRight", 0.3f }, { "eyeSquintLeft", 0.4f }, { "eyeSquintRight", 0.4f } } } },
        { "blink", 30.0f, { { { "blink", 1 } }, { { "eyeBlinkLeft", 1 }, { "eyeBlinkRight", 1 } }, { { "Fcl_EYE_Close", 1 } }, { { "eyesClosed", 1 } }, { { "eyeClose", 1 } } } },
        { "blinkLeft", 30.0f, { { { "blinkLeft", 1 } }, { { "eyeBlinkLeft", 1 } }, { { "Fcl_EYE_Close_L", 1 } }, { { "blink_L", 1 } } } },
        { "blinkRight", 30.0f, { { { "blinkRight", 1 } }, { { "eyeBlinkRight", 1 } }, { { "Fcl_EYE_Close_R", 1 } }, { { "blink_R", 1 } } } },
        { "aa", 28.0f, { { { "aa", 1 } }, { { "viseme_aa", 1 } }, { { "Fcl_MTH_A", 1 } }, { { "jawOpen", 1 } } } },
        { "ih", 28.0f, { { { "ih", 1 } }, { { "viseme_I", 1 } }, { { "Fcl_MTH_I", 1 } }, { { "mouthSmileLeft", 0.5f }, { "mouthSmileRight", 0.5f }, { "jawOpen", 0.2f } } } },
        { "ou", 28.0f, { { { "ou", 1 } }, { { "viseme_U", 1 } }, { { "Fcl_MTH_U", 1 } }, { { "mouthPucker", 1 }, { "jawOpen", 0.2f } } } },
        { "ee", 28.0f, { { { "ee", 1 } }, { { "viseme_E", 1 } }, { { "Fcl_MTH_E", 1 } }, { { "mouthStretchLeft", 0.6f }, { "mouthStretchRight", 0.6f }, { "jawOpen", 0.3f } } } },
        { "oh", 28.0f, { { { "oh", 1 } }, { { "viseme_O", 1 } }, { { "Fcl_MTH_O", 1 } }, { { "mouthFunnel", 1 }, { "jawOpen", 0.4f } } } },
        { "sil", 28.0f, { { { "viseme_sil", 1 } }, { { "mouthClose", 1 } } } },
    };
    return list;
}

const char* const kVowelOrder[] = { "aa", "oh", "ee", "ih", "ou" };

} // namespace

std::string FaceRig::key(const std::string& name) {
    std::string out;
    for (unsigned char c : name)
        if (c != '_' && c != '-' && c != ' ' && c != '.') out.push_back(static_cast<char>(std::tolower(c)));
    return out;
}

std::string FaceRig::resolve(const std::string& wanted) const {
    auto it = m_byKey.find(key(wanted));
    return it == m_byKey.end() ? std::string() : it->second;
}

float FaceRig::random01() {
    m_rng = m_rng * 1664525u + 1013904223u;
    return static_cast<float>((m_rng >> 8) & 0xFFFFu) / 65535.0f;
}

void FaceRig::bind(const std::vector<std::string>& morphNames) {
    m_names = morphNames;
    m_byKey.clear();
    for (const std::string& n : m_names) m_byKey.emplace(key(n), n);
    m_expressions.clear();
    m_raw.clear();
    m_weights.clear();
    for (const Recipes& r : catalog()) {
        Expression e;
        e.rate = r.rate;
        for (const Pairs& option : r.options) {
            Recipe resolved;
            for (const auto& [name, weight] : option)
                if (const std::string have = resolve(name); !have.empty()) resolved.morphs.emplace_back(have, weight);
            // A recipe works when the model has all of its shapes; a pair like smile left and right needs both.
            if (resolved.morphs.size() == option.size()) {
                e.recipe = std::move(resolved);
                break;
            }
        }
        m_expressions[r.expression] = std::move(e);
    }
    ++m_version;
}

bool FaceRig::canDo(const std::string& expression) const {
    const auto it = m_expressions.find(expression);
    return it != m_expressions.end() && !it->second.recipe.morphs.empty();
}

std::vector<std::string> FaceRig::expressions() const {
    std::vector<std::string> out;
    for (const Recipes& r : catalog())
        if (canDo(r.expression)) out.push_back(r.expression);
    return out;
}

bool FaceRig::setExpression(const std::string& name, float weight) {
    if (name == "neutral") {
        for (auto& [n, e] : m_expressions)
            if (n != "blink" && n != "blinkLeft" && n != "blinkRight" && !(n == "aa" || n == "ih" || n == "ou" || n == "ee" || n == "oh" || n == "sil")) e.target = 0.0f;
        return true;
    }
    auto it = m_expressions.find(name);
    if (it == m_expressions.end() || it->second.recipe.morphs.empty()) return false;
    it->second.target = std::clamp(weight, 0.0f, 1.0f);
    return true;
}

bool FaceRig::setMorph(const std::string& name, float weight) {
    const std::string have = resolve(name);
    if (have.empty()) return false;
    m_raw[have].first = std::clamp(weight, 0.0f, 1.0f);
    return true;
}

void FaceRig::clear() {
    for (auto& [n, e] : m_expressions) e.target = 0.0f;
    for (auto& [n, r] : m_raw) r.first = 0.0f;
    stopSpeaking();
}

std::vector<FaceRig::VisemeKey> FaceRig::lipSyncFromText(const std::string& text, float seconds) {
    const std::u32string chars = bidi::decode(text);
    std::vector<VisemeKey> keys;
    if (chars.empty() || seconds <= 0.0f) return keys;
    const float step = seconds / static_cast<float>(chars.size());
    auto vowel = [](char32_t c) -> const char* {
        switch (c) {
        case U'a': case U'A': case 0xE0: case 0xE1: case 0xE2: case 0xE4: case 0xE3: case 0xE5: return "aa";
        case U'e': case U'E': case 0xE8: case 0xE9: case 0xEA: case 0xEB: return "ee";
        case U'i': case U'I': case U'y': case 0xEC: case 0xED: case 0xEE: case 0xEF: return "ih";
        case U'o': case U'O': case 0xF2: case 0xF3: case 0xF4: case 0xF6: case 0xF5: return "oh";
        case U'u': case U'U': case 0xF9: case 0xFA: case 0xFB: case 0xFC: case U'w': return "ou";
        default: return nullptr;
        }
    };
    std::string held = "sil";
    for (size_t i = 0; i < chars.size(); ++i) {
        const char32_t c = chars[i];
        const char* v = vowel(c);
        std::string next = held;
        float weight = 1.0f;
        if (v) {
            next = v;
        } else if (c == U'm' || c == U'M' || c == U'b' || c == U'B' || c == U'p' || c == U'P' || c == U' ' || c == U',' || c == U'.' || c == U'!' || c == U'?') {
            next = "sil"; // lips together, or a pause
        } else {
            weight = 0.0f; // another consonant: the mouth stays where it was, a little less open
            next = held;
        }
        if (next != held || v) {
            keys.push_back({ step * static_cast<float>(i), next, next == "sil" ? 0.0f : weight });
            held = next;
        }
    }
    keys.push_back({ seconds, "sil", 0.0f });
    return keys;
}

void FaceRig::say(std::vector<VisemeKey> keys) {
    std::sort(keys.begin(), keys.end(), [](const VisemeKey& a, const VisemeKey& b) { return a.time < b.time; });
    m_keys = std::move(keys);
    m_speechTime = 0.0f;
    m_speaking = !m_keys.empty();
}

void FaceRig::stopSpeaking() {
    m_keys.clear();
    m_speaking = false;
    m_level = 0.0f;
    for (const char* v : { "aa", "ih", "ou", "ee", "oh", "sil" }) {
        auto it = m_expressions.find(v);
        if (it != m_expressions.end()) it->second.target = 0.0f;
    }
}

void FaceRig::update(float dt) {
    dt = std::clamp(dt, 0.0f, 0.1f);
    // A line of speech: the mouth shape of the key we are in.
    if (!m_keys.empty()) {
        m_speechTime += dt;
        const VisemeKey* current = nullptr;
        for (const VisemeKey& k : m_keys)
            if (k.time <= m_speechTime) current = &k;
        for (const char* v : { "aa", "ih", "ou", "ee", "oh", "sil" }) {
            auto it = m_expressions.find(v);
            if (it != m_expressions.end()) it->second.target = current && current->viseme == v ? current->weight : 0.0f;
        }
        if (m_speechTime > m_keys.back().time + 0.15f) stopSpeaking();
    }
    // A voice's loudness: the jaw follows the level, the vowel changes about every 110 ms while it talks.
    if (m_keys.empty()) {
        m_levelSmooth += (m_level - m_levelSmooth) * std::min(1.0f, dt * 18.0f);
        const bool talking = m_levelSmooth > 0.03f;
        if (talking) {
            m_vowelClock += dt;
            if (m_vowelClock > 0.11f) {
                m_vowelClock = 0.0f;
                m_vowel = (m_vowel + 1 + static_cast<int>(random01() * 3.0f)) % 5;
            }
            for (int i = 0; i < 5; ++i)
                if (auto it = m_expressions.find(kVowelOrder[i]); it != m_expressions.end())
                    it->second.target = i == m_vowel ? std::min(1.0f, m_levelSmooth * 1.2f) : 0.0f;
        } else if (m_voiceActive) { // the voice just stopped: the mouth closes
            for (int i = 0; i < 5; ++i)
                if (auto it = m_expressions.find(kVowelOrder[i]); it != m_expressions.end()) it->second.target = 0.0f;
        }
        m_voiceActive = talking;
        m_speaking = talking;
    }

    // Blinking: a short close and open, at random intervals, unless the eyes are held shut on purpose.
    float blink = 0.0f;
    if (m_autoBlink) {
        const bool held = m_expressions.count("blink") && m_expressions["blink"].target > 0.5f;
        if (m_blinkPhase < 0.0f && !held) {
            m_blinkWait -= dt;
            if (m_blinkWait <= 0.0f) {
                m_blinkPhase = 0.0f;
                m_blinkWait = m_blinkFrom + (m_blinkTo - m_blinkFrom) * random01();
            }
        }
    }
    if (m_blinkPhase >= 0.0f) {
        m_blinkPhase += dt;
        constexpr float kClose = 0.06f, kHold = 0.04f, kOpen = 0.12f;
        if (m_blinkPhase < kClose) blink = m_blinkPhase / kClose;
        else if (m_blinkPhase < kClose + kHold) blink = 1.0f;
        else if (m_blinkPhase < kClose + kHold + kOpen) blink = 1.0f - (m_blinkPhase - kClose - kHold) / kOpen;
        else m_blinkPhase = -1.0f;
    }

    // Ease every expression to its target, then sum the recipes (a morph never goes past 1).
    std::map<std::string, float> sum;
    for (auto& [name, e] : m_expressions) {
        if (e.recipe.morphs.empty()) continue;
        const float t = name == "blink" ? std::max(e.target, blink) : e.target;
        e.current += (t - e.current) * std::min(1.0f, dt * e.rate);
        if (std::fabs(t - e.current) < 0.002f) e.current = t;
        if (e.current <= 0.0f) continue;
        for (const auto& [morph, w] : e.recipe.morphs) sum[morph] += e.current * w;
    }
    for (auto& [morph, r] : m_raw) {
        r.second += (r.first - r.second) * std::min(1.0f, dt * 12.0f);
        if (std::fabs(r.first - r.second) < 0.002f) r.second = r.first;
        if (r.second > 0.0f) sum[morph] += r.second;
    }
    for (auto& [morph, w] : sum) w = std::min(w, 1.0f);
    if (sum != m_weights) {
        m_weights = std::move(sum);
        ++m_version;
    }
}

} // namespace kke
