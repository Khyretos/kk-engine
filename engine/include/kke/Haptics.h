#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace kke {

// Haptics (ENG-F48, docs/INPUT.md "Haptics"): what each player's
// controller feels, mixed from many sources at once. Pure logic: InputModule
// sends the mix to the player's pads each frame (SDL rumble, and the
// trigger motors where the pad has them).
//
// Two kinds of source:
//  - one-shots: a pulse with a strength on the low (heavy) and high
//    (light, buzzy) motors and on the triggers, fading out over its
//    length; presets name the usual ones ("impact", "explosion",
//    "footstep", "heartbeat"...);
//  - levels: something steady the game updates every frame or when it
//    changes ("engine" at 0.4, "rain" at 0.1), kept until set to 0.
// The mix adds them up and clips each motor to 1, then scales by the
// player's vibration setting (0 = off, accessibility).
struct HapticPulse {
    float low = 0.0f, high = 0.0f;           // 0..1, the two body motors
    float leftTrigger = 0.0f, rightTrigger = 0.0f; // 0..1, trigger motors (Xbox One and later)
    float seconds = 0.2f;
    float attack = 0.0f;  // seconds to reach full strength
    float decay = 1.0f;   // share of `seconds` spent fading out at the end (0 = cut off, 1 = fade all the way)
    float rate = 0.0f;    // Hz: > 0 pulses on and off (heartbeat, alarm), 0 = steady
};

// The named presets, scaled by `strength` (0..1+). Unknown names give
// nothing and false.
bool hapticPreset(const std::string& name, float strength, HapticPulse& out);
// The preset names, for docs and the Lua API's error message.
const std::vector<std::string>& hapticPresetNames();

struct HapticMix {
    float low = 0.0f, high = 0.0f, leftTrigger = 0.0f, rightTrigger = 0.0f;
    bool operator==(const HapticMix& o) const {
        return low == o.low && high == o.high && leftTrigger == o.leftTrigger && rightTrigger == o.rightTrigger;
    }
};

class HapticMixer {
public:
    void play(const HapticPulse& pulse);
    // A steady source: `level` 0..1 scales `shape` (its motors); 0 removes it.
    void setLevel(const std::string& name, float level, const HapticPulse& shape);
    void stop(); // every pulse and level
    void setScale(float s) { m_scale = s < 0.0f ? 0.0f : s; } // the player's vibration setting
    float scale() const { return m_scale; }

    // Advances by dt seconds and returns what the motors should do now.
    HapticMix update(float dt);
    size_t active() const { return m_pulses.size() + m_levels.size(); }

private:
    struct Running { HapticPulse pulse; float t = 0.0f; };
    struct Level { std::string name; float level = 0.0f; HapticPulse shape; float t = 0.0f; };
    std::vector<Running> m_pulses;
    std::vector<Level> m_levels;
    float m_scale = 1.0f;
};

// How strong a pulse is `t` seconds in (0..1): attack, hold, decay, and
// the on/off rate. 0 once it has ended.
float hapticEnvelope(const HapticPulse& p, float t);

} // namespace kke
