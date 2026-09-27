#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kke {

// What's behind everything, and the air in between: the sky, fog and the
// colour look of a scene. Part of kke::Lighting (Application.h), so every
// lit shader sees it through the same lighting buffer. A kke::Mood
// (Mood.h) fills all of it in from one small data file.
//
// Nothing here is on by default: a game that never touches it looks
// exactly as before (the renderer's clear colour, flat ambient, no fog,
// plain AgX).

// The sky drawn behind the scene.
struct Sky {
    enum class Kind : int {
        None = 0,     // the renderer's clear colour (setClearColor)
        Gradient = 1, // zenith / horizon / ground colours plus a sun disc
        Image = 2,    // an equirectangular HDR image (.hdr), e.g. a CC0 Poly Haven sky
    };
    Kind kind = Kind::None;

    // Gradient colours, linear scene light (not sRGB). Also used for an
    // image sky: kke::SkyImage::analyse() fills them in from the picture,
    // so fog and water reflections match it.
    glm::vec3 zenith{0.16f, 0.36f, 0.75f};
    glm::vec3 horizon{0.62f, 0.74f, 0.86f};
    glm::vec3 ground{0.18f, 0.17f, 0.16f};
    // How quickly the horizon colour gives way to the zenith going up:
    // 1 = evenly, larger = a thinner band of horizon glow.
    float horizonFalloff = 2.0f;

    // Sun disc and the glow around it (gradient skies; an image sky has its
    // own sun). The sun's direction is lights[0]'s.
    glm::vec3 sunColor{1.0f, 0.92f, 0.78f};
    float sunDiscIntensity = 40.0f; // times sunColor, inside the disc
    float sunSizeDegrees = 0.8f;    // angular diameter (the real sun is ~0.53)
    float sunGlow = 0.6f;           // strength of the halo (Henyey-Greenstein, g = 0.76)

    // Image sky. `image` is a path to a .hdr file (see Mood.h for where
    // relative names are looked up). Its sun sits wherever the photo had
    // it; `yawDegrees` turns the whole sky about the vertical axis.
    std::string image;
    float yawDegrees = 0.0f;
    float intensity = 1.0f; // multiplies the image (after it's normalised: SkyImage)

    // The scene's ambient (fill) light comes from the sky itself: its
    // colours spread over directions (blue from above, warm or dark from
    // below) instead of the flat Lighting::ambientColor. `ambientStrength`
    // scales it.
    bool lightsScene = true;
    float ambientStrength = 1.0f;
    // How much of the sky's colour the fill light keeps: 1 = all of it
    // (a blue sky fills shadows with blue), 0 = none (grey fill as bright
    // as the sky). Below 1 stands in for the light bounced off the ground
    // and everything around, which a sky alone leaves out.
    float ambientSaturation = 0.6f;
};

// Height fog: thicker near the ground, thinning out upward, towards the
// sky's horizon colour (with a warm glow looking into the sun). Analytic
// per pixel (Inigo Quilez, "Better Fog"): no noise, no temporal blending.
struct Fog {
    bool enabled = false;
    float density = 0.01f;       // per metre, at `height`
    float heightFalloff = 0.08f; // per metre above `height`: larger = fog hugs the ground
    float height = 0.0f;         // world y where fog is `density` thick
    float maxOpacity = 1.0f;     // cap, so far mountains don't vanish entirely
    bool colorFromSky = true;    // use the sky's horizon colour
    glm::vec3 color{0.6f, 0.68f, 0.76f}; // when colorFromSky is false (linear)
    float sunScatter = 0.6f;     // how strongly fog glows towards the sun
};

// The colour look, applied inside the tone curve. The standard ASC CDL
// (slope, offset, power per channel) plus saturation, the same controls
// film graders use and the "look" stage of AgX (Wrensch's minimal AgX:
// "punchy" and "golden"). All-identity by default (no change).
struct ColorGrade {
    glm::vec3 slope{1.0f};
    glm::vec3 offset{0.0f};
    glm::vec3 power{1.0f};
    float saturation = 1.0f;

