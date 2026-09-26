#pragma once

#include "kke/Mesh.h"
#include "kke/Module.h"
#include "kke/Ocean.h"
#include "kke/Pipeline.h"

#include <glm/glm.hpp>
#include <memory>

namespace kke {

class Application;

// Draws a kke::OceanWaves sea (shaders/ocean.*) and a gradient sky with a
// sun (shaders/sky.*). The sea is one static grid (uploaded once) that
// follows the camera in whole-cell steps; all wave motion happens in the
// vertex shader, so the CPU cost per frame is one push-constant block.
// Grid: `cells` x `cells` quads over `extent` metres (default 200x200
// over 160 m = 80k triangles; plenty for a sandbox view, trivial for any
// GPU made this decade — lower it for min-spec).
class OceanRenderer {
public:
    OceanRenderer(Application& app, int cells = 200, float extent = 160.0f);
    void drawSky(const RenderContext& ctx, const glm::vec3& sunDirection);
    void drawOcean(const RenderContext& ctx, const OceanWaves& waves, float time, const glm::vec3& cameraPos);

private:
    std::unique_ptr<Pipeline> m_ocean, m_sky;
    std::unique_ptr<Mesh> m_grid;
    float m_cellSize;
};

} // namespace kke
