#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace kke {

// Morphological anti-aliasing on top of MSAA (ENG-F05, the SMAA family:
// Jimenez et al. 2012, after Reshetov's MLAA): one frame, no history, so
// nothing ghosts. MSAA smooths triangle edges; this also catches what MSAA
// can't: edges inside a pixel's shading (alpha-tested leaves, thin
// highlights, a render scale below 1).
//
// 1. Edges: where the perceived brightness of two neighbours differs by
//    more than `threshold`.
// 2. Weights: along each edge, the run of edge pixels is followed both ways
//    (up to `maxSearch`) and its two ends tell the shape the pixels stand
//    for (a step of a slanted line, an L corner, a U notch). Each pixel
//    gets the share of it that the shape's smooth line gives to the
//    neighbour across the edge. The area comes straight from the line, so
//    no precomputed area texture is needed.
// 3. Blend: each pixel mixes in its four neighbours by those shares.
//
// This is the CPU copy and the tests' reference; shaders/smaa_*.comp do the
// same per pixel (kke::SmaaPass).
struct SmaaSettings {
    float threshold = 0.1f; // brightness difference (perceptual, 0..1) that makes an edge
    int maxSearch = 16;     // pixels followed each way along an edge
};

// Perceived brightness of scene light (linear, any range): a gentle tone
// curve, luminance, then a square root for a roughly perceptual scale.
float smaaLuma(const glm::vec3& light);

// The signed area of the smooth line over one pixel of an edge run: the run
// is `before` pixels left of this one plus `after` right of it, its ends
// stand at heights `leftEnd` and `rightEnd` (+0.5: the shape turns towards
// the far side of the edge, -0.5: towards this side, 0: no turn). Returns
// (the area on the far side, the area on this side), both >= 0.
glm::vec2 smaaLineArea(int before, int after, float leftEnd, float rightEnd);

struct SmaaImage {
    int width = 0, height = 0;
    std::function<glm::vec3(int x, int y)> light; // the scene light at pixel (x, y), y down
};

// Per pixel: (x = edge with the left neighbour, y = edge with the top neighbour).
std::vector<glm::bvec2> smaaEdges(const SmaaImage& image, const SmaaSettings& settings);
// Per pixel: how much of each neighbour it takes in (x = left, y = top, z = right, w = bottom).
std::vector<glm::vec4> smaaWeights(int width, int height, const std::vector<glm::bvec2>& edges, const SmaaSettings& settings);
// The anti-aliased image.
std::vector<glm::vec3> smaaBlend(const SmaaImage& image, const std::vector<glm::vec4>& weights);

} // namespace kke