    bool isIdentity() const {
        return slope == glm::vec3(1.0f) && offset == glm::vec3(0.0f) && power == glm::vec3(1.0f) && saturation == 1.0f;
    }
    // Named looks: "none", "punchy" (more contrast and colour), "golden"
    // (warm, soft), "cool" (blue-green shadows), "faded" (low contrast,
    // lifted blacks). False (and unchanged) for an unknown name.
    static bool preset(const std::string& name, ColorGrade& out);
};

// Order-2 spherical harmonics (9 coefficients per colour channel) of the
// light arriving from every direction: the standard compact way to light
// diffuse surfaces from an environment (Ramamoorthi & Hanrahan, "An
// Efficient Representation for Irradiance Environment Maps", 2001).
// The coefficients stored are already convolved with the cosine lobe and
// divided by pi, so irradiance(n) is the colour a white matte surface
// facing n shows: what Lighting::ambientColor meant for a flat ambient.
struct SkySH {
    std::array<glm::vec3, 9> c{};

    // A constant colour from every direction (the old flat ambient).
    static SkySH constant(const glm::vec3& color);
    // Sky colour `up` above the horizon, `down` below: a hemisphere light.
    static SkySH hemisphere(const glm::vec3& up, const glm::vec3& down);
    // Projects radiance(dir) sampled on a regular grid of directions.
    template <typename Fn>
    static SkySH project(Fn&& radiance, int thetaSteps = 32, int phiSteps = 64);

    glm::vec3 irradiance(const glm::vec3& n) const;
    SkySH operator*(float s) const;
};

// The radiance a gradient sky (Sky::Kind::Gradient, no sun disc) sends
// along `dir`: the same function as shaders/sky_common.glsl.
glm::vec3 gradientSkyRadiance(const Sky& sky, const glm::vec3& dir);

// An equirectangular HDR image sky, decoded on the CPU (stb_image, .hdr).
// Also reads a sky cropped below the horizon: an image less than half as
// tall as it is wide covers the top of the sphere, and the last row
// continues down to the nadir (the ground hides it anyway), which halves
// the file size of a "pure sky".
class SkyImage {
public:
    // Nullptr with `error` set when the file can't be read.
    static std::unique_ptr<SkyImage> load(const std::string& path, std::string* error = nullptr);
    // From pixels already in memory (tests; procedural skies).
    SkyImage(uint32_t width, uint32_t height, std::vector<float> rgb);

    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }
    // Rows of the full equirect sphere this image covers (>= height()).
    uint32_t fullHeight() const { return m_width / 2 > m_height ? m_width / 2 : m_height; }
    const std::vector<float>& rgb() const { return m_rgb; }

    // Bilinear sample towards `dir` (y up), before any yaw or intensity.
    glm::vec3 sample(const glm::vec3& dir) const;

    // Loads once per path and keeps it: moods, the renderer and every
    // view share one decoded copy. Thread safe.
    static std::shared_ptr<const SkyImage> cached(const std::string& path, std::string* error = nullptr);

    // Measurements used to fit the rest of the scene to the picture.
    struct Analysis {
        float normalise = 1.0f;       // scale that brings the sky's average above the horizon (sun left out) to 0.7
        glm::vec3 zenith{0.0f}, horizon{0.0f}, ground{0.0f}; // average colours (after `normalise`): above 50 degrees, 3 to 15, below 0
        glm::vec3 sunDirection{0.0f, 1.0f, 0.0f}; // towards the brightest spot (before yaw)
        float sunElevationDegrees = 90.0f;
        glm::vec3 sunColor{1.0f};     // normalised colour of the brightest spot
        bool hasSun = false;          // a clear bright spot stands out
        float sunClamp = 1e30f;       // texels brighter than this (before `normalise`) are the sun: left out of averages
    };
    // Worked out once, when the image is made.
    const Analysis& analysis() const { return m_analysis; }

    // The same image resampled to width x (width/2), full sphere, RGBA
    // half floats ready for the GPU (see kke::SkyRenderer).
    std::vector<uint16_t> toHalfRgba(float scale) const;

private:
    glm::vec3 texel(int x, int y) const;
    Analysis analyse() const;
    uint32_t m_width = 0, m_height = 0;
    std::vector<float> m_rgb;
    Analysis m_analysis;
};

