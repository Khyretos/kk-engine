#include "kke/AudioMixer.h"
#include "kke/RoomAcoustics.h"

#include <gtest/gtest.h>
#include <glm/gtc/constants.hpp>

#include <cmath>
#include <limits>
#include <memory>

using namespace kke;

namespace {

// An axis-aligned box room around the origin, half-size `h`, with an
// optional hole in the +X wall (|y|,|z| < hole) and an optional roof.
AcousticRayFn boxRoom(glm::vec3 h, uint32_t material, float hole = 0.0f, bool roof = true, bool floor = true) {
    return [=](const glm::vec3& from, const glm::vec3& dir, float maxDistance) -> AcousticRay {
        float best = std::numeric_limits<float>::infinity();
        glm::vec3 normal(0.0f);
        for (int axis = 0; axis < 3; ++axis) {
            for (float sign : {-1.0f, 1.0f}) {
                if (std::fabs(dir[axis]) < 1e-6f) continue;
                if (axis == 1 && sign > 0 && !roof) continue;
                if (axis == 1 && sign < 0 && !floor) continue;
                const float t = (sign * h[axis] - from[axis]) / dir[axis];
                if (t <= 0.0f) continue;
                const glm::vec3 p = from + dir * t;
                if (glm::any(glm::greaterThan(glm::abs(p), h + 1e-3f))) continue; // the walls are the box's faces, not infinite planes
                if (axis == 0 && sign > 0 && hole > 0.0f && std::fabs(p.y) < hole && std::fabs(p.z) < hole) continue;
                if (t < best) {
                    best = t;
                    normal = glm::vec3(0.0f);
                    normal[axis] = -sign;
                }
            }
        }
        if (best > maxDistance) return {};
        return {true, best, material, normal};
    };
}

AcousticRayFn openField() {
    return [](const glm::vec3& from, const glm::vec3& dir, float maxDistance) -> AcousticRay {
        if (dir.y >= -1e-4f) return {};
        const float t = (from.y + 1.7f) / -dir.y; // ground 1.7 m below the ears
        if (t > maxDistance) return {};
        return {true, t, AudioMaterialTable::Dirt, glm::vec3(0, 1, 0)};
    };
}

double energy(const std::vector<float>& v, size_t from, size_t to) {
    double e = 0.0;
    for (size_t i = from; i < to && i < v.size(); ++i) e += double(v[i]) * v[i];
    return e;
}

} // namespace

TEST(RoomAcoustics, FibonacciSphereIsEvenAndUnit) {
    const auto dirs = fibonacciSphere(64);
    ASSERT_EQ(dirs.size(), 64u);
    glm::vec3 sum(0.0f);
    for (const glm::vec3& d : dirs) {
        EXPECT_NEAR(glm::length(d), 1.0f, 1e-4f);
        sum += d;
    }
    EXPECT_LT(glm::length(sum) / 64.0f, 0.05f);
}

TEST(RoomAcoustics, OpenFieldHasNoReverb) {
    AudioMaterialTable m;
    const RoomAcoustics r = probeRoom(glm::vec3(0.0f), openField(), m);
    EXPECT_LT(r.enclosure, 0.6f);
    EXPECT_EQ(r.ceiling, 0.0f);
    EXPECT_LT(r.wet, 0.02f);
    EXPECT_GT(r.openness, 0.95f);
}

TEST(RoomAcoustics, ClosedStoneRoomRings) {
    AudioMaterialTable m;
    const RoomAcoustics r = probeRoom(glm::vec3(0.0f), boxRoom({4, 3, 4}, AudioMaterialTable::Stone), m);
    EXPECT_FLOAT_EQ(r.enclosure, 1.0f);
    EXPECT_FLOAT_EQ(r.walls, 1.0f);
    EXPECT_FLOAT_EQ(r.ceiling, 1.0f);
    EXPECT_GT(r.wet, 0.3f);
    EXPECT_GT(r.rt60, 0.5f);
    EXPECT_TRUE(r.openings.empty());
    EXPECT_EQ(glm::length(r.openingDir), 0.0f);
}

TEST(RoomAcoustics, BiggerRoomsRingLongerSoftRoomsShorter) {
    AudioMaterialTable m;
    const RoomAcoustics small = probeRoom(glm::vec3(0.0f), boxRoom({3, 2.5f, 3}, AudioMaterialTable::Stone), m);
    const RoomAcoustics hall = probeRoom(glm::vec3(0.0f), boxRoom({15, 8, 15}, AudioMaterialTable::Stone), m);
    const RoomAcoustics padded = probeRoom(glm::vec3(0.0f), boxRoom({3, 2.5f, 3}, AudioMaterialTable::Rubber), m);
    EXPECT_GT(hall.rt60, small.rt60 * 2.0f);
    EXPECT_LT(padded.rt60, small.rt60 * 0.5f);
    EXPECT_GT(padded.damping, small.damping);
    EXPECT_GT(hall.preDelay, small.preDelay);
}

