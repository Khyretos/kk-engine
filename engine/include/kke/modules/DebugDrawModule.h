#pragma once

#include "kke/Module.h"
#include "kke/Buffer.h"
#include "kke/Mesh.h"
#include "kke/Pipeline.h"
#include "kke/Renderer.h"

#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace kke {

// Immediate-mode debug lines: call line()/box()/grid() from update() and
// they're drawn this frame (and stay while the simulation is paused).
// Used by editors and tools — selection boxes, placement grids, gizmos.
//
// Performance (see docs/OPTIMIZATION.md): every line of a frame is expanded
// into one camera-facing quad (6 vertices) in a single vertex buffer per
// frame in flight, drawn with one call per layer (depth-tested, and
// "on top"). Buffers grow to the high-water mark and are reused; nothing
// is allocated per frame once warmed up.
class DebugDrawModule : public Module {
public:
    const char* name() const override { return "DebugDraw"; }
    void init(Application& app) override;
    void update(const UpdateContext& ctx) override; // clears last frame's lines
    void render(const RenderContext& ctx) override;

    void line(const glm::vec3& a, const glm::vec3& b, const glm::vec3& color, float thickness = 0.02f, bool onTop = false);
    void box(const glm::vec3& min, const glm::vec3& max, const glm::vec3& color, float thickness = 0.02f, bool onTop = false);
    // Box with its own transform (e.g. a rotated prop's local bounds).
    void box(const glm::mat4& transform, const glm::vec3& localMin, const glm::vec3& localMax, const glm::vec3& color,
             float thickness = 0.02f, bool onTop = false);
    void gridXZ(const glm::vec3& center, float halfSize, float step, const glm::vec3& color, float thickness = 0.01f);
    void cross(const glm::vec3& p, float size, const glm::vec3& color, bool onTop = true);

private:
    struct Line { glm::vec3 a, b, color; float thickness; };
    std::vector<Line> m_lines[2]; // [0] depth-tested, [1] on top
    std::vector<Vertex> m_scratch;
    std::unique_ptr<Buffer> m_buffers[Renderer::kMaxFramesInFlight];
    size_t m_capacity[Renderer::kMaxFramesInFlight] = {};
    std::unique_ptr<Pipeline> m_pipeline, m_pipelineOnTop;
    Application* m_app = nullptr;
};

} // namespace kke
