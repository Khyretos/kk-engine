#include "kke/Sky.h"
#include "kke/Log.h"

// No STB_IMAGE_IMPLEMENTATION here: RmlVulkanRenderInterface.cpp has it.
// stb_image reads Radiance .hdr (RGBE) with stbi_loadf.
#include <stb_image.h>

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

namespace kke {

namespace {
constexpr float kPi = 3.14159265358979f;

// What an image sky's average (sun excluded) is scaled to, in scene light:
// bright enough to read as daylight next to a surface lit by a sun of
// intensity ~3, dim enough that AgX keeps its colour. Sky::intensity
// scales from there (a night sky sets a small one).
constexpr float kSkyAverage = 0.7f;

float luminance(const glm::vec3& c) { return glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f)); }
} // namespace

bool ColorGrade::preset(const std::string& name, ColorGrade& out) {
    ColorGrade g;
    if (name == "none") {
    } else if (name == "punchy") {
        // Wrensch's minimal AgX "punchy" look, eased: his (power 1.35,
        // saturation 1.4) is tuned for a photo; this keeps skin and
        // Synty's flat colours from going neon.
        g.power = glm::vec3(1.2f);
        g.saturation = 1.3f;
    } else if (name == "golden") {
        // Wrensch's "golden" look, eased the same way.
        g.slope = glm::vec3(1.0f, 0.93f, 0.78f);
        g.power = glm::vec3(0.95f);
        g.saturation = 1.2f;
    } else if (name == "cool") {
        g.slope = glm::vec3(0.94f, 1.0f, 1.06f);
        g.offset = glm::vec3(-0.01f, 0.0f, 0.015f);
        g.power = glm::vec3(1.1f);
        g.saturation = 1.1f;
    } else if (name == "faded") {
        g.slope = glm::vec3(0.92f);
        g.offset = glm::vec3(0.04f);
        g.power = glm::vec3(0.95f);
        g.saturation = 0.85f;
    } else {
        return false;
    }
    out = g;
    return true;
}

SkySH SkySH::constant(const glm::vec3& color) {
    // Exactly: only the constant term, which irradiance() multiplies back.
    SkySH o;
    o.c[0] = color / 0.282095f;
    return o;
}

SkySH SkySH::hemisphere(const glm::vec3& up, const glm::vec3& down) {
    return project([&](const glm::vec3& d) { return d.y >= 0.0f ? up : down; });
}

glm::vec3 SkySH::irradiance(const glm::vec3& n) const {
    // Same basis, same order as project() and shaders/sky_common.glsl.
    glm::vec3 r = c[0] * 0.282095f;
    r += c[1] * (0.488603f * n.y) + c[2] * (0.488603f * n.z) + c[3] * (0.488603f * n.x);
    r += c[4] * (1.092548f * n.x * n.y) + c[5] * (1.092548f * n.y * n.z);
    r += c[6] * (0.315392f * (3.0f * n.z * n.z - 1.0f));
    r += c[7] * (1.092548f * n.x * n.z) + c[8] * (0.546274f * (n.x * n.x - n.y * n.y));
    return glm::max(r, glm::vec3(0.0f));
}

SkySH SkySH::operator*(float s) const {
    SkySH o;
    for (int i = 0; i < 9; ++i) o.c[i] = c[i] * s;
    return o;
}

glm::vec3 gradientSkyRadiance(const Sky& sky, const glm::vec3& dir) {
    const float y = dir.y;
    if (y >= 0.0f) {
        const float t = std::pow(std::clamp(y, 0.0f, 1.0f), 1.0f / std::max(sky.horizonFalloff, 0.05f));
        return glm::mix(sky.horizon, sky.zenith, t);
    }
    // Below the horizon: a short blend into the ground colour.
    const float t = 1.0f - std::pow(1.0f - std::clamp(-y, 0.0f, 1.0f), 8.0f);
    return glm::mix(sky.horizon, sky.ground, t);
}

glm::vec2 equirectUv(const glm::vec3& dir) {
    const glm::vec3 d = glm::normalize(dir);
    return { 0.5f + std::atan2(d.x, -d.z) / (2.0f * kPi), std::acos(std::clamp(d.y, -1.0f, 1.0f)) / kPi };
}