TEST(RoomAcoustics, FindsTheDoor) {
    AudioMaterialTable m;
    // A 3 m wide opening in the +X wall of a 4 m room.
    RoomProbeSettings s;
    s.rays = 128;
    const RoomAcoustics r = probeRoom(glm::vec3(0.0f), boxRoom({4, 3, 4}, AudioMaterialTable::Wood, 1.5f), m, s);
    ASSERT_FALSE(r.openings.empty());
    EXPECT_GT(r.openingDir.x, 0.9f);
    EXPECT_GT(r.openness, 0.0f);
    EXPECT_LT(r.openness, 0.5f);
    // Still a room: a roof and most walls.
    EXPECT_GT(r.wet, 0.2f);
}

TEST(RoomAcoustics, NoRayFunctionIsHarmless) {
    AudioMaterialTable m;
    const RoomAcoustics r = probeRoom(glm::vec3(0.0f), AcousticRayFn{}, m);
    EXPECT_EQ(r.wet, 0.0f);
}

TEST(Reverb, TailLastsAsLongAsRt60Says) {
    auto tailAfter = [](float rt60) {
        Reverb rv(48000);
        rv.setRoom(rt60, 0.2f, 0.5f, 0.0f);
        std::vector<float> in(480, 0.0f), out(960, 0.0f), all;
        // Let the parameters settle, then an impulse.
        for (int i = 0; i < 40; ++i) {
            std::fill(out.begin(), out.end(), 0.0f);
            rv.process(in.data(), out.data(), 480);
        }
        in[0] = 1.0f;
        for (int block = 0; block < 200; ++block) { // 2 s
            std::fill(out.begin(), out.end(), 0.0f);
            rv.process(in.data(), out.data(), 480);
            in[0] = 0.0f;
            all.insert(all.end(), out.begin(), out.end());
        }
        // Energy between 0.5 and 1.0 s, relative to the first 0.25 s.
        return energy(all, 48000, 96000) / std::max(1e-30, energy(all, 0, 24000));
    };
    const double shortRoom = tailAfter(0.3f), longRoom = tailAfter(2.0f);
    EXPECT_GT(longRoom, shortRoom * 100.0);
    EXPECT_LT(shortRoom, 1e-3);
}

TEST(Reverb, DryRoomAddsNothingAndOutputStaysFinite) {
    Reverb rv(48000);
    rv.setRoom(1.0f, 0.3f, 0.0f, 0.0f);
    std::vector<float> in(480, 0.5f), out(960, 0.0f);
    for (int i = 0; i < 50; ++i) rv.process(in.data(), out.data(), 480);
    for (float x : out) EXPECT_EQ(x, 0.0f);
    // Loud, long room: the feedback stays below 1 (no blow-up).
    rv.setRoom(8.0f, 0.0f, 1.0f, 0.05f);
    for (int i = 0; i < 1000; ++i) {
        std::fill(out.begin(), out.end(), 0.0f);
        rv.process(in.data(), out.data(), 480);
    }
    for (float x : out) {
        ASSERT_TRUE(std::isfinite(x));
        ASSERT_LT(std::fabs(x), 10.0f);
    }
}

namespace {
SoundHandle noise(float seconds, int rate = 48000) {
    auto b = std::make_shared<SoundBuffer>();
    b->sampleRate = rate;
    b->samples.resize(size_t(seconds * float(rate)));
    uint32_t s = 12345;
    for (float& x : b->samples) {
        s = s * 1664525u + 1013904223u;
        x = (float(s >> 8) / float(1u << 24) - 0.5f) * 0.5f;
    }
    return b;
}
} // namespace

TEST(Binaural, WoodworthDelay) {
    EXPECT_NEAR(AudioMixer::interauralDelay(0.0f), 0.0f, 1e-7f);
    // Straight to the side: (a/c)(pi/2 + 1) = ~0.66 ms for an 8.75 cm head.
    EXPECT_NEAR(AudioMixer::interauralDelay(glm::half_pi<float>()), 0.0875f / 343.0f * (glm::half_pi<float>() + 1.0f), 1e-6f);
    // Front and back mirror.
    EXPECT_NEAR(AudioMixer::interauralDelay(0.3f), AudioMixer::interauralDelay(glm::pi<float>() - 0.3f), 1e-6f);
}

