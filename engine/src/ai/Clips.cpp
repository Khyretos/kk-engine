#include "kke/ai/Clips.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>

namespace kke::ai {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// The clip's own name: "Armature|WalkSlow" -> "WalkSlow".
std::string shortName(const std::string& name) {
    const size_t bar = name.rfind('|');
    return bar == std::string::npos ? name : name.substr(bar + 1);
}

// `want` (lower case) as a word of `name`: at its start, after a non-letter
// or starting a capital ("Sleep_Idle", "WalkSlow"), so "eat" isn't found
// in "Death".
bool hasWord(const std::string& name, const char* want) {
    const std::string low = lower(name);
    for (size_t p = low.find(want); p != std::string::npos; p = low.find(want, p + 1)) {
        if (p == 0 || !std::isalpha(static_cast<unsigned char>(name[p - 1])) || std::isupper(static_cast<unsigned char>(name[p])))
            return true;
    }
    return false;
}

int findClip(const ModelData& model, std::initializer_list<const char*> names) {
    for (const char* want : names) {
        for (size_t i = 0; i < model.animations.size(); ++i)
            if (hasWord(shortName(model.animations[i].name), want)) return static_cast<int>(i);
    }
    return -1;
}

} // namespace

ClipChoice clipForAnim(const ModelData& model, const std::string& anim) {
    if (model.animations.empty()) return {};
    for (size_t i = 0; i < model.animations.size(); ++i)
        if (model.animations[i].name == anim) return { static_cast<int>(i), 1.0f };

    const int idle = std::max(0, findClip(model, { "idle", "standing" }));
    const int walk = findClip(model, { "walkslow", "walking", "walk", "trot" });
    const int run = findClip(model, { "running", "run", "gallop" });
    const int jump = findClip(model, { "jump" });
    auto pick = [&](int found, int fallback) { return ClipChoice{ found >= 0 ? found : fallback, 1.0f }; };

    if (anim == "walk") {
        if (walk >= 0) return { walk, 1.0f };
        if (jump >= 0) return { jump, 0.7f };
        return pick(run, idle);
    }
    if (anim == "run") {
        if (run >= 0) return { run, 1.0f };
        if (jump >= 0) return { jump, 1.0f };
        return pick(walk, idle);
    }
    if (anim == "eat") return pick(findClip(model, { "eat", "graze" }), idle);
    if (anim == "drink") return pick(findClip(model, { "drink", "eat" }), idle);
    if (anim == "rest") return pick(findClip(model, { "sleep", "rest", "lie", "sit" }), idle);
    if (anim == "attack") return pick(findClip(model, { "attack", "bite", "headbutt", "kick" }), idle);
    if (anim == "alert") return pick(findClip(model, { "alert", "tailwag", "look" }), idle);
    if (anim == "sniff") return pick(findClip(model, { "sniff", "smell" }), idle);
    if (anim == "bark") return pick(findClip(model, { "bark", "howl", "call" }), idle);
    return pick(findClip(model, { lower(anim).c_str() }), idle);
}

} // namespace kke::ai