glm::vec3 equirectDir(const glm::vec2& uv) {
    const float phi = (uv.x - 0.5f) * 2.0f * kPi;
    const float theta = uv.y * kPi;
    return { std::sin(theta) * std::sin(phi), std::cos(theta), -std::sin(theta) * std::cos(phi) };
}

std::unique_ptr<SkyImage> SkyImage::load(const std::string& path, std::string* error) {
    int w = 0, h = 0, n = 0;
    float* px = stbi_loadf(path.c_str(), &w, &h, &n, 3);
    if (!px) {
        if (error) *error = "can't read sky image '" + path + "' (" + (stbi_failure_reason() ? stbi_failure_reason() : "unknown reason") + ")";
        return nullptr;
    }
    if (w < 8 || h < 4 || h > w) {
        stbi_image_free(px);
        if (error) *error = "sky image '" + path + "' is " + std::to_string(w) + "x" + std::to_string(h) + "; an equirectangular sky is twice as wide as tall (or less tall, cropped below the horizon)";
        return nullptr;
    }
    std::vector<float> rgb(px, px + static_cast<size_t>(w) * h * 3);
    stbi_image_free(px);
    return std::make_unique<SkyImage>(static_cast<uint32_t>(w), static_cast<uint32_t>(h), std::move(rgb));
}

SkyImage::SkyImage(uint32_t width, uint32_t height, std::vector<float> rgb) : m_width(width), m_height(height), m_rgb(std::move(rgb)) {
    // Non-finite or negative texels (rare, but .hdr files have them) would
    // poison every average below.
    for (float& v : m_rgb)
        if (!std::isfinite(v) || v < 0.0f) v = 0.0f;
    m_analysis = analyse();
}

std::shared_ptr<const SkyImage> SkyImage::cached(const std::string& path, std::string* error) {
    static std::mutex mutex;
    static std::map<std::string, std::shared_ptr<const SkyImage>> cache;
    std::lock_guard<std::mutex> lock(mutex);
    if (auto it = cache.find(path); it != cache.end()) return it->second;
    std::shared_ptr<const SkyImage> img = load(path, error);
    if (img) cache[path] = img;
    return img;
}

glm::vec3 rotateYaw(const glm::vec3& v, float degrees) {
    const float a = glm::radians(degrees);
    const float c = std::cos(a), s = std::sin(a);
    return { c * v.x + s * v.z, v.y, -s * v.x + c * v.z };
}

glm::vec3 SkyImage::texel(int x, int y) const {
    x = ((x % static_cast<int>(m_width)) + static_cast<int>(m_width)) % static_cast<int>(m_width);
    y = std::clamp(y, 0, static_cast<int>(m_height) - 1);
    const size_t i = (static_cast<size_t>(y) * m_width + static_cast<size_t>(x)) * 3;
    return { m_rgb[i], m_rgb[i + 1], m_rgb[i + 2] };
}

glm::vec3 SkyImage::sample(const glm::vec3& dir) const {
    const glm::vec2 uv = equirectUv(dir);
    const float fx = uv.x * static_cast<float>(m_width) - 0.5f;
    const float fy = uv.y * static_cast<float>(fullHeight()) - 0.5f;
    const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
    const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
    const glm::vec3 a = glm::mix(texel(x0, y0), texel(x0 + 1, y0), tx);
    const glm::vec3 b = glm::mix(texel(x0, y0 + 1), texel(x0 + 1, y0 + 1), tx);
    return glm::mix(a, b, ty);
}

