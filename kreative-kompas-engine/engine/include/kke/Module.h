#pragma once

#include <volk.h>
#include <glm/glm.hpp>

namespace kke {

class Application;

// Per-frame CPU-side timing handed to Module::update().
struct UpdateContext {
    float dt;         // seconds since last frame
    float totalTime;  // seconds since Application started
};

// Per-frame GPU-side context handed to Module::render()/Module::compute().
// Everything a module needs to draw is here — it should never need to reach
// back into Application/Renderer internals to get a camera matrix.
struct RenderContext {
    VkCommandBuffer cmd;
    glm::mat4 view;
    glm::mat4 proj;      // already has the Vulkan Y-flip applied
    glm::vec3 cameraPos;
    float aspectRatio;
    VkRenderPass renderPass;
};

// The plug-in boundary for everything that isn't core frame plumbing:
// gameplay, a grid, particles, a destruction sim, a networking client...
// Each is a Module. Application owns a list of them and drives their
// lifecycle. See docs/ADDING_A_MODULE.md (or the README section of the
// same name) for a worked example.
//
// A module that doesn't render anything (e.g. a NetworkModule that just
// ships/receives packets) simply doesn't override render()/compute() —
// the defaults are no-ops, so partial implementations are the norm, not
// something you need to work around.
class Module {
public:
    virtual ~Module() = default;

    // Short identifier, shown in engine logs and the module-list UI.
    virtual const char* name() const = 0;

    // Called once, after the Renderer (and therefore the Vulkan device and
    // render pass) exists — this is where you create pipelines, buffers,
    // and any other GPU resources this module owns.
    virtual void init(Application& app) {}

    // Called once per frame, before any rendering. CPU-side simulation:
    // physics, AI, networking polls, gameplay logic, etc.
    virtual void update(const UpdateContext& ctx) {}

    // Called once per frame, before the render pass begins. This is the
    // only place it's legal to record compute dispatches or the barriers
    // that follow them (e.g. ParticleModule's simulation step) — Vulkan
    // doesn't allow compute work inside a graphics render pass.
    virtual void compute(VkCommandBuffer cmd) {}

    // Called once per frame, with the render pass already active. Bind a
    // pipeline, push constants, and issue draw calls here.
    virtual void render(const RenderContext& ctx) {}

    // Called once per frame, before the render pass begins, to build any
    // ImGui:: panels this module wants on screen (stats, debug toggles...).
    virtual void renderUi() {}

    // Called once, before the Renderer (and Vulkan device) is torn down.
    // Destroy GPU resources here rather than relying on your destructor
    // running at the right time relative to device teardown.
    virtual void shutdown() {}
};

} // namespace kke
