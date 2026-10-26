#include "kke/DayNight.h"

#include "kke/Application.h"
#include "kke/Mood.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace kke {

namespace {

// A colour (or number) through the day, by the sun's elevation in degrees.
template <typename T, size_t N>
T along(const std::array<float, N>& at, const std::array<T, N>& value, float e) {
    if (e <= at[0]) return value[0];
    for (size_t i = 1; i < N; ++i)
        if (e <= at[i]) {
            const float t = (e - at[i - 1]) / (at[i] - at[i - 1]);
            return value[i - 1] + (value[i] - value[i - 1]) * t;
        }
    return value[N - 1];
}

// Night, deep dusk, dusk, sunset, golden hour, morning, day, high noon.
constexpr std::array<float, 8> kAt{-18.0f, -8.0f, -3.0f, 0.0f, 4.0f, 10.0f, 25.0f, 90.0f};
const std::array<glm::vec3, 8> kZenith{glm::vec3(0.004f, 0.006f, 0.016f), glm::vec3(0.015f, 0.022f, 0.06f), glm::vec3(0.05f, 0.07f, 0.16f),
                                       glm::vec3(0.10f, 0.15f, 0.32f),    glm::vec3(0.14f, 0.26f, 0.55f),   glm::vec3(0.16f, 0.33f, 0.70f),
                                       glm::vec3(0.16f, 0.36f, 0.75f),    glm::vec3(0.14f, 0.34f, 0.78f)};
const std::array<glm::vec3, 8> kHorizon{glm::vec3(0.008f, 0.012f, 0.03f), glm::vec3(0.05f, 0.05f, 0.09f), glm::vec3(0.30f, 0.18f, 0.20f),
                                        glm::vec3(0.80f, 0.42f, 0.22f),   glm::vec3(0.92f, 0.62f, 0.40f), glm::vec3(0.80f, 0.74f, 0.70f),
                                        glm::vec3(0.62f, 0.74f, 0.86f),   glm::vec3(0.60f, 0.73f, 0.88f)};
const std::array<glm::vec3, 8> kSunColor{glm::vec3(1.0f, 0.40f, 0.18f), glm::vec3(1.0f, 0.40f, 0.18f), glm::vec3(1.0f, 0.40f, 0.18f),
                                         glm::vec3(1.0f, 0.40f, 0.18f), glm::vec3(1.0f, 0.62f, 0.36f), glm::vec3(1.0f, 0.82f, 0.62f),
                                         glm::vec3(1.0f, 0.95f, 0.86f), glm::vec3(1.0f, 0.97f, 0.93f)};
constexpr std::array<float, 8> kSunIntensity{0.0f, 0.0f, 0.0f, 0.35f, 1.0f, 2.0f, 2.8f, 3.2f};
constexpr std::array<float, 8> kExposure{2.2f, 1.6f, 1.35f, 1.15f, 1.05f, 1.0f, 1.0f, 1.0f};
constexpr std::array<float, 8> kGlow{0.6f, 0.6f, 0.9f, 1.0f, 0.9f, 0.7f, 0.6f, 0.6f};

const glm::vec3 kMoonColor{0.55f, 0.65f, 1.0f};
constexpr float kMoonIntensity = 0.4f;

float wrap24(float h) {
    h = std::fmod(h, 24.0f);
    return h < 0.0f ? h + 24.0f : h;
}

float smooth(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

float DayNight::sunElevationAt(float h) const {
    h = wrap24(h);
    const float rise = std::clamp(sunrise, 0.0f, 23.0f), set = std::clamp(sunset, rise + 1.0f, 24.0f);
    const float day = set - rise, night = 24.0f - day;
    const float pi = 3.14159265f;
    if (h >= rise && h <= set) return noonElevation * std::sin(pi * (h - rise) / day);
    // Below the horizon: down to 60% of noon's height at midnight's turning point.
    const float since = h > set ? h - set : h + 24.0f - set;
    return -noonElevation * 0.6f * std::sin(pi * since / night);
}

DayNight::Look DayNight::lookAt(float h) const {
    h = wrap24(h);
    Look l;
    const float rise = std::clamp(sunrise, 0.0f, 23.0f), set = std::clamp(sunset, rise + 1.0f, 24.0f);
    l.sunElevation = sunElevationAt(h);
    // East at sunrise, south at midday, west at sunset; round through north by night.
    if (h >= rise && h <= set) l.sunAzimuth = 90.0f + 180.0f * (h - rise) / (set - rise);
    else l.sunAzimuth = 270.0f + 180.0f * (h > set ? h - set : h + 24.0f - set) / (24.0f - (set - rise));
    l.sunAzimuth = std::fmod(l.sunAzimuth, 360.0f);
    l.towardsSun = sunDirectionFrom(l.sunAzimuth, l.sunElevation);

    const float e = l.sunElevation;
    l.sky.kind = Sky::Kind::Gradient;
    l.sky.zenith = along(kAt, kZenith, e);
    l.sky.horizon = along(kAt, kHorizon, e);
    l.sky.ground = l.sky.horizon * 0.25f;
    l.sky.sunColor = along(kAt, kSunColor, e);
    l.sky.sunGlow = along(kAt, kGlow, e);
    l.exposure = along(kAt, kExposure, e);

    const float sun = along(kAt, kSunIntensity, e);
    const float moon = kMoonIntensity * smooth(-3.0f, -10.0f, e);
    if (sun > 0.0f || moon <= 0.0f) {
        // A sun on the horizon would light nothing: keep it a little above for the light.
        l.keyDirection = -sunDirectionFrom(l.sunAzimuth, std::max(e, 2.0f));
        l.keyColor = l.sky.sunColor;
        l.keyIntensity = sun;
    } else {
        // The moon: opposite the sun, never lower than 25 degrees.
        l.moonlit = true;
        l.keyDirection = -sunDirectionFrom(std::fmod(l.sunAzimuth + 180.0f, 360.0f), std::max(25.0f, -e));
        l.keyColor = kMoonColor;
        l.keyIntensity = moon;
    }
    return l;
}

std::vector<DayNight::Crossed> DayNight::advance(float dt) {
    std::vector<Crossed> out;
    if (!running || !(dt > 0.0f) || !(dayLengthSeconds > 0.0f)) return out;
    const float dh = std::min(24.0f, dt / dayLengthSeconds * 24.0f);
    const float from = hour, to = hour + dh;
    // Everything in (from, to], unwrapped so a step past midnight still sees it.
    struct Mark { float at; Event event; };
    std::vector<Mark> marks;
    for (float base : {0.0f, 24.0f}) {
        for (int i = 0; i < 24; ++i) marks.push_back({base + float(i), Event::Hour});
        marks.push_back({base + sunrise, Event::Dawn});
        marks.push_back({base + sunset, Event::Dusk});
    }
    std::stable_sort(marks.begin(), marks.end(), [](const Mark& a, const Mark& b) { return a.at < b.at; });
    for (const Mark& m : marks)
        if (m.at > from && m.at <= to) out.push_back({m.event, int(std::floor(wrap24(m.at) + 1e-4f)) % 24});
    hour = wrap24(to);
    return out;
}

void DayNight::apply(const Look& look, Lighting& lighting, bool withSky) {
    Light& key = lighting.lights[0];
    key.enabled = true;
    key.isDirectional = true;
    key.direction = look.keyDirection;
    key.color = look.keyColor;
    key.intensity = look.keyIntensity;
    lighting.lights[1].enabled = false;
    lighting.exposure = look.exposure;
    if (withSky) {
        const bool lights = lighting.sky.kind == Sky::Kind::None ? true : lighting.sky.lightsScene;
        const float strength = lighting.sky.kind == Sky::Kind::None ? 1.0f : lighting.sky.ambientStrength;
        lighting.sky = look.sky;
        lighting.sky.lightsScene = lights;
        lighting.sky.ambientStrength = strength;
    }
}

bool DayNight::skyChanged(const Sky& a, const Sky& b, float tolerance) {
    if (a.kind != b.kind) return true;
    auto far = [tolerance](const glm::vec3& x, const glm::vec3& y) {
        const glm::vec3 d = glm::abs(x - y);
        const float scale = std::max({x.r, x.g, x.b, y.r, y.g, y.b, 0.02f});
        return std::max({d.r, d.g, d.b}) > tolerance * scale;
    };
    return far(a.zenith, b.zenith) || far(a.horizon, b.horizon) || far(a.sunColor, b.sunColor);
}

} // namespace kke
