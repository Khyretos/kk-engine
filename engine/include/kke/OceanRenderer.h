#pragma once

#include "kke/Mesh.h"
#include "kke/Module.h"
#include "kke/Ocean.h"
#include "kke/Pipeline.h"

#include <glm/glm.hpp>
#include <memory>

namespace kke {

class Application;

// Draws a kke::OceanWaves sea (shaders/ocean.*), reflecting the scene's
// sky (kke::Lighting::sky, drawn by the engine's kke::SkyRenderer). The sea is one static grid (uploaded once) that
// follows the camera in whole-cell steps; all wave motion happens in the
// vertex shader, so the CPU cost per frame is one push-constant block.
// Grid: `cells` x `cells` quads over `extent` metres (default 200x200
// over 160 m = 80k triangles; plenty for a sandbox view, trivial for any
// GPU made this decade — lower it for min-spec).
//
// farExtent > extent: the middle half of the grid keeps its fine cells and
// the outer cells grow toward the edge, so the same vertex count reaches a
// far horizon (a naval battle seen from 100 m away). Waves too short for a
// cell are faded out there (each vertex knows its cell size), so the
// distant sea never flickers.
class OceanRenderer {
public:
    OceanRenderer(Application& app, int cells = 200, float extent = 160.0f, float farExtent = 0.0f);
    void drawOcean(const RenderContext& ctx, const OceanWaves& waves, float time, const glm::vec3& cameraPos);

private:
    std::unique_ptr<Pipeline> m_ocean;
    std::unique_ptr<Mesh> m_grid;
    float m_cellSize;
};

} // namespace kke
