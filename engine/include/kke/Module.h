#pragma once

#include <volk.h>
#include <glm/glm.hpp>
#include <SDL3/SDL.h>
#include <typeindex>
#include <vector>

namespace kke {

class Application;

// Per-frame CPU-side timing handed to Module::update(). Use this for
// anything that's fine drifting with frame rate: camera easing, UI
// animation, polling a socket for new packets.
struct UpdateContext {
    float dt;         // seconds since last frame (variable)
    float totalTime;  // seconds since Application started
};

// Fixed-rate timing handed to Module::fixedUpdate(). Runs at a constant
// tick rate (60 Hz by default) regardless of render frame rate — use this
// for anything that needs to be deterministic or tick-addressable:
// physics integration, animation state machines, and anything a
// NetworkModule might need to reconcile against a specific simulation
// step. tickIndex is the same number on every peer running the same
// inputs from the same seed, which is what makes "replicate a seed and a
// trigger tick instead of the resulting geometry" (see DestructionModule)
// actually work.
struct FixedUpdateContext {
    float fixedDt;      // constant, e.g. 1/60
    uint64_t tickIndex; // monotonically increasing simulation tick
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
    // Set 0, binding 0 in every lit pipeline — see kke::LightingBuffer.
    // Threaded through here rather than requiring every module to
    // separately cache an Application* just to reach
    // app.lightingBuffer(), matching how view/proj/cameraPos already
    // flow through this same struct instead of a lookup.
    VkDescriptorSet lightingDescriptorSet = VK_NULL_HANDLE;
};

// A dependency one module declares on another, by concrete type. Declaring
// a dependency changes two things: (1) init() order — dependencies are
// always init()'d before the modules that depend on them; (2) whether
// Application::run() refuses to start. required=true means "don't run
// without this"; required=false just documents an ordering preference —
// the module is expected to check Application::getModule<T>() itself at
// runtime and behave correctly if it comes back null.
//
// This is for *hard*, named dependencies ("physics must init before
// gameplay reads its results"). For *soft*, optional cross-talk between
// modules that may or may not both be present — the networking/destruction
// case — use a capability interface (kke/Capabilities.h) and
// Application::findCapability<T>() instead. Don't declare a
// ModuleDependency just so you can call findCapability() later; only
// declare one if init order or presence genuinely matters.
struct ModuleDependency {
    std::type_index type;
    bool required = true;
    const char* reason = "";
};

// The plug-in boundary for everything that isn't core frame plumbing:
// gameplay, a grid, particles, a destruction sim, a networking client...
// Each is a Module. Application owns a list of them and drives their
// lifecycle. See the README's "Adding a module" and "Cross-module
// communication" sections for worked examples.
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

    // Modules this one needs to exist (or would like to exist) before it.
    // Application resolves init() order from this graph and throws if a
    // required dependency is missing or a cycle exists. Default: no
    // dependencies — most modules, especially standalone ones like a
    // NetworkModule that only cares about capabilities, don't need this.
    virtual std::vector<ModuleDependency> dependencies() const { return {}; }

    // Called once, after the Renderer (and therefore the Vulkan device and
    // render pass) exists — this is where you create pipelines, buffers,
    // and any other GPU resources this module owns. Called in dependency
    // order (dependencies first).
    virtual void init(Application& app) {}

    // Called at a fixed tick rate (see FixedUpdateContext) — deterministic
    // simulation goes here, not in update().
    virtual void fixedUpdate(const FixedUpdateContext& ctx) {}

    // Called once per frame, before any rendering, at the variable render
    // frame rate. CPU-side work that's fine drifting with frame rate.
    virtual void update(const UpdateContext& ctx) {}

    // Called once per frame, before the render pass begins. This is the
    // only place it's legal to record compute dispatches or the barriers
    // that follow them (e.g. ParticleModule's simulation step) — Vulkan
    // doesn't allow compute work inside a graphics render pass.
    virtual void compute(VkCommandBuffer cmd) {}

    // Called for every raw SDL event, before update()/fixedUpdate() for
    // that frame — the same events DebugUi already sees for ImGui, now
    // available to any module (UiModule forwards these into RmlUi's
    // Context; a future chat module might read SDL_EVENT_TEXT_INPUT the
    // same way). Most modules don't need this and shouldn't override it —
    // OrbitCameraModule, for example, deliberately polls
    // Window::mouseState() once per frame instead, which is simpler for
    // "what's the net drag this frame" than reconstructing it from
    // individual motion events.
    virtual void onEvent(const SDL_Event& event) {}

    // Called once per frame, with the render pass already active. Bind a
    // pipeline, push constants, and issue draw calls here.
    virtual void render(const RenderContext& ctx) {}

    // Called once per frame, before the render pass begins, to build any
    // ImGui:: panels this module wants on screen (stats, debug toggles...).
    virtual void renderUi() {}

    // Called once, before the Renderer (and Vulkan device) is torn down.
    // Destroy GPU resources here rather than relying on your destructor
    // running at the right time relative to device teardown. Called in
    // reverse dependency order (dependents shut down before what they
    // depend on).
    virtual void shutdown() {}
};

} // namespace kke
