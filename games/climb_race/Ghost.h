#pragma once

#include "NetRace.h"

#include "kke/net/Protocol.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace climb_race {

// Time trial (DESIGN.md): a run recorded as the pose every `step` seconds
// (the same pose online play sends, kke::net::NetPlayerState, with the
// feet in the mountain's own space, not the world's), so it can be played
// back on any face as a ghost. The best run on each mountain is saved in
// climb_race_ghosts/<mountain>.ghost.
class Ghost {
public:
    static constexpr float kStep = 0.05f; // s between samples (20 a second)

    // Recording: call every frame with the race clock; a pose is kept every kStep.
    void record(float time, const netrace::Pose& wallSpacePose);
    void clear() { m_samples.clear(); m_time = 0.0f; }
    bool empty() const { return m_samples.empty(); }
    // The run's length (s): the time it topped out in.
    float time() const { return m_time; }
    void setTime(float t) { m_time = t; }
    std::string name;             // who climbed it

    // Where it is `t` seconds after the start (feet in wall space; the
    // feet move smoothly between samples, the rest is the nearest sample).
    netrace::Pose at(float t) const;

    std::vector<uint8_t> encode() const;
    static std::optional<Ghost> decode(const std::vector<uint8_t>& bytes);
    bool save(const std::filesystem::path& file, std::string* error = nullptr) const;
    static std::optional<Ghost> load(const std::filesystem::path& file, std::string* error = nullptr);

private:
    std::vector<kke::net::NetPlayerState> m_samples;
    float m_time = 0.0f;
};

} // namespace climb_race