TEST(Binaural, HeadShadowLiftsFacingEarCutsFarEar) {
    // DC gain is 1 (low frequencies wrap around the head); gain at Nyquist
    // is (b0 - b1) / (1 - a1).
    auto nyquist = [](AudioMixer::Shelf s) { return (s.b0 - s.b1) / (1.0f - s.a1); };
    auto dc = [](AudioMixer::Shelf s) { return (s.b0 + s.b1) / (1.0f + s.a1); };
    const auto facing = AudioMixer::headShadow(0.0f, 48000), away = AudioMixer::headShadow(glm::pi<float>(), 48000);
    EXPECT_NEAR(dc(facing), 1.0f, 1e-4f);
    EXPECT_NEAR(dc(away), 1.0f, 1e-4f);
    EXPECT_GT(nyquist(facing), 1.8f);
    EXPECT_LT(nyquist(away), 0.2f);
}

TEST(Binaural, SourceOnTheRightIsLouderAndEarlierInTheRightEar) {
    AudioMixer m(48000, 4);
    m.setSpatialMode(SpatialMode::Binaural);
    m.setRoom(0.3f, 0.5f, 0.0f, 0.0f);
    Listener l; // at the origin looking down -Z; +X is right
    m.setListener(l);
    VoiceDesc d;
    d.sound = noise(0.3f);
    d.position = glm::vec3(3.0f, 0.0f, 0.0f);
    ASSERT_NE(m.play(d), 0u);
    std::vector<float> out(2 * 4800);
    m.mix(out.data(), 4800);
    std::vector<float> left(4800), right(4800);
    for (int i = 0; i < 4800; ++i) { left[size_t(i)] = out[size_t(2 * i)]; right[size_t(i)] = out[size_t(2 * i + 1)]; }
    EXPECT_GT(energy(right, 0, 4800), energy(left, 0, 4800) * 1.5);
    // Cross-correlate: the left ear lags by ~the Woodworth delay (~32 samples).
    int bestLag = 0;
    double best = -1e30;
    for (int lag = -10; lag <= 60; ++lag) {
        double c = 0.0;
        for (int i = 100; i < 4000; ++i) c += double(right[size_t(i)]) * left[size_t(i + lag)];
        if (c > best) { best = c; bestLag = lag; }
    }
    const float expected = AudioMixer::interauralDelay(glm::half_pi<float>()) * 48000.0f;
    EXPECT_NEAR(float(bestLag), expected, 3.0f);
}

TEST(Mixer, ViaPlacesAnOccludedSoundAtTheOpening) {
    AudioMixer m(48000, 4);
    Listener l;
    m.setListener(l);
    VoiceDesc d;
    d.sound = noise(0.5f);
    d.position = glm::vec3(-5.0f, 0.0f, 0.0f); // behind a wall to the left
    const uint32_t id = m.play(d);
    ASSERT_NE(id, 0u);
    m.setVia(id, glm::vec3(3.0f, 0.0f, 0.0f), 12.0f); // ...but the door is on the right
    std::vector<float> out(2 * 960);
    m.mix(out.data(), 960);
    auto sounds = m.activeSounds();
    ASSERT_EQ(sounds.size(), 1u);
    EXPECT_GT(sounds[0].azimuth, 1.0f);           // heard from the right
    EXPECT_NEAR(sounds[0].distance, 12.0f, 1e-3f); // as far as the path
    m.clearVia(id);
    m.mix(out.data(), 960);
    sounds = m.activeSounds();
    EXPECT_LT(sounds[0].azimuth, -1.0f);
}

TEST(Mixer, ReverbSendsSpatialSoundsOnly) {
    auto tailEnergy = [](bool spatial, float wet) {
        AudioMixer m(48000, 4);
        m.setRoom(1.5f, 0.2f, wet, 0.0f);
        VoiceDesc d;
        d.sound = noise(0.05f);
        d.spatial = spatial;
        d.position = glm::vec3(0.0f, 0.0f, -2.0f);
        std::vector<float> out(2 * 480);
        for (int i = 0; i < 20; ++i) m.mix(out.data(), 480); // let the room settle
        m.play(d);
        double tail = 0.0;
        for (int i = 0; i < 60; ++i) {
            m.mix(out.data(), 480);
            if (i >= 20) tail += energy(out, 0, out.size()); // after the sound itself ended
        }
        return tail;
    };
    EXPECT_GT(tailEnergy(true, 0.4f), 1e-4);
    EXPECT_EQ(tailEnergy(false, 0.4f), 0.0);
    EXPECT_EQ(tailEnergy(true, 0.0f), 0.0);
}
