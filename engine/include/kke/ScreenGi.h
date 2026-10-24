#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>

namespace kke {

// Screen-space global illumination (ENG-F01): the light a surface gets
// from what is around it on screen (a red wall tints the floor beside it)
// and how much of the sky it still sees (contact shadows, ambient
// occlusion), from the depth and light of a finished frame.
//
// The method is the visibility bitmask (Therrien, Levesque and Gilet,
// "Screen Space Indirect Lighting with Visibility Bitmask", 2023): each
// pixel looks along a few fixed directions on screen ("slices"); in each
// slice its hemisphere is cut into 32 sectors of equal cosine weight, and
// every depth sample along the slice covers the sectors between its front
// and its assumed back (`thickness` behind it), adding its light for the
// sectors it is the first to cover. Unlike horizon-based methods (GTAO,
// most SSGI), which treat everything as infinitely thick, light passes
// behind a thin pole and the pole doesn't darken the whole wall behind it.
//
// No noise and no blending of frames (docs/RENDERING_PRINCIPLES.md): the
// slice directions and sample distances are the same for every pixel and
// every frame. This is the CPU copy and the tests' reference; the shader
// (shaders/screen_gi.comp) does the same per pixel.
struct ScreenGiSettings {
    int slices = 4;          // directions on screen (each looks both ways)
    int steps = 8;           // depth samples each way
    float radius = 1.5f;     // metres: how far a surface gathers from
    float thickness = 0.25f; // metres: how deep each sample is assumed to be
    bool horizonOnly = false; // the reference to beat: every sample infinitely thick (GTAO-style)
};

// A finished frame as the method sees it: a perspective camera looking
// down -z (view space: x right, y up), each pixel's distance along -z and
// the light leaving it towards the camera (scene light, before tone mapping).
struct ScreenGiView {
    int width = 0, height = 0;
    float tanHalfFovY = 0.414f; // tan(fovY / 2)
    std::function<float(int x, int y)> depth;     // metres along -z; <= 0 = nothing there (sky)
    std::function<glm::vec3(int x, int y)> light; // what the pixel shows
};

struct ScreenGiResult {
    glm::vec3 bounce{0.0f}; // light from the surroundings, as ambientIrradiance() gives it (times albedo = what is reflected)
    float visibility = 1.0f; // the share of the (cosine-weighted) hemisphere that sees past everything on screen
};

// The 32 sectors a slice covers from t0 to t1 (each 0..1 across the
// hemisphere, cosine-weighted: equal t steps have equal weight).
uint32_t screenGiSectors(float t0, float t1);
// Where an angle `a` (radians from the view vector, towards the slice's
// side) falls across the hemisphere around the projected normal `n`.
float screenGiSector(float a, float n);
// View-space position of pixel (x, y) at `depth` (the pixel centre).
glm::vec3 screenGiViewPos(const ScreenGiView& view, float x, float y, float depth);
// The method at one pixel with view-space normal `normal`.
ScreenGiResult screenGi(const ScreenGiView& view, int x, int y, const glm::vec3& normal, const ScreenGiSettings& settings = {});

} // namespace kke