// Turns a direction about the vertical axis by `degrees` (counter-
// clockwise seen from above), the way Sky::yawDegrees turns an image sky.
glm::vec3 rotateYaw(const glm::vec3& v, float degrees);

// Everything the GPU needs about the sky, worked out from kke::Sky and
// kke::Fog: an image sky's colours measured, the ambient light projected
// to spherical harmonics, the fog colour chosen. kke::SkyResolver makes
// it and caches the slow parts.
struct SkyEnvironment {
    Sky::Kind kind = Sky::Kind::None;
    glm::vec3 zenith{0.0f}, horizon{0.0f}, ground{0.0f};
    float horizonFalloff = 2.0f;
    float yawRadians = 0.0f;
    float imageScale = 1.0f;       // normalise x intensity
    glm::vec3 sunDisc{0.0f};       // colour x disc intensity (gradient skies)
    float sunCosRadius = 1.0f;
    glm::vec3 sunGlow{0.0f};
    glm::vec3 fogColor{0.0f};
    SkySH ambient;                 // what lit surfaces are filled with
    glm::vec3 ambientAverage{0.0f}; // its average, for shaders that want one colour
    std::shared_ptr<const SkyImage> image; // set when kind == Image
};

class SkyResolver {
public:
    // `flatAmbient` is Lighting::ambientColor, used when the sky doesn't
    // light the scene. Image load failures are logged once per path and
    // fall back to the gradient.
    const SkyEnvironment& resolve(const Sky& sky, const Fog& fog, const glm::vec3& flatAmbient);

private:
    SkyEnvironment m_env;
    bool m_valid = false;
    Sky m_lastSky;
    Fog m_lastFog;
    glm::vec3 m_lastAmbient{-1.0f};
    std::string m_failedImage;
};

// Direction <-> equirect coordinates, shared with shaders/sky_common.glsl:
// u = 0.5 + atan(x, -z) / 2pi, v = acos(y) / pi.
glm::vec2 equirectUv(const glm::vec3& dir);
glm::vec3 equirectDir(const glm::vec2& uv);

// ---------------------------------------------------------------- inline

template <typename Fn>
SkySH SkySH::project(Fn&& radiance, int thetaSteps, int phiSteps) {
    // Plain Riemann sum over the sphere, weighted by each cell's solid
    // angle. Basis constants from Ramamoorthi & Hanrahan (2001), eq. 3.
    std::array<glm::vec3, 9> L{};
    const float pi = 3.14159265358979f;
    float total = 0.0f;
    for (int t = 0; t < thetaSteps; ++t) {
        const float theta = (static_cast<float>(t) + 0.5f) * pi / static_cast<float>(thetaSteps);
        const float dOmega = std::sin(theta) * (pi / static_cast<float>(thetaSteps)) * (2.0f * pi / static_cast<float>(phiSteps));
        for (int p = 0; p < phiSteps; ++p) {
            const float phi = (static_cast<float>(p) + 0.5f) * 2.0f * pi / static_cast<float>(phiSteps);
            const glm::vec3 d(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
            const glm::vec3 r = radiance(d) * dOmega;
            L[0] += r * 0.282095f;
            L[1] += r * (0.488603f * d.y);
            L[2] += r * (0.488603f * d.z);
            L[3] += r * (0.488603f * d.x);
            L[4] += r * (1.092548f * d.x * d.y);
            L[5] += r * (1.092548f * d.y * d.z);
            L[6] += r * (0.315392f * (3.0f * d.z * d.z - 1.0f));
            L[7] += r * (1.092548f * d.x * d.z);
            L[8] += r * (0.546274f * (d.x * d.x - d.y * d.y));
            total += dOmega;
        }
    }
    // Correct the sum's small solid-angle error, then convolve with the
    // clamped cosine (A0 = pi, A1 = 2pi/3, A2 = pi/4) and divide by pi.
    const float fix = 4.0f * pi / total;
    const float a[9] = { 1.0f, 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f };
    SkySH out;
    for (int i = 0; i < 9; ++i) out.c[i] = L[i] * fix * a[i];
    return out;
}

} // namespace kke
