#pragma once

#include "kke/Buffer.h"
#include "kke/Mesh.h"
#include "kke/Module.h"
#include "kke/Pipeline.h"
#include "kke/Renderer.h"

#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace kke {

class Application;

// Draws many lit spheres cheaply: one camera-facing quad each, shaded in
// impostor.frag as a real sphere (normal, lighting, shadow, correct
// depth) plus optional incandescent glow. For liquid particles, splashes,
// debris, projectiles — anywhere thousands of little balls beat meshes.
// No per-sphere geometry, no instancing extension needed: one vertex
// buffer per frame in flight, grown to the high-water mark, one draw.
class SphereImpostorRenderer {
public:
    explicit SphereImpostorRenderer(Application& app);

    struct Sphere {
        glm::vec3 center;
        float radius;
        glm::vec3 color;     // sRGB albedo
        float glow = 0.0f;   // 0 = none .. 1 = white-hot
        float roughness = 0.4f;
    };
    // Call from render(); `spheres` is only read during the call.
    void draw(const RenderContext& ctx, const std::vector<Sphere>& spheres);

private:
    Application& m_app;
    std::unique_ptr<Pipeline> m_pipeline;
    std::unique_ptr<Buffer> m_buffers[Renderer::kMaxFramesInFlight];
    size_t m_capacity[Renderer::kMaxFramesInFlight] = {};
    std::vector<Vertex> m_scratch;
};

// A mesh rebuilt on the CPU now and then (a melting surface, a procedural
// terrain chunk): upload(...) when it changed, draw(...) every frame. Lit
// by cube.vert + glow.frag (uv.x = glow). Buffers per frame in flight,
// reused; nothing allocated per frame unless the mesh grows.
class DynamicMeshRenderer {
public:
    explicit DynamicMeshRenderer(Application& app);
    void upload(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    void draw(const RenderContext& ctx, const glm::mat4& model = glm::mat4(1.0f), float metallic = 0.0f, float roughness = 0.6f);
    void drawShadow(const ShadowRenderContext& ctx, const glm::mat4& model = glm::mat4(1.0f));

private:
    struct FrameBuffers { std::unique_ptr<Buffer> vertices, indices; size_t vCap = 0, iCap = 0; uint64_t version = 0; };
    void ensureUploaded(uint32_t frame);
    Application& m_app;
    std::unique_ptr<Pipeline> m_pipeline, m_shadowPipeline;
    FrameBuffers m_frames[Renderer::kMaxFramesInFlight];
    std::vector<Vertex> m_vertices;
    std::vector<uint32_t> m_indices;
    uint64_t m_version = 0;
};

} // namespace kke
