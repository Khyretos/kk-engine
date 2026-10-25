#include "kke/HumanLook.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace kke {

namespace {

constexpr const char* kCodePrefix = "kh1.";
constexpr uint8_t kCodeVersion = 1;
constexpr int kSteps = 254; // even, so a two-sided slider's 0 and a macro's 0.5 are exact

// FNV-1a over the pack's slider names, in order: two packs with the same
// sliders give the same key, so a share code never lands on another list.
uint16_t packKey(const HumanPack& pack) {
    uint32_t h = 2166136261u;
    for (const HumanModifier& m : pack.modifiers()) {
        for (unsigned char c : m.name) h = (h ^ c) * 16777619u;
        h = (h ^ 0xFFu) * 16777619u;
    }
    return static_cast<uint16_t>(h ^ (h >> 16));
}

uint8_t toByte(float v, float lo, float hi) {
    if (hi <= lo) return 0;
    return static_cast<uint8_t>(std::lround(std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f) * kSteps));
}
float fromByte(uint8_t b, float lo, float hi) { return lo + (hi - lo) * static_cast<float>(std::min<int>(b, kSteps)) / kSteps; }
uint8_t colourByte(float c) { return static_cast<uint8_t>(std::lround(std::clamp(c, 0.0f, 1.0f) * 255.0f)); }

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string base64url(const std::vector<uint8_t>& in) {
    std::string out;
    for (size_t i = 0; i < in.size(); i += 3) {
        const uint32_t n = (uint32_t(in[i]) << 16) | (i + 1 < in.size() ? uint32_t(in[i + 1]) << 8 : 0) |
                           (i + 2 < in.size() ? in[i + 2] : 0);
        out += kB64[(n >> 18) & 63];
        out += kB64[(n >> 12) & 63];
        if (i + 1 < in.size()) out += kB64[(n >> 6) & 63];
        if (i + 2 < in.size()) out += kB64[n & 63];
    }
    return out;
}

bool unbase64url(const std::string& in, std::vector<uint8_t>& out) {
    uint32_t acc = 0;
    int bits = 0;
    for (char c : in) {
        const char* p = std::strchr(kB64, c);
        if (!p || c == '\0') return false;
        acc = (acc << 6) | static_cast<uint32_t>(p - kB64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
        }
    }
    return true;
}

nlohmann::json colourJson(const glm::vec3& c) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", colourByte(c.r), colourByte(c.g), colourByte(c.b));
    return buf;
}

bool colourFrom(const nlohmann::json& j, glm::vec3& out) {
    if (!j.is_string()) return false;
    const std::string s = j.get<std::string>();
    unsigned r, g, b;
    if (s.size() != 7 || s[0] != '#' || std::sscanf(s.c_str() + 1, "%02x%02x%02x", &r, &g, &b) != 3) return false;
    out = glm::vec3(r, g, b) / 255.0f;
    return true;
}

} // namespace

void applyLook(Human& human, const HumanLook& look) { human.setAll(look.shape); }

void takeShape(const Human& human, HumanLook& look) { look.shape = human.values(); }

std::string lookToJson(const HumanLook& look) {
    nlohmann::json j;
    j["format"] = "kk-human";
    j["version"] = 1;
    j["shape"] = nlohmann::json::object();
    for (const auto& [k, v] : look.shape) j["shape"][k] = std::round(v * 10000.0f) / 10000.0f;
    j["skin"] = { { "colour", colourJson(look.skin) } };
    j["eyes"] = { { "iris", colourJson(look.eyes) } };
    j["hair"] = { { "style", look.hair }, { "length", look.hairLength }, { "root", colourJson(look.hairRoot) },
                  { "tip", colourJson(look.hairTip) } };
    return j.dump(2) + "\n";
}

bool lookFromJson(const std::string& text, HumanLook& look, std::string* error) {
    const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    if (j.is_discarded() || !j.is_object()) return fail("not JSON");
    if (j.value("format", std::string()) != "kk-human") return fail("not a kk-human look");
    if (j.value("version", 0) > 1) return fail("made by a newer engine");
    HumanLook out;
    if (auto s = j.find("shape"); s != j.end() && s->is_object())
        for (auto& [k, v] : s->items())
            if (v.is_number()) out.shape[k] = v.get<float>();
    if (auto s = j.find("skin"); s != j.end() && s->is_object() && s->contains("colour")) colourFrom((*s)["colour"], out.skin);
    if (auto e = j.find("eyes"); e != j.end() && e->is_object() && e->contains("iris")) colourFrom((*e)["iris"], out.eyes);
    if (auto h = j.find("hair"); h != j.end() && h->is_object()) {
        out.hair = h->value("style", std::string());
        out.hairLength = h->value("length", 1.0f);
        if (h->contains("root")) colourFrom((*h)["root"], out.hairRoot);
        if (h->contains("tip")) colourFrom((*h)["tip"], out.hairTip);
    }
    look = std::move(out);
    return true;
}

