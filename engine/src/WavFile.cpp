#include "kke/WavFile.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace kke {

bool writeWav(const std::string& path, const float* interleaved, size_t sampleCount, int channels, int sampleRate, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    if (channels < 1 || channels > 8) return fail("unsupported channel count " + std::to_string(channels));
    if (sampleRate < 1) return fail("bad sample rate " + std::to_string(sampleRate));
    if (sampleCount % size_t(channels) != 0) return fail("sample count is not a whole number of frames");
    if (sampleCount && !interleaved) return fail("no samples");
    const uint64_t bytes = uint64_t(sampleCount) * 2;
    if (bytes > 0xFFFFFFFFull - 36) return fail("too long for a WAV file (4 GB)");

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return fail("can't open '" + path + "' for writing: " + std::strerror(errno));
    auto u32 = [&](uint32_t v) {
        const unsigned char b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
        f.write(reinterpret_cast<const char*>(b), 4);
    };
    auto u16 = [&](uint16_t v) {
        const unsigned char b[2] = {uint8_t(v), uint8_t(v >> 8)};
        f.write(reinterpret_cast<const char*>(b), 2);
    };
    f.write("RIFF", 4);
    u32(uint32_t(36 + bytes));
    f.write("WAVEfmt ", 8);
    u32(16);
    u16(1); // PCM
    u16(uint16_t(channels));
    u32(uint32_t(sampleRate));
    u32(uint32_t(sampleRate * channels * 2));
    u16(uint16_t(channels * 2));
    u16(16);
    f.write("data", 4);
    u32(uint32_t(bytes));
    std::vector<unsigned char> block;
    block.reserve(8192);
    for (size_t i = 0; i < sampleCount; ++i) {
        float s = interleaved[i];
        s = s != s ? 0.0f : (s < -1.0f ? -1.0f : (s > 1.0f ? 1.0f : s));
        const int16_t v = int16_t(s * 32767.0f);
        block.push_back(uint8_t(uint16_t(v)));
        block.push_back(uint8_t(uint16_t(v) >> 8));
        if (block.size() >= 8192) {
            f.write(reinterpret_cast<const char*>(block.data()), std::streamsize(block.size()));
            block.clear();
        }
    }
    f.write(reinterpret_cast<const char*>(block.data()), std::streamsize(block.size()));
    f.flush();
    if (!f) return fail("write to '" + path + "' failed: " + std::strerror(errno));
    return true;
}

} // namespace kke
