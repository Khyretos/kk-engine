#pragma once

#include "kke/Buffer.h"
#include "kke/Cloth.h"
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
// by cube.vert + glow.frag (uv.x = glow), or translucent (drawTranslucent). Buffers per frame in flight,
// reused; nothing allocated per frame unless the mesh grows.
class DynamicMeshRenderer {
public:
    explicit DynamicMeshRenderer(Application& app);
    ~DynamicMeshRenderer();
    DynamicMeshRenderer(const DynamicMeshRenderer&) = delete;
    DynamicMeshRenderer& operator=(const DynamicMeshRenderer&) = delete;
    void upload(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    void draw(const RenderContext& ctx, const glm::mat4& model = glm::mat4(1.0f), float metallic = 0.0f, float roughness = 0.6f);
    void drawShadow(const ShadowRenderContext& ctx, const glm::mat4& model = glm::mat4(1.0f));
    // Translucent material (jelly, gummy, coloured glass, slime): what's
    // behind shows through, filtered by the colour, more so where the
    // surface is seen face-on (thin) than at the edges (thick), with
    // Fresnel reflections, light scattered inside and light shining
    // through from behind. Two passes over the front faces: one
    // multiplies what's already drawn by the transmittance, one adds the
    // light. Draw it after everything opaque. Per vertex: color = tint,
    // uv.x = density (absorption; 0 = clear, ~2-6 = jelly), uv.y =
    // milkiness (0 = clear, 1 = opaque-ish, like panna cotta). With
    // ctx.refraction the first pass bends what's behind instead, by the
    // index of refraction `ior` (water 1.33, glass 1.5; 1 = straight).
    void drawTranslucent(const RenderContext& ctx, const glm::mat4& model = glm::mat4(1.0f), float roughness = 0.08f, float ior = 1.33f);
    // Fabric (cloth.frag): two-sided, woven, with sheen and thread gloss
    // from the Fabric (kke/Cloth.h). Per vertex: color = sRGB albedo
    // (usually fabric.color), normal, uv = cloth coordinates in metres.
    void drawCloth(const RenderContext& ctx, const Fabric& fabric, const glm::mat4& model = glm::mat4(1.0f));

    // What ray tracing sees of the mesh (ENG-F02): draw() and drawCloth()
    // add it to the frame's ray scene while ray-traced reflections are on
    // (Application::recordRayDraw): this frame slot's buffers (their
    // addresses; kke::Vertex, 44 bytes), or for KKE and a mesh of more than
    // 20000 triangles a box round the vertices, in their average colour.
    struct RayView {
        VkDeviceAddress vertices = 0, indices = 0;
        const void* vertexBuffer = nullptr; // changes when the buffer is remade (a new geometry then)
        uint32_t vertexCount = 0, indexCount = 0;
        uint64_t version = 0;     // changes with every upload()
        glm::vec3 lo{0.0f}, hi{0.0f}; // the vertices' bounds (model space)
        glm::vec3 color{1.0f};    // their average colour, linear
    };
    RayView rayView(uint32_t frame) const;

private:
    struct FrameBuffers { std::unique_ptr<Buffer> vertices, indices; size_t vCap = 0, iCap = 0; uint64_t version = 0; };
    void ensureUploaded(uint32_t frame);
    Application& m_app;
    // Shared by every DynamicMeshRenderer of the application (made once).
    std::shared_ptr<Pipeline> m_pipeline, m_shadowPipeline;
    std::shared_ptr<Pipeline> m_absorbPipeline, m_lightPipeline, m_refractPipeline; // drawTranslucent(), made on first use
    std::shared_ptr<Pipeline> m_refractRaysPipeline; // drawTranslucent() tracing refracted rays, made on first use
    std::shared_ptr<Pipeline> m_clothPipeline;                   // drawCloth(), made on first use
    // raySet: bound as set 3 (the refracting pass with rays, RenderContext::refractionRays).
    void bindAndDraw(const RenderContext& ctx, Pipeline& pipeline, const glm::mat4& model, float metallic, float roughness,
                     VkDescriptorSet raySet = VK_NULL_HANDLE);
    FrameBuffers m_frames[Renderer::kMaxFramesInFlight];
    std::vector<Vertex> m_vertices;
    std::vector<uint32_t> m_indices;
    uint64_t m_version = 0;
    glm::vec3 m_lo{0.0f}, m_hi{0.0f}, m_color{1.0f}; // upload(): for rayView()
};

} // namespace kke