SkyImage::Analysis SkyImage::analyse() const {
    Analysis a;
    const float full = static_cast<float>(fullHeight());
    // Averages by solid angle over three bands, and the brightest texel.
    glm::dvec3 zen(0.0), hor(0.0), gnd(0.0), upper(0.0);
    double wz = 0.0, wh = 0.0, wg = 0.0, wu = 0.0;
    float peak = 0.0f;
    int peakX = 0, peakY = 0;
    for (uint32_t y = 0; y < m_height; ++y) {
        const float theta = (static_cast<float>(y) + 0.5f) / full * kPi;
        const float elevation = 90.0f - theta * 180.0f / kPi;
        const double w = std::sin(theta);
        for (uint32_t x = 0; x < m_width; ++x) {
            const glm::vec3 c = texel(static_cast<int>(x), static_cast<int>(y));
            const float l = luminance(c);
            if (elevation > 0.0f && l > peak) {
                peak = l;
                peakX = static_cast<int>(x);
                peakY = static_cast<int>(y);
            }
            if (elevation >= 0.0f) {
                upper += glm::dvec3(c) * w;
                wu += w;
            }
            if (elevation >= 50.0f) {
                zen += glm::dvec3(c) * w;
                wz += w;
            } else if (elevation >= 3.0f && elevation < 15.0f) {
                hor += glm::dvec3(c) * w;
                wh += w;
            } else if (elevation < 0.0f) {
                gnd += glm::dvec3(c) * w;
                wg += w;
            }
        }
    }
    // The sun is so bright it would dominate every average; take the
    // averages without it by clamping each texel's contribution (a simple,
    // robust stand-in for masking the disc).
    const glm::vec3 upperAvg = wu > 0.0 ? glm::vec3(upper / wu) : glm::vec3(1.0f);
    const float mean = luminance(upperAvg);
    if (peak > 0.0f && mean > 0.0f) {
        const float cap = mean * 8.0f;
        a.sunClamp = cap;
        glm::dvec3 u2(0.0), z2(0.0), h2(0.0);
        for (uint32_t y = 0; y < m_height; ++y) {
            const float theta = (static_cast<float>(y) + 0.5f) / full * kPi;
            const float elevation = 90.0f - theta * 180.0f / kPi;
            if (elevation < 0.0f) continue;
            const double w = std::sin(theta);
            for (uint32_t x = 0; x < m_width; ++x) {
                glm::vec3 c = texel(static_cast<int>(x), static_cast<int>(y));
                const float l = luminance(c);
                if (l > cap) c *= cap / l;
                u2 += glm::dvec3(c) * w;
                if (elevation >= 50.0f) z2 += glm::dvec3(c) * w;
                else if (elevation >= 3.0f && elevation < 15.0f) h2 += glm::dvec3(c) * w;
            }
        }
        upper = u2;
        zen = z2;
        hor = h2;
    }
    const glm::vec3 skyAvg = wu > 0.0 ? glm::vec3(upper / wu) : glm::vec3(1.0f);
    const float skyLum = std::max(luminance(skyAvg), 1e-6f);
    a.normalise = kSkyAverage / skyLum;
    a.zenith = (wz > 0.0 ? glm::vec3(zen / wz) : skyAvg) * a.normalise;
    a.horizon = (wh > 0.0 ? glm::vec3(hor / wh) : skyAvg) * a.normalise;
    a.ground = (wg > 0.0 ? glm::vec3(gnd / wg) : a.horizon * 0.3f) * a.normalise;

    a.sunDirection = equirectDir({ (static_cast<float>(peakX) + 0.5f) / static_cast<float>(m_width),
                                   (static_cast<float>(peakY) + 0.5f) / full });
    a.sunElevationDegrees = std::asin(std::clamp(a.sunDirection.y, -1.0f, 1.0f)) * 180.0f / kPi;
    // A real sun is hundreds of times brighter than the sky around it;
    // an overcast sky's brightest spot is a few times brighter at most.
    a.hasSun = peak > skyLum * 50.0f;
    if (a.hasSun) {
        // The disc's colour: average of the texels near the peak.
        glm::vec3 sum(0.0f);
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) sum += texel(peakX + dx, peakY + dy);
        const float l = std::max(luminance(sum), 1e-6f);
        a.sunColor = glm::clamp(sum / l, glm::vec3(0.0f), glm::vec3(2.0f));
    }
    return a;
}

std::vector<uint16_t> SkyImage::toHalfRgba(float scale) const {
    const uint32_t outH = fullHeight();
    std::vector<uint16_t> out(static_cast<size_t>(m_width) * outH * 4);
    for (uint32_t y = 0; y < outH; ++y) {
        for (uint32_t x = 0; x < m_width; ++x) {
            // Half floats top out at 65504: a photographed sun can go past it.
            const glm::vec3 c = glm::min(texel(static_cast<int>(x), static_cast<int>(y)) * scale, glm::vec3(60000.0f));
            const size_t i = (static_cast<size_t>(y) * m_width + x) * 4;
            out[i + 0] = glm::packHalf1x16(c.r);
            out[i + 1] = glm::packHalf1x16(c.g);
            out[i + 2] = glm::packHalf1x16(c.b);
            out[i + 3] = glm::packHalf1x16(1.0f);
        }
    }
    return out;
}

