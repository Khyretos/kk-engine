#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace kke {

// An ocean surface as a sum of Gerstner (trochoidal) waves — the classic
// real-time ocean (GPU Gems 1 ch. 1; Tessendorf's notes). Each wave moves
// surface points in circles, which gives sharp crests and flat troughs
// like real swell, at the cost of a few sin/cos per wave.
//
// The same math runs on the GPU (shaders/ocean.vert, with the parameters
// from toGpu()) and on the CPU here, so floating objects ride exactly the
// waves that are drawn. Dispersion is real (omega = sqrt(g k)): long waves
// travel faster than short ones, which is most of why it looks right.
struct GerstnerWave {
    glm::vec2 direction{1.0f, 0.0f};  // normalized travel direction
    float wavelength = 10.0f;         // metres
    float amplitude = 0.3f;           // metres
    float steepness = 0.5f;           // 0 = sine wave .. 1 = sharp crest (keep sum over waves < 1)
    float phase = 0.0f;
};

class OceanWaves {
public:
    // 4 waves: what fits in the 128 bytes of push constants every Vulkan
    // device guarantees (shaders/ocean.vert), so CPU and GPU match exactly.
    static constexpr int kMaxWaves = 4;

    OceanWaves();                          // a pleasant default swell
    std::vector<GerstnerWave>& waves() { return m_waves; }
    const std::vector<GerstnerWave>& waves() const { return m_waves; }
    float seaLevel = 0.0f;

    // Builds a swell from wind: wave heights and lengths scale with wind
    // speed (roughly Pierson-Moskowitz), directions spread +-40 degrees.
    void setWind(float speedMetresPerSecond, float directionRadians, float choppiness = 0.6f);

    // Displacement of the undisturbed surface point (x, 0, z) at time t.
    glm::vec3 displacement(const glm::vec2& xz, float t) const;
    // Water height at world (x, z): Gerstner moves points sideways, so this
    // inverts that with a few fixed-point iterations (converges fast for
    // steepness < 1).
    float height(const glm::vec2& xz, float t) const;
    // Surface normal and the water's own velocity at (x, z).
    glm::vec3 normal(const glm::vec2& xz, float t) const;
    glm::vec3 velocity(const glm::vec2& xz, float t) const;

    // Packed for shaders/ocean.vert's push constants (7 vec4):
    // out[0..3] = vec4(dir.x, dir.y, k = 2 pi / wavelength, amplitude) per wave,
    // out[4] = Q per wave, out[5] = omega per wave, out[6] = phase per wave.
    void toGpu(std::vector<glm::vec4>& out) const;

private:
    std::vector<GerstnerWave> m_waves;
};

} // namespace kke
