#include "kke/AudioMixer.h"
#include "kke/WavFile.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

using namespace kke;

namespace {
std::vector<unsigned char> readAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
uint32_t le32(const std::vector<unsigned char>& b, size_t at) {
    return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24;
}
int16_t le16s(const std::vector<unsigned char>& b, size_t at) { return int16_t(uint16_t(b[at] | b[at + 1] << 8)); }
std::string tempPath(const char* name) { return (std::filesystem::temp_directory_path() / name).string(); }
} // namespace

TEST(WavFile, WritesA16BitPcmHeaderAndClippedSamples) {
    const std::string path = tempPath("kke_test_wav_file.wav");
    const float s[] = {0.0f, 1.0f, -1.0f, 0.5f, 2.0f, -3.0f};
    std::string err;
    ASSERT_TRUE(writeWav(path, s, 6, 2, 48000, &err)) << err;
    const auto b = readAll(path);
    ASSERT_EQ(b.size(), 44u + 12u);
    EXPECT_EQ(std::string(b.begin(), b.begin() + 4), "RIFF");
    EXPECT_EQ(le32(b, 4), 36u + 12u);
    EXPECT_EQ(std::string(b.begin() + 8, b.begin() + 16), "WAVEfmt ");
    EXPECT_EQ(le32(b, 24), 48000u);
    EXPECT_EQ(le32(b, 28), 48000u * 4u);
    EXPECT_EQ(le32(b, 40), 12u);
    EXPECT_EQ(le16s(b, 44), 0);
    EXPECT_EQ(le16s(b, 46), 32767);
    EXPECT_EQ(le16s(b, 48), -32767);
    EXPECT_EQ(le16s(b, 50), 16383);
    EXPECT_EQ(le16s(b, 52), 32767);   // clipped
    EXPECT_EQ(le16s(b, 54), -32767);  // clipped
    std::filesystem::remove(path);
}

TEST(WavFile, RefusesBadArgumentsAndUnwritablePaths) {
    const float s[] = {0.0f, 0.0f, 0.0f};
    std::string err;
    EXPECT_FALSE(writeWav(tempPath("kke_bad.wav"), s, 3, 2, 48000, &err));
    EXPECT_NE(err.find("whole number"), std::string::npos);
    EXPECT_FALSE(writeWav(tempPath("kke_bad.wav"), s, 3, 0, 48000, &err));
    EXPECT_FALSE(writeWav(tempPath("kke_bad.wav"), s, 3, 1, 0, &err));
    EXPECT_FALSE(writeWav("/nonexistent-dir/x/y.wav", s, 3, 1, 48000, &err));
    EXPECT_NE(err.find("can't open"), std::string::npos);
}

TEST(AudioMixerCapture, RecordsExactlyWhatWasMixed) {
    AudioMixer mixer(48000, 4);
    auto buf = std::make_shared<SoundBuffer>();
    buf->samples.assign(4800, 0.25f);
    VoiceDesc d;
    d.sound = buf;
    d.spatial = false;
    ASSERT_NE(mixer.play(d), 0u);
    EXPECT_FALSE(mixer.capturing());
    mixer.startCapture(1000);
    EXPECT_TRUE(mixer.capturing());
    std::vector<float> a(512 * 2), b(256 * 2);
    mixer.mix(a.data(), 512);
    mixer.mix(b.data(), 256);
    const std::vector<float> got = mixer.stopCapture();
    EXPECT_FALSE(mixer.capturing());
    ASSERT_EQ(got.size(), a.size() + b.size());
    for (size_t i = 0; i < a.size(); ++i) ASSERT_EQ(got[i], a[i]);
    for (size_t i = 0; i < b.size(); ++i) ASSERT_EQ(got[a.size() + i], b[i]);
    // Not capturing: nothing kept.
    mixer.mix(a.data(), 512);
    EXPECT_TRUE(mixer.stopCapture().empty());
}