namespace {
bool sameSky(const Sky& a, const Sky& b) {
    return a.kind == b.kind && a.zenith == b.zenith && a.horizon == b.horizon && a.ground == b.ground &&
           a.horizonFalloff == b.horizonFalloff && a.sunColor == b.sunColor && a.sunDiscIntensity == b.sunDiscIntensity &&
           a.sunSizeDegrees == b.sunSizeDegrees && a.sunGlow == b.sunGlow && a.image == b.image && a.yawDegrees == b.yawDegrees &&
           a.intensity == b.intensity && a.lightsScene == b.lightsScene && a.ambientStrength == b.ambientStrength &&
           a.ambientSaturation == b.ambientSaturation;
}
bool sameFog(const Fog& a, const Fog& b) { return a.colorFromSky == b.colorFromSky && a.color == b.color; }
} // namespace

const SkyEnvironment& SkyResolver::resolve(const Sky& sky, const Fog& fog, const glm::vec3& flatAmbient) {
    if (m_valid && sameSky(sky, m_lastSky) && sameFog(fog, m_lastFog) && flatAmbient == m_lastAmbient) return m_env;
    m_valid = true;
    m_lastSky = sky;
    m_lastFog = fog;
    m_lastAmbient = flatAmbient;

    SkyEnvironment e;
    e.kind = sky.kind;
    e.zenith = sky.zenith;
    e.horizon = sky.horizon;
    e.ground = sky.ground;
    e.horizonFalloff = sky.horizonFalloff;
    e.yawRadians = glm::radians(sky.yawDegrees);
    const float radius = glm::radians(std::max(sky.sunSizeDegrees, 0.05f) * 0.5f);
    e.sunCosRadius = std::cos(radius);
    e.sunDisc = sky.sunColor * sky.sunDiscIntensity;
    e.sunGlow = sky.sunColor * sky.sunGlow;

    if (sky.kind == Sky::Kind::Image) {
        std::string error;
        e.image = sky.image.empty() ? nullptr : SkyImage::cached(sky.image, &error);
        if (!e.image) {
            if (m_failedImage != sky.image) {
                log::get("Sky")->warn("{}; showing the gradient sky instead", sky.image.empty() ? std::string("Sky::Kind::Image with no image set") : error);
                m_failedImage = sky.image;
            }
            e.kind = Sky::Kind::Gradient;
        } else {
            const SkyImage::Analysis& a = e.image->analysis();
            e.imageScale = a.normalise * sky.intensity;
            e.zenith = a.zenith * sky.intensity;
            e.horizon = a.horizon * sky.intensity;
            e.ground = a.ground * sky.intensity;
        }
    }

    if (e.kind == Sky::Kind::None || !sky.lightsScene) {
        e.ambient = SkySH::constant(flatAmbient);
    } else if (e.kind == Sky::Kind::Image) {
        // The picture without its sun: the sun lights the scene as
        // lights[0], so counting it here too would light everything twice.
        const float cap = e.image->analysis().sunClamp;
        const SkyImage& img = *e.image;
        const float yaw = sky.yawDegrees;
        const float scale = e.imageScale * sky.ambientStrength;
        e.ambient = SkySH::project([&](const glm::vec3& d) {
            glm::vec3 c = img.sample(rotateYaw(d, -yaw));
            const float l = luminance(c);
            if (l > cap) c *= cap / l;
            return c * scale;
        });
    } else {
        Sky g = sky;
        e.ambient = SkySH::project([&](const glm::vec3& d) { return gradientSkyRadiance(g, d); }) * sky.ambientStrength;
    }
    if (e.kind != Sky::Kind::None && sky.lightsScene) {
        const float keep = std::clamp(sky.ambientSaturation, 0.0f, 2.0f);
        for (glm::vec3& c : e.ambient.c) c = glm::mix(glm::vec3(luminance(c)), c, keep);
    }
    e.ambientAverage = e.ambient.c[0] * 0.282095f;
    // Distant air takes the colour of the sky a little above the horizon:
    // the band right at the horizon is the palest part of any sky, and
    // fogging towards it washes everything out.
    e.fogColor = fog.colorFromSky && e.kind != Sky::Kind::None ? glm::mix(e.horizon, e.zenith, 0.35f) : fog.color;
    m_env = std::move(e);
    return m_env;
}

} // namespace kke
