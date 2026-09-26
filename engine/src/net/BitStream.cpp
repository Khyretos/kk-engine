#include "kke/net/BitStream.h"

#include <algorithm>
#include <cmath>

namespace kke::net {

namespace {

uint32_t steps(float min, float max, float resolution) {
    return static_cast<uint32_t>(std::ceil((max - min) / resolution));
}

// Values of the three smallest quaternion components lie in
// [-1/sqrt(2), 1/sqrt(2)].
constexpr float kQuatRange = 0.70710678f;
constexpr int kQuatBits = 11;

} // namespace

// ---------------------------------------------------------------- write

void WriteStream::bits(uint32_t value, int count) {
    if (count <= 0) return;
    if (count < 32) value &= (1u << count) - 1u;
    m_scratch |= static_cast<uint64_t>(value) << m_scratchBits;
    m_scratchBits += count;
    m_bitCount += static_cast<size_t>(count);
    while (m_scratchBits >= 8) {
        m_out.push_back(static_cast<uint8_t>(m_scratch & 0xffu));
        m_scratch >>= 8;
        m_scratchBits -= 8;
    }
}

void WriteStream::flush() {
    if (m_scratchBits > 0) {
        m_out.push_back(static_cast<uint8_t>(m_scratch & 0xffu));
        m_bitCount += static_cast<size_t>(8 - m_scratchBits);
        m_scratch = 0;
        m_scratchBits = 0;
    }
}

void WriteStream::real(float& v, float min, float max, float resolution) {
    const float x = std::isfinite(v) ? std::clamp(v, min, max) : min;
    const uint32_t n = steps(min, max, resolution);
    const uint32_t q = std::min(n, static_cast<uint32_t>(std::lround((x - min) / resolution)));
    bits(q, bitsRequired(n));
}

void WriteStream::vec3(glm::vec3& v, float range, float resolution) {
    for (int i = 0; i < 3; ++i) real(v[i], -range, range, resolution);
}

void WriteStream::vec3(glm::vec3& v, const glm::vec3& min, const glm::vec3& max, float resolution) {
    for (int i = 0; i < 3; ++i) real(v[i], min[i], max[i], resolution);
}

void WriteStream::quat(glm::quat& qIn) {
    glm::quat q = qIn;
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    q = std::isfinite(len) && len > 1e-6f ? q / len : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    float c[4] = { q.x, q.y, q.z, q.w };
    int largest = 0;
    for (int i = 1; i < 4; ++i)
        if (std::fabs(c[i]) > std::fabs(c[largest])) largest = i;
    const float sign = c[largest] < 0.0f ? -1.0f : 1.0f; // q and -q are the same rotation
    bits(static_cast<uint32_t>(largest), 2);
    for (int i = 0; i < 4; ++i) {
        if (i == largest) continue;
        float x = c[i] * sign;
        real(x, -kQuatRange, kQuatRange, 2.0f * kQuatRange / static_cast<float>((1u << kQuatBits) - 1u));
    }
}

void WriteStream::string(std::string& s, size_t maxLength) {
    const size_t n = std::min(s.size(), maxLength);
    bits(static_cast<uint32_t>(n), bitsRequired(static_cast<uint32_t>(maxLength)));
    for (size_t i = 0; i < n; ++i) bits(static_cast<uint8_t>(s[i]), 8);
}

void WriteStream::bytes(std::vector<uint8_t>& data, size_t maxLength) {
    const size_t n = std::min(data.size(), maxLength);
    bits(static_cast<uint32_t>(n), bitsRequired(static_cast<uint32_t>(maxLength)));
    for (size_t i = 0; i < n; ++i) bits(data[i], 8);
}

// ---------------------------------------------------------------- read

uint32_t ReadStream::readBits(int count) {
    if (!m_ok || count <= 0) return 0;
    if (static_cast<size_t>(count) > bitsLeft()) {
        m_ok = false;
        return 0;
    }
    uint32_t value = 0;
    for (int got = 0; got < count;) {
        const size_t byte = m_bitPos >> 3;
        const int offset = static_cast<int>(m_bitPos & 7u);
        const int take = std::min(8 - offset, count - got);
        const uint32_t chunk = (static_cast<uint32_t>(m_data[byte]) >> offset) & ((1u << take) - 1u);
        value |= chunk << got;
        got += take;
        m_bitPos += static_cast<size_t>(take);
    }
    return value;
}

void ReadStream::real(float& v, float min, float max, float resolution) {
    const uint32_t n = steps(min, max, resolution);
    const uint32_t q = readBits(bitsRequired(n));
    if (!m_ok || q > n) {
        m_ok = false;
        v = min;
        return;
    }
    v = std::min(max, min + static_cast<float>(q) * resolution);
}

void ReadStream::vec3(glm::vec3& v, float range, float resolution) {
    for (int i = 0; i < 3; ++i) real(v[i], -range, range, resolution);
}

void ReadStream::vec3(glm::vec3& v, const glm::vec3& min, const glm::vec3& max, float resolution) {
    for (int i = 0; i < 3; ++i) real(v[i], min[i], max[i], resolution);
}

void ReadStream::quat(glm::quat& q) {
    const int largest = static_cast<int>(readBits(2));
    float c[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float sum = 0.0f;
    for (int i = 0; i < 4; ++i) {
        if (i == largest) continue;
        real(c[i], -kQuatRange, kQuatRange, 2.0f * kQuatRange / static_cast<float>((1u << kQuatBits) - 1u));
        sum += c[i] * c[i];
    }
    c[largest] = std::sqrt(std::max(0.0f, 1.0f - sum));
    q = m_ok ? glm::normalize(glm::quat(c[3], c[0], c[1], c[2])) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

void ReadStream::string(std::string& s, size_t maxLength) {
    const uint32_t n = readBits(bitsRequired(static_cast<uint32_t>(maxLength)));
    s.clear();
    if (!m_ok || n > maxLength || static_cast<size_t>(n) * 8 > bitsLeft()) {
        m_ok = false;
        return;
    }
    s.reserve(n);
    for (uint32_t i = 0; i < n; ++i) s.push_back(static_cast<char>(readBits(8)));
}

void ReadStream::bytes(std::vector<uint8_t>& data, size_t maxLength) {
    const uint32_t n = readBits(bitsRequired(static_cast<uint32_t>(maxLength)));
    data.clear();
    if (!m_ok || n > maxLength || static_cast<size_t>(n) * 8 > bitsLeft()) {
        m_ok = false;
        return;
    }
    data.reserve(n);
    for (uint32_t i = 0; i < n; ++i) data.push_back(static_cast<uint8_t>(readBits(8)));
}

} // namespace kke::net
