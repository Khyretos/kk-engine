#include "Ghost.h"

#include "kke/net/BitStream.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <system_error>

namespace climb_race {

namespace {

constexpr uint32_t kMagic = 0x4b47u; // "KG"
constexpr uint32_t kVersion = 1;
constexpr uint32_t kMaxSamples = 20u * 60u * 30u; // half an hour
constexpr size_t kMaxName = 32;

template <typename Stream> void serializeGhost(Stream& s, uint32_t& magic, uint32_t& version, std::string& name, float& time,
                                               std::vector<kke::net::NetPlayerState>& samples) {
    s.integer(magic, 0, 0xffff);
    s.integer(version, 0, 255);
    s.string(name, kMaxName);
    s.real(time, 0.0f, 3600.0f, 0.001f);
    uint32_t n = static_cast<uint32_t>(std::min<size_t>(samples.size(), kMaxSamples));
    s.integer(n, 0, kMaxSamples);
    if constexpr (Stream::kReading) samples.resize(s.ok() ? n : 0);
    for (size_t i = 0; i < n && i < samples.size(); ++i) kke::net::serialize(s, samples[i]);
}

} // namespace

void Ghost::record(float time, const netrace::Pose& pose) {
    const size_t want = static_cast<size_t>(std::floor(std::max(0.0f, time) / kStep)) + 1;
    if (m_samples.size() >= want || m_samples.size() >= kMaxSamples) return;
    const kke::net::NetPlayerState s = netrace::toState(pose);
    while (m_samples.size() < want) m_samples.push_back(s); // a slow frame fills the gap
    m_time = std::max(m_time, time);
}

netrace::Pose Ghost::at(float t) const {
    if (m_samples.empty()) return {};
    const float f = std::clamp(t / kStep, 0.0f, static_cast<float>(m_samples.size() - 1));
    const size_t i = static_cast<size_t>(f);
    const size_t j = std::min(i + 1, m_samples.size() - 1);
    const float k = f - static_cast<float>(i);
    netrace::Pose p = netrace::fromState(m_samples[k < 0.5f ? i : j]);
    // Smooth feet between samples; the limbs ride along (they're relative to them).
    const glm::vec3 feet = glm::mix(m_samples[i].position, m_samples[j].position, k);
    const glm::vec3 shift = feet - p.feet;
    p.feet = feet;
    for (int h = 0; h < 2; ++h) {
        p.grip[h] += shift;
        p.foot[h] += shift;
    }
    p.hips += shift;
    return p;
}

std::vector<uint8_t> Ghost::encode() const {
    std::vector<uint8_t> out;
    uint32_t magic = kMagic, version = kVersion;
    std::string n = name.substr(0, kMaxName);
    float time = m_time;
    std::vector<kke::net::NetPlayerState> samples = m_samples;
    {
        kke::net::WriteStream w(out);
        serializeGhost(w, magic, version, n, time, samples);
    }
    return out;
}

std::optional<Ghost> Ghost::decode(const std::vector<uint8_t>& bytes) {
    Ghost g;
    uint32_t magic = 0, version = 0;
    kke::net::ReadStream r(bytes.data(), bytes.size());
    serializeGhost(r, magic, version, g.name, g.m_time, g.m_samples);
    if (!r.ok() || magic != kMagic || version != kVersion || g.m_samples.empty()) return std::nullopt;
    return g;
}

bool Ghost::save(const std::filesystem::path& file, std::string* error) const {
    std::error_code ec;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    const std::vector<uint8_t> bytes = encode();
    // Via a temporary file and a rename: a crash never leaves half a ghost.
    const std::filesystem::path tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            if (error) *error = "could not write " + tmp.string();
            return false;
        }
    }
    std::filesystem::rename(tmp, file, ec);
    if (ec) {
        if (error) *error = ec.message();
        return false;
    }
    return true;
}

std::optional<Ghost> Ghost::load(const std::filesystem::path& file, std::string* error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt; // no ghost yet: not an error
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto g = decode(bytes);
    if (!g && error) *error = file.string() + " is damaged or from another version";
    return g;
}

} // namespace climb_race
