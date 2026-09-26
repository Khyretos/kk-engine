// kke_audio_preview: writes every audio material's impact sound to WAV
// files, soft to hard, so they can be listened to (and compared) without
// running a game. Also a spatial demo: one impact walking around the
// listener, mixed to stereo by kke::AudioMixer, and the same sound behind
// a wall. Usage: kke_audio_preview [out_dir]   (default: audio_preview/)
#include "kke/AudioMixer.h"
#include "kke/ImpactSynth.h"

#include <glm/gtc/constants.hpp>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
// 16-bit PCM WAV, written by hand (miniaudio's encoder is compiled out).
bool writeWav(const std::string& path, const std::vector<float>& interleaved, int channels, int rate) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t bytes = uint32_t(interleaved.size() * 2);
    f.write("RIFF", 4); u32(36 + bytes); f.write("WAVEfmt ", 8);
    u32(16); u16(1); u16(uint16_t(channels)); u32(uint32_t(rate)); u32(uint32_t(rate * channels * 2)); u16(uint16_t(channels * 2)); u16(16);
    f.write("data", 4); u32(bytes);
    for (float s : interleaved) {
        const float c = s < -1.0f ? -1.0f : (s > 1.0f ? 1.0f : s);
        const int16_t v = int16_t(c * 32767.0f);
        f.write(reinterpret_cast<const char*>(&v), 2);
    }
    return bool(f);
}
} // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "audio_preview";
    std::filesystem::create_directories(dir);
    const int rate = 48000;
    kke::AudioMaterialTable table;

    // Per material: soft, medium, hard hit, 0.4 s apart.
    for (const auto& [id, mat] : table.all()) {
        std::vector<float> out(size_t(rate) * 3, 0.0f);
        const float levels[] = {0.15f, 0.5f, 1.0f};
        for (int i = 0; i < 3; ++i) {
            kke::ImpactParams p;
            p.intensity = levels[i];
            p.seed = uint32_t(i + 1);
            kke::SoundBuffer b = kke::synthesizeImpact(mat, p, rate);
            const size_t at = size_t(i) * size_t(rate) * 4 / 10;
            for (size_t k = 0; k < b.samples.size() && at + k < out.size(); ++k) out[at + k] += b.samples[k];
        }
        const std::string path = dir + "/impact_" + mat.name + ".wav";
        std::printf("%s %s\n", writeWav(path, out, 1, rate) ? "wrote" : "FAILED", path.c_str());
    }

    // Spatial: a metal knock every 0.5 s walking a circle around the
    // listener (front, right, back, left), then the same four behind a
    // wood wall (transmission 0.35).
    for (int walled = 0; walled < 2; ++walled) {
        kke::AudioMixer mixer(rate, 16);
        kke::Listener l;
        mixer.setListener(l);
        auto knock = std::make_shared<kke::SoundBuffer>(kke::synthesizeImpact(table.get(kke::AudioMaterialTable::Metal), {0.7f, 1.0f, 7}, rate));
        std::vector<float> out;
        std::vector<float> block(size_t(rate / 2) * 2);
        for (int step = 0; step < 8; ++step) {
            const float az = glm::half_pi<float>() * float(step);
            kke::VoiceDesc d;
            d.sound = knock;
            d.position = glm::vec3(std::sin(az), 0.0f, -std::cos(az)) * 3.0f;
            const uint32_t v = mixer.play(d);
            if (walled) mixer.setTransmission(v, table.get(kke::AudioMaterialTable::Wood).transmission);
            mixer.mix(block.data(), rate / 2);
            out.insert(out.end(), block.begin(), block.end());
        }
        const std::string path = dir + (walled ? "/spatial_behind_wood_wall.wav" : "/spatial_front_right_back_left.wav");
        std::printf("%s %s\n", writeWav(path, out, 2, rate) ? "wrote" : "FAILED", path.c_str());
    }
    return 0;
}
