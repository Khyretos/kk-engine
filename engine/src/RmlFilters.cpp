#include "kke/RmlFilters.h"

#include <RmlUi/Core/Variant.h>

#include <algorithm>
#include <cmath>

namespace kke {

namespace {
// A colour matrix written row by row (glm stores columns).
glm::mat4 rows(glm::vec4 r0, glm::vec4 r1, glm::vec4 r2, glm::vec4 r3) {
    return glm::transpose(glm::mat4(r0, r1, r2, r3));
}

float srgbToLinear(float c) {
    return c < 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float c) {
    return c < 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}
} // namespace

// The same filters and matrices as RmlUi's GL3 backend (RenderInterface_GL3::CompileFilter).
RmlFilter compileRmlFilter(const std::string& name, const Rml::Dictionary& parameters) {
    RmlFilter f;
    const float value = Rml::Get(parameters, "value", 1.0f);
    if (name == "opacity") {
        f.type = RmlFilterType::Opacity;
        f.opacity = std::clamp(value, 0.0f, 1.0f);
    } else if (name == "blur") {
        f.type = RmlFilterType::Blur;
        f.sigma = std::max(0.0f, Rml::Get(parameters, "sigma", 1.0f));
    } else if (name == "drop-shadow") {
        f.type = RmlFilterType::DropShadow;
        f.sigma = std::max(0.0f, Rml::Get(parameters, "sigma", 0.0f));
        f.color = rmlLinearPremultiplied(Rml::Get(parameters, "color", Rml::Colourb()));
        const Rml::Vector2f offset = Rml::Get(parameters, "offset", Rml::Vector2f(0.0f));
        f.offset = glm::vec2(offset.x, offset.y);
    } else if (name == "brightness") {
        f.type = RmlFilterType::ColorMatrix;
        f.matrix = rows({ value, 0, 0, 0 }, { 0, value, 0, 0 }, { 0, 0, value, 0 }, { 0, 0, 0, 1 });
    } else if (name == "contrast") {
        f.type = RmlFilterType::ColorMatrix;
        const float grey = 0.5f - 0.5f * value;
        f.matrix = rows({ value, 0, 0, grey }, { 0, value, 0, grey }, { 0, 0, value, grey }, { 0, 0, 0, 1 });
    } else if (name == "invert") {
        f.type = RmlFilterType::ColorMatrix;
        const float v = std::clamp(value, 0.0f, 1.0f);
        const float inverted = 1.0f - 2.0f * v;
        f.matrix = rows({ inverted, 0, 0, v }, { 0, inverted, 0, v }, { 0, 0, inverted, v }, { 0, 0, 0, 1 });
    } else if (name == "grayscale") {
        f.type = RmlFilterType::ColorMatrix;
        const float rev = 1.0f - value;
        const glm::vec3 g = value * glm::vec3(0.2126f, 0.7152f, 0.0722f);
        f.matrix = rows({ g.x + rev, g.y, g.z, 0 }, { g.x, g.y + rev, g.z, 0 }, { g.x, g.y, g.z + rev, 0 }, { 0, 0, 0, 1 });
    } else if (name == "sepia") {
        f.type = RmlFilterType::ColorMatrix;
        const float rev = 1.0f - value;
        const glm::vec3 r = value * glm::vec3(0.393f, 0.769f, 0.189f);
        const glm::vec3 g = value * glm::vec3(0.349f, 0.686f, 0.168f);
        const glm::vec3 b = value * glm::vec3(0.272f, 0.534f, 0.131f);
        f.matrix = rows({ r.x + rev, r.y, r.z, 0 }, { g.x, g.y + rev, g.z, 0 }, { b.x, b.y, b.z + rev, 0 }, { 0, 0, 0, 1 });
    } else if (name == "hue-rotate") {
        f.type = RmlFilterType::ColorMatrix;
        const float s = std::sin(value), c = std::cos(value);
        f.matrix = rows({ 0.213f + 0.787f * c - 0.213f * s, 0.715f - 0.715f * c - 0.715f * s, 0.072f - 0.072f * c + 0.928f * s, 0 },
                        { 0.213f - 0.213f * c + 0.143f * s, 0.715f + 0.285f * c + 0.140f * s, 0.072f - 0.072f * c - 0.283f * s, 0 },
                        { 0.213f - 0.213f * c - 0.787f * s, 0.715f - 0.715f * c + 0.715f * s, 0.072f + 0.928f * c + 0.072f * s, 0 },
                        { 0, 0, 0, 1 });
    } else if (name == "saturate") {
        f.type = RmlFilterType::ColorMatrix;
        f.matrix = rows({ 0.213f + 0.787f * value, 0.715f - 0.715f * value, 0.072f - 0.072f * value, 0 },
                        { 0.213f - 0.213f * value, 0.715f + 0.285f * value, 0.072f - 0.072f * value, 0 },
                        { 0.213f - 0.213f * value, 0.715f - 0.715f * value, 0.072f + 0.928f * value, 0 },
                        { 0, 0, 0, 1 });
    }
    return f;
}

// sRGB to linear per channel, then premultiplied by alpha.
glm::vec4 rmlLinearPremultiplied(Rml::Colourb color) {
    const float a = color.alpha / 255.0f;
    return glm::vec4(srgbToLinear(color.red / 255.0f) * a, srgbToLinear(color.green / 255.0f) * a,
                     srgbToLinear(color.blue / 255.0f) * a, a);
}

glm::vec4 rmlTargetColor(glm::vec4 c, bool targetEncoded) {
    if (!targetEncoded || c.a <= 0.0f) return targetEncoded ? glm::vec4(0.0f) : c;
    return glm::vec4(linearToSrgb(c.r / c.a) * c.a, linearToSrgb(c.g / c.a) * c.a, linearToSrgb(c.b / c.a) * c.a, c.a);
}

// Three sigmas each side, at most kRmlMaxBlurRadius taps: a wider blur runs
// as n passes of sigma / sqrt(n), which add up to the same Gaussian.
RmlBlurPlan rmlBlurPlan(float sigma) {
    RmlBlurPlan plan;
    if (!(sigma >= 0.1f)) return plan;
    plan.passes = 1;
    plan.sigma = sigma;
    while (std::ceil(3.0f * plan.sigma) > static_cast<float>(kRmlMaxBlurRadius)) {
        ++plan.passes;
        plan.sigma = sigma / std::sqrt(static_cast<float>(plan.passes));
    }
    plan.radius = std::max(1, static_cast<int>(std::ceil(3.0f * plan.sigma)));
    return plan;
}

// From sigma 3 on, halving the image loses no detail a blur would keep.
bool rmlBlurAtHalfSize(float sigma) {
    return sigma >= 3.0f;
}

// exp(-i^2 / (2 sigma^2)) for each tap, divided by the sum over both arms.
std::vector<float> rmlBlurWeights(float sigma, int radius) {
    std::vector<float> w(static_cast<size_t>(std::max(0, radius)) + 1);
    float total = 0.0f;
    for (int i = 0; i <= radius; ++i) {
        w[static_cast<size_t>(i)] = std::exp(-static_cast<float>(i * i) / (2.0f * sigma * sigma));
        total += (i == 0 ? 1.0f : 2.0f) * w[static_cast<size_t>(i)];
    }
    for (float& x : w) x /= total;
    return w;
}

// rml_filter.frag's loop: taps i and i + 1 as one sample at their weighted
// position, so a linear-filtered fetch there returns both.
std::vector<RmlBlurTap> rmlBlurTaps(float sigma, int radius) {
    std::vector<RmlBlurTap> taps;
    for (int i = 1; i <= radius; i += 2) {
        const float w1 = std::exp(-static_cast<float>(i * i) / (2.0f * sigma * sigma));
        const float w2 = i + 1 <= radius ? std::exp(-static_cast<float>((i + 1) * (i + 1)) / (2.0f * sigma * sigma)) : 0.0f;
        const float w = w1 + w2;
        taps.push_back({ (static_cast<float>(i) * w1 + static_cast<float>(i + 1) * w2) / w, w });
    }
    return taps;
}

// Un-premultiply, encode to sRGB, apply the matrix in premultiplied sRGB
// space (RmlUi's matrices are made for it), then back to linear.
glm::vec4 rmlApplyColorMatrix(const glm::mat4& matrix, glm::vec4 color) {
    if (color.a <= 0.0f) return glm::vec4(0.0f);
    const glm::vec3 straight = glm::vec3(color) / color.a;
    const glm::vec3 s(linearToSrgb(straight.r), linearToSrgb(straight.g), linearToSrgb(straight.b));
    const glm::vec4 t = matrix * glm::vec4(s * color.a, color.a);
    const glm::vec3 o = glm::clamp(glm::vec3(t) / color.a, 0.0f, 1.0f);
    return glm::vec4(srgbToLinear(o.r) * color.a, srgbToLinear(o.g) * color.a, srgbToLinear(o.b) * color.a, color.a);
}

} // namespace kke
