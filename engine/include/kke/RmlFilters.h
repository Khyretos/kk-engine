#pragma once

// CSS filters for the RmlUi Vulkan backend (ENG-G09): what `filter`,
// `backdrop-filter` and a box-shadow's blur compile to, the push constants
// of shaders/rml_filter.frag, and CPU copies of that shader's maths for
// tests/test_rml_filters.cpp. kke::RmlVulkanRenderInterface runs them.

#include <RmlUi/Core/Dictionary.h>
#include <RmlUi/Core/Types.h>
#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace kke {

// What a compiled filter does to a layer.
enum class RmlFilterType { Invalid, Opacity, ColorMatrix, Blur, DropShadow, MaskImage };

// One compiled filter (Rml::RenderInterface::CompileFilter).
struct RmlFilter {
    RmlFilterType type = RmlFilterType::Invalid;
    float opacity = 1.0f;
    glm::mat4 matrix{1.0f};
    float sigma = 0.0f;
    glm::vec4 color{0.0f};
    glm::vec2 offset{0.0f};
};

// How a blur of one sigma runs: passes of a separable kernel of radius taps.
struct RmlBlurPlan {
    int passes = 0;
    float sigma = 0.0f;
    int radius = 0;
};

// The longest kernel arm one blur pass samples.
constexpr int kRmlMaxBlurRadius = 32;

// shaders/rml_filter.frag's push constants, byte for byte.
struct RmlFilterPush {
    glm::mat4 matrix{1.0f};
    glm::vec4 color{0.0f};
    glm::vec2 offset{0.0f};
    glm::vec2 uvMin{0.0f};
    glm::vec2 uvMax{1.0f};
    float sigma = 0.0f;
    int radius = 0;
    int mode = 0;
    float opacity = 1.0f;
};
static_assert(sizeof(RmlFilterPush) == 120, "rml_filter.frag's push constant block is 120 bytes");

// rml_filter.frag's modes.
constexpr int kRmlFilterOpacity = 0;
constexpr int kRmlFilterColorMatrix = 1;
constexpr int kRmlFilterBlur = 2;
constexpr int kRmlFilterDropShadow = 3;
constexpr int kRmlFilterMask = 4;

// RmlUi's filter name and parameters to a filter; Invalid for a name it does not know.
RmlFilter compileRmlFilter(const std::string& name, const Rml::Dictionary& parameters);
// A colour as RmlUi gives it (straight alpha, sRGB) to linear, premultiplied.
glm::vec4 rmlLinearPremultiplied(Rml::Colourb color);
// The passes, per-pass sigma and radius for a Gaussian blur of this sigma (pixels).
RmlBlurPlan rmlBlurPlan(float sigma);
// The kernel weights for taps 0..radius, normalised over both arms, as rml_filter.frag computes them.
std::vector<float> rmlBlurWeights(float sigma, int radius);
// rml_filter.frag's colour-matrix mode on one linear, premultiplied colour.
glm::vec4 rmlApplyColorMatrix(const glm::mat4& matrix, glm::vec4 color);

} // namespace kke
