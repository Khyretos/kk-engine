#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kke::net {

// Bit-packed serialization, in the style of Glenn Fiedler's `serialize`
// library (BSD-3; docs/NETWORKING.md): each message has ONE function that both
// writes and reads it, templated on the stream, so the two directions can
// never disagree:
//
//     template <typename Stream> bool serialize(Stream& s, Hello& m) {
//         s.integer(m.version, 0, 65535);
//         s.string(m.name, 24);
//         return s.ok();
//     }
//
// Values are packed into exactly the bits their range needs (a 0..8
// player id is 4 bits, a position to the millimetre within +-4 km is 23),
// so a snapshot of 60 bodies fits one ~1200-byte UDP packet.
//
// Reading is hostile-input safe: every read checks the remaining bits and
// the value's range; the first failure sets !ok(), leaves the value at a
// safe default, and every later read is a no-op. Decoders never read out
// of bounds, allocate more than their stated maximums, or loop on
// attacker-chosen counts (fuzzed in tests/test_net.cpp).

// How many bits hold any value in [0, range].
constexpr int bitsRequired(uint32_t range) {
    int n = 0;
    while (n < 32 && (range >> n) != 0) ++n;
    return n;
}

class WriteStream {
public:
    static constexpr bool kReading = false;
    static constexpr bool kWriting = true;

    explicit WriteStream(std::vector<uint8_t>& out) : m_out(out) {}
    ~WriteStream() { flush(); }

    void bits(uint32_t value, int count);
    void boolean(bool& b) { bits(b ? 1u : 0u, 1); }
    template <typename T>
    void integer(T& v, int64_t min, int64_t max);
    // Clamped to [min, max], stored in steps of `resolution`.
    void real(float& v, float min, float max, float resolution);
    void vec3(glm::vec3& v, float range, float resolution); // each axis in [-range, range]
    void vec3(glm::vec3& v, const glm::vec3& min, const glm::vec3& max, float resolution);
    void quat(glm::quat& q);                                 // "smallest three", 2 + 3 x 11 bits
    void string(std::string& s, size_t maxLength);
    void bytes(std::vector<uint8_t>& data, size_t maxLength);
    // Whole bytes so far (the last one padded). Called by the destructor.
    void flush();
    size_t bitsWritten() const { return m_bitCount; }
    bool ok() const { return true; }

private:
    std::vector<uint8_t>& m_out;
    uint64_t m_scratch = 0;
    int m_scratchBits = 0;
    size_t m_bitCount = 0;
};

class ReadStream {
public:
    static constexpr bool kReading = true;
    static constexpr bool kWriting = false;

    ReadStream(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}

    uint32_t readBits(int count);
    void bits(uint32_t& value, int count) { value = readBits(count); }
    void boolean(bool& b) { b = readBits(1) != 0; }
    template <typename T>
    void integer(T& v, int64_t min, int64_t max);
    void real(float& v, float min, float max, float resolution);
    void vec3(glm::vec3& v, float range, float resolution);
    void vec3(glm::vec3& v, const glm::vec3& min, const glm::vec3& max, float resolution);
    void quat(glm::quat& q);
    void string(std::string& s, size_t maxLength);
    void bytes(std::vector<uint8_t>& data, size_t maxLength);
    void flush() {}

    bool ok() const { return m_ok; }
    void fail() { m_ok = false; }
    size_t bitsRead() const { return m_bitPos; }
    size_t bitsLeft() const { return m_size * 8 - m_bitPos; }

private:
    const uint8_t* m_data;
    size_t m_size;
    size_t m_bitPos = 0;
    bool m_ok = true;
};

template <typename T>
void WriteStream::integer(T& v, int64_t min, int64_t max) {
    const int64_t clamped = static_cast<int64_t>(v) < min ? min : (static_cast<int64_t>(v) > max ? max : static_cast<int64_t>(v));
    bits(static_cast<uint32_t>(clamped - min), bitsRequired(static_cast<uint32_t>(max - min)));
}

template <typename T>
void ReadStream::integer(T& v, int64_t min, int64_t max) {
    const uint32_t raw = readBits(bitsRequired(static_cast<uint32_t>(max - min)));
    const int64_t value = min + static_cast<int64_t>(raw);
    if (!m_ok || value > max) {
        m_ok = false;
        v = static_cast<T>(min);
        return;
    }
    v = static_cast<T>(value);
}

} // namespace kke::net