HumanLook lookFromMhm(const std::string& mhm) {
    HumanLook look;
    std::istringstream in(mhm);
    for (std::string line; std::getline(in, line);) {
        std::istringstream words(line);
        std::string key;
        words >> key;
        if (key == "modifier") {
            std::string name;
            float v = 0.0f;
            if (words >> name >> v) look.shape[name] = v;
        } else if (key == "skinColor" || key == "eyeColor") { // older saves: "skinColor 0.8 0.6 0.5"
            glm::vec3 c;
            if (words >> c.r >> c.g >> c.b) (key == "skinColor" ? look.skin : look.eyes) = c;
        }
    }
    return look;
}

HumanLook quantised(const HumanLook& look, const HumanPack& pack) {
    HumanLook out;
    if (!fromShareCode(shareCode(look, pack), pack, out)) return look;
    return out;
}

std::string shareCode(const HumanLook& look, const HumanPack& pack) {
    std::vector<uint8_t> b;
    b.push_back(kCodeVersion);
    const uint16_t key = packKey(pack);
    b.push_back(static_cast<uint8_t>(key & 0xFF));
    b.push_back(static_cast<uint8_t>(key >> 8));
    for (const glm::vec3* c : { &look.skin, &look.eyes, &look.hairRoot, &look.hairTip })
        for (int k = 0; k < 3; ++k) b.push_back(colourByte((*c)[k]));
    b.push_back(colourByte(std::clamp(look.hairLength, 0.0f, 2.0f) * 0.5f));
    const std::string hair = look.hair.substr(0, 32);
    b.push_back(static_cast<uint8_t>(hair.size()));
    b.insert(b.end(), hair.begin(), hair.end());
    // Which sliders are off their default, one bit each in the pack's
    // order, then their values: a typical person is about 200 letters.
    const size_t n = pack.modifiers().size();
    const size_t bits = b.size();
    b.resize(bits + (n + 7) / 8, 0);
    for (size_t i = 0; i < n; ++i) {
        const HumanModifier& m = pack.modifiers()[i];
        auto it = look.shape.find(m.name);
        if (it == look.shape.end()) continue;
        const uint8_t q = toByte(it->second, m.min, m.max);
        if (q == toByte(m.defaultValue, m.min, m.max)) continue; // reads back as the default: one code per look
        b[bits + i / 8] |= static_cast<uint8_t>(1u << (i % 8));
        b.push_back(q);
    }
    return kCodePrefix + base64url(b);
}

bool fromShareCode(const std::string& code, const HumanPack& pack, HumanLook& look, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    if (code.rfind(kCodePrefix, 0) != 0) return fail("not a character share code");
    std::vector<uint8_t> b;
    if (!unbase64url(code.substr(4), b)) return fail("the code has letters that don't belong in it");
    size_t p = 0;
    auto take = [&](size_t n) { return p + n <= b.size() ? (p += n, true) : false; };
    if (!take(3) || b[0] != kCodeVersion) return fail("made by a different engine version");
    if ((b[1] | (b[2] << 8)) != packKey(pack)) return fail("made with a different human pack");
    HumanLook out;
    if (!take(13)) return fail("the code is cut short");
    glm::vec3* colours[] = { &out.skin, &out.eyes, &out.hairRoot, &out.hairTip };
    for (int c = 0; c < 4; ++c)
        for (int k = 0; k < 3; ++k) (*colours[c])[k] = b[3 + c * 3 + k] / 255.0f;
    out.hairLength = b[15] / 255.0f * 2.0f;
    if (!take(1)) return fail("the code is cut short");
    const size_t hairLen = b[p - 1];
    if (!take(hairLen)) return fail("the code is cut short");
    out.hair.assign(b.begin() + static_cast<long>(p - hairLen), b.begin() + static_cast<long>(p));
    const size_t n = pack.modifiers().size();
    if (!take((n + 7) / 8)) return fail("the code is cut short");
    const size_t bits = p - (n + 7) / 8;
    for (size_t i = 0; i < n; ++i) {
        if (!(b[bits + i / 8] & (1u << (i % 8)))) continue;
        if (!take(1)) return fail("the code is cut short");
        const HumanModifier& m = pack.modifiers()[i];
        out.shape[m.name] = fromByte(b[p - 1], m.min, m.max);
    }
    if (p != b.size()) return fail("the code is longer than it should be");
    look = std::move(out);
    return true;
}

} // namespace kke
