#pragma once

#include "kke/Window.h"
#include "kke/Renderer.h"
#include "kke/DebugUi.h"
#include "kke/Module.h"
#include "kke/EngineError.h"
#include "kke/LightingBuffer.h"
#include "kke/Sky.h"
#include "kke/Viewports.h"
#include "kke/ResourceGovernor.h"
#include "kke/HardwareTarget.h"
#include "kke/UiProfile.h"
#include "kke/ShadowMap.h"
#include "kke/Texture.h"

#include <glm/glm.hpp>
#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kke {

class BenchRecorder;
struct BenchOptions;
class SkyRenderer;
struct Mood;

// A basic orbit-style camera. Not a module itself — nearly every module
// wants to read the camera, so it lives on Application directly rather
// than behind another lookup.
struct Camera {
    glm::vec3 position{2.0f, 2.0f, 2.5f};
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float fovDegrees = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
};

// One light — either directional (uses `direction`, ignores `position`)
// or a point light (uses `position`, ignores `direction`). Kept as one
// struct with a type flag rather than two separate types: the GPU-side
// UBO this feeds needs a fixed-layout array either way, and a shared
// CPU-side struct keeps LightingBuffer's upload code simple (see
// LightingBuffer.h/.cpp — Renderer owns one, updated once per frame
// from whatever's in Application::lighting() when render() runs).
struct Light {
    bool enabled = false;
    bool isDirectional = true;
    glm::vec3 direction{0.0f, -1.0f, 0.0f}; // meaningful only if isDirectional
    glm::vec3 position{0.0f};               // meaningful only if !isDirectional
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
};

// Same reasoning as Camera just above: nearly every module that draws
// real geometry wants to read the current lights, so this lives on
// Application directly rather than behind a getModule<>() lookup.
// kMaxLights is a real, fixed limit (not "as many as you want") because
// the GPU-side UBO this feeds has a fixed-size array — see
// LightingBuffer.h for exactly how that's laid out and why 4 was
// chosen (a small, genuinely useful number for a first real multi-
// light slice, not an arbitrary round number).
// How lit shaders turn scene light (unbounded) into displayable colour
// (shaders/tonemap.glsl). The values are what the shaders read, so keep the
// order. AgX is the default: highlights desaturate towards white instead of
// shifting hue. ACES gives more contrast (the "movie" look); Reinhard is the
// engine's original curve. See docs/RENDERING_PRINCIPLES.md §7.
enum class ToneMapper : int { AgX = 0, ACES = 1, Reinhard = 2 };

// "agx", "aces" or "reinhard" (any case); nullopt for anything else. Used by
// the KKE_TONEMAP override, which forces one curve over whatever the game
// picked, to compare them.
std::optional<ToneMapper> parseToneMapper(const std::string& name);

struct Lighting {
    static constexpr int kMaxLights = 4;
    std::array<Light, kMaxLights> lights;
    glm::vec3 ambientColor{0.15f, 0.15f, 0.15f}; // flat fill light so unlit faces read as dim, not pure black
    // Light 0's shadow map is still rendered either way (so toggling is
    // instant), but lit shaders ignore it when this is false.
    bool shadowsEnabled = true;
    ToneMapper toneMapper = ToneMapper::AgX;
    // Multiplies scene light before tone mapping: > 1 brightens, < 1 darkens.
    float exposure = 1.0f;
    // The sky, the air and the colour look (kke/Sky.h). All off by default;
    // a kke::Mood (Mood.h) sets them together. With a sky that lights the
    // scene, it replaces ambientColor as the fill light.
    Sky sky;
    Fog fog;
    ColorGrade grade;

    Lighting() {
        // A sensible default so a demo that never touches lighting at
        // all still looks like the single-light version this replaced,
        // not suddenly pitch black — matches this project's own
        // "verify no regression" discipline rather than assuming a
        // silent behavior change is fine.
        lights[0].enabled = true;
        lights[0].isDirectional = true;
        lights[0].direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
        lights[0].color = glm::vec3(1.0f, 0.98f, 0.92f);
        lights[0].intensity = 1.0f;
    }
};

// Recorded when a module throws during any lifecycle call — see
// Application::run()'s per-module try/catch and "Debugging" in the
// docs/HISTORY.md for the full reasoning. Kept in call order, oldest first.
//
// friendlyMessage and technicalMessage are ALWAYS both populated,
// regardless of what was actually thrown: a module that throws a plain
// std::exception gets its what() text copied into both (the only text
// available), while a module that throws kke::EngineError (see
// EngineError.h) gets the real plain-language/technical split, plus
// source/file/line when known. DebugControlModule's Emergency Log shows
// friendlyMessage prominently and technicalMessage as a secondary
// detail — designed so a non-programmer gets something actionable
// without the technical text being hidden from someone who wants it.
struct BrokenModuleInfo {
    std::string moduleName;
    std::string stage;             // which lifecycle method threw, e.g. "update", "render"
    std::string friendlyMessage;   // plain language — the thing to show a scripter first
    std::string technicalMessage;  // e.what() — always available, shown as a secondary detail
    ErrorSource source = ErrorSource::Unknown;
    std::string file;              // empty if unknown
    int line = 0;                  // 0 if unknown
};

// Owns the window, renderer, and debug UI; resolves module dependency
// order; drives the Module lifecycle (including a fixed-timestep tick for
// deterministic simulation); runs the main loop.
//
// Cross-module communication has two tiers, deliberately kept separate:
//
//   - getModule<T>()      — "give me the one Concrete module of type T,
//                            if it exists." Use when a module genuinely
//                            needs a specific other module and would be
//                            broken without it (declare it via
//                            dependencies() too, so init order is right
//                            and a missing required one fails loudly).
//
//   - findCapability<T>() — "give me every module that implements
//                            interface T, whatever they are." Use for
//                            loose, optional cross-talk — the "recognize
//                            each other if both present, fine alone if
//                            not" case (see kke/Capabilities.h).
//
// See docs/HISTORY.md's "Cross-module communication" section for the worked
// networking/destruction example.
class Application {
public:
    Application(const std::string& title, uint32_t width, uint32_t height, float fixedUpdateHz = 60.0f);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    template <typename T, typename... Args>
    T& addModule(Args&&... args) {
        auto module = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *module;
        m_modules.push_back(std::move(module));
        return ref;
    }

    // Typed lookup by concrete module type. Returns nullptr if that
    // module wasn't added to this Application — always check.
    template <typename T>
    T* getModule() {
        auto it = m_moduleByType.find(std::type_index(typeid(T)));
        if (it == m_moduleByType.end()) return nullptr;
        return dynamic_cast<T*>(it->second);
    }

    // Every module (whatever its concrete type) that implements interface
    // Capability. Safe to call with zero results — that's the "this
    // module isn't present, so nobody asks for my state" case working as
    // intended, not an error.
    template <typename Capability>
    std::vector<Capability*> findCapability() {
        std::vector<Capability*> result;
        for (auto& m : m_modules) {
            if (auto* c = dynamic_cast<Capability*>(m.get())) {
                result.push_back(c);
            }
        }
        return result;
    }

    void run();

    // The Kreative Kompas intro (kke::LogoIntro) that run() plays before
    // any module's init(). On by default; KKE_SKIP_INTRO=1 skips it for
    // one run whatever this says.
    void setIntroEnabled(bool enabled) { m_introEnabled = enabled; }
    bool introEnabled() const { return m_introEnabled; }

    Window& window() { return m_window; }
    Renderer& renderer() { return *m_renderer; }
    VulkanDevice& device() { return m_renderer->device(); }
    Camera& camera() { return m_camera; }
    // Split screen / picture-in-picture (ACTION_PLAN.md 1.3): each view
    // is a camera drawn into its part of the window (kke/Viewports.h for
    // the usual layouts), in order, so a picture-in-picture goes last.
    // Empty (the default) = one view of camera() over the whole window.
    // Only the first kMaxViews are drawn. Screen-space effects that need
    // a prepass (FluidSurface) follow the first view only.
    struct View {
        Camera camera;
        ViewRect rect;
    };
    std::vector<View>& views() { return m_views; }
    Lighting& lighting() { return m_lighting; }
    // A mood (kke/Mood.h): sky, sun, fill light, fog, look and exposure
    // from one data file in assets/moods ("golden_hour", "night", ...) or
    // the game's own moods/ folder, or a path. False (logged, and with
    // `error`) when it can't be found or read; the lighting is unchanged.
    // KKE_MOOD=<name> replaces whatever the game picks, to try moods
    // on any game without editing it.
    bool setMood(const std::string& nameOrPath, std::string* error = nullptr);
    // The mood last set (name empty before any), e.g. for AudioModule's ambience.
    const Mood& mood() const { return *m_mood; }
    LightingBuffer& lightingBuffer() { return *m_lightingBuffer; }
    // The shadow map sampler's descriptor SET LAYOUT (not the set
    // itself -- that's bound automatically via RenderContext, see
    // Module.h) -- needed by any module drawing through cube.frag at
    // pipeline-creation time, since that pipeline's layout must
    // declare a compatible set 1 whether or not that module's own
    // geometry casts a shadow.
    VkDescriptorSetLayout shadowMapSetLayout() const { return m_shadowMapSetLayout; }
    // The ShadowMap instance itself — needed by real shadow-casting
    // modules (see CubeModule) at pipeline-creation time, to get its
    // render pass (shadowMap().renderPass()) for their own shadow
    // pipeline.
    ShadowMap& shadowMap() { return *m_shadowMap; }
    // Set 2's own layout -- needed by any module drawing through
    // cube.frag at pipeline-creation time, same reasoning as
    // shadowMapSetLayout() above.
    VkDescriptorSetLayout materialTextureSetLayout() const { return m_materialTextureSetLayout; }
    // The default (1x1 white) texture's own descriptor set -- what
    // every object without a real texture of its own binds. Modules
    // with a real texture (see CubeModule) create and bind their own
    // separate descriptor set using the same materialTextureSetLayout()
    // instead of this one.
    VkDescriptorSet defaultTextureDescriptorSet() const { return m_defaultTextureDescriptorSet; }
    VkDescriptorSet shadowMapDescriptorSet() const { return m_shadowMapDescriptorSet; }
    // The engine-wide image cache: loads an image file once (decode + mip
    // chain) and returns a descriptor set for materialTextureSetLayout().
    // Every module asking for the same path shares one GPU copy — a Synty
    // atlas is 2048x2048 (21 MB with mips), and loading it per module both
    // doubled memory and cost ~200 ms per extra load. VK_NULL_HANDLE if the
    // file can't be loaded (logged once).
    VkDescriptorSet textureSet(const std::string& path);

    // --- Debug pause/step ---
    // Freezes simulation (fixedUpdate/update stop advancing) while still
    // rendering every frame, so the same, stable frame keeps presenting
    // for as long as you like — the point being to give an external
    // capture tool (RenderDoc, a Vulkan profiling layer, or just staring
    // at validation-layer output) a frame that isn't changing out from
    // under you. Rendering itself is never paused: a frozen frame still
    // needs to actually get drawn and presented to be inspectable at all.
    // Set every frame by UI modules (RmlUi's UiModule) while the mouse is
    // over, or dragging, one of their elements — so gameplay/camera
    // modules don't also react to a click meant for a button. ImGui has
    // its own equivalent (ImGui::GetIO().WantCaptureMouse); check both.
    void setUiCapturesMouse(bool captured) { m_uiCapturesMouse = captured; }

    // 0 = unlimited. Sleeps at the end of each frame to hold this rate —
    // saves power/heat on laptops and handhelds; combine with VSync off.
    void setFrameRateLimit(float fps) { m_frameRateLimit = fps; }
    float frameRateLimit() const { return m_frameRateLimit; }

    // Set while a full-screen, fully opaque UI (pause menu, marketplace,
    // settings) hides the whole 3D view: the shadow pass, prepass() and
    // render() are skipped, so the frame costs only the clear and the
    // overlay/UI. Simulation (update, fixedUpdate, compute) keeps running.
    // Anything partly see-through must leave this off.
    // docs/RENDERING_PRINCIPLES.md §9, issue #40.
    void setSceneCovered(bool covered) { m_sceneCovered = covered; }
    bool sceneCovered() const { return m_sceneCovered; }

    // The resource governor's budget (kke/ResourceGovernor.h). Starts
    // governed from default settings (KKE_USE_EVERYTHING=1 lifts it);
    // SettingsModule replaces it from the player's settings. Modules read
    // workerThreads when they start; the frame caps apply every frame,
    // with backgroundFrameRate while the window is unfocused or minimized.
    void setResourceBudget(const ResourceBudget& budget);
    const ResourceBudget& resourceBudget() const { return m_budget; }
    // The cap the last frame actually used (0 = none).
    float effectiveFrameRateLimit() const { return m_effectiveLimit; }

    // The hardware target this device runs as (kke/HardwareTarget.h),
    // chosen once at startup: KKE_TARGET, the build's KKE_DEFAULT_TARGET,
    // or the recognised device. Its settings are the defaults a missing
    // settings.json key falls back to.
    const HardwareTarget& hardwareTarget() const { return *m_target.target; }
    EngineSettings targetDefaultSettings() const { return settingsForTarget(*m_target.target); }

    // What kind of screen the menus are made for (kke/UiProfile.h): phone,
    // desktop or console, from the hardware target or KKE_UI_PROFILE.
    UiProfile uiProfile() const { return m_uiProfile; }
    // The part of the drawn frame (renderer extent, pixels) that menus and
    // HUDs keep to: the system's safe area (notch, rounded corners, status
    // and navigation bars) plus the profile's standard margin. RmlUi and
    // the ImGui panels both lay out inside it, so nothing is cut off at
    // the screen's edges.
    ScreenRect uiSafeRect() const;

    // The ImGui developer overlay (Performance, Physics, Camera panels...).
    DebugUi& debugUi() { return *m_debugUi; }
    bool uiCapturesMouse() const { return m_uiCapturesMouse; }

    bool isPaused() const { return m_paused; }
    void setPaused(bool paused) { m_paused = paused; }
    // Advances the simulation by exactly one fixed tick + one update()
    // call, then re-freezes — for stepping through frames one at a time
    // while paused. A no-op if not currently paused.
    void stepOneFrame() { if (m_paused) m_stepRequested = true; }
    // Slow motion (below 1) or fast forward (above 1): game time runs at
    // this times wall-clock time. There are as many fixed ticks per
    // second as ever, each one a step of fixedDt x scale (so slow motion
    // stays smooth, and a shorter step is only more stable; keep fast
    // forward modest), and update() gets dt x scale. Real time for a
    // camera or UI: dt / timeScale(). A replay of a hit in slow motion
    // shows FEMFX squash that lasts a few ms (games/tennis).
    void setTimeScale(float scale) { m_timeScale = scale > 0.0f ? scale : 1.0f; }
    float timeScale() const { return m_timeScale; }

    // The most fixedUpdate() ticks a single rendered frame may run to
    // catch up with wall-clock time. When the simulation can't keep up
    // (a big fracture event, a slow machine) the leftover time is
    // dropped: the world briefly runs in slow motion, but the frame rate
    // stays usable. The old cap was 8, which turned one expensive tick
    // into eight per frame — measured at 0.4 FPS in physics_demo — and
    // made the overload worse instead of absorbing it. 1 = never catch
    // up (pure slow-motion under load); 0 is treated as 1. A tick that
    // itself costs more wall time than it simulates is never followed by
    // a catch-up tick in the same frame, whatever this cap says.
    void setMaxFixedStepsPerFrame(uint32_t steps) { m_maxFixedStepsPerFrame = steps ? steps : 1; }
    uint32_t maxFixedStepsPerFrame() const { return m_maxFixedStepsPerFrame; }
    // Fixed ticks actually run during the most recent frame — so a stats
    // panel can show when the simulation is falling behind.
    uint32_t fixedStepsLastFrame() const { return m_fixedStepsLastFrame; }
    // UpdateContext::alpha of the current frame, for code that draws
    // outside update() (render, renderShadow).
    float fixedAlpha() const { return m_fixedAlpha; }

    // --- Per-module error isolation ---
    // See run()'s per-module try/catch: a module that throws during any
    // lifecycle call gets recorded here and is never called again for
    // the rest of this session (not even shutdown() — see run()'s
    // comment on that tradeoff). Everything else keeps running.
    const std::vector<BrokenModuleInfo>& brokenModules() const { return m_brokenModuleInfos; }

    // --- Benchmark (kke/BenchRecorder.h, docs/BENCHMARKS.md) ---
    // Started by KKE_BENCHMARK=<seconds> (tools/kke_benchmark sets it), or
    // by calling startBenchmark() before run() (a launcher without
    // environment variables, e.g. Android). The game runs as usual; run()
    // records every frame, writes the report and ends when the time is up.
    void startBenchmark(const BenchOptions& options);
    // The running benchmark, nullptr when there is none. A game may call
    // benchmark()->addEvent("wave 3") so hitches carry what it was doing.
    BenchRecorder* benchmark() { return m_bench.get(); }

private:
    void resolveInitOrder();
    // Points the shadow map's descriptor set at its image (again after
    // ShadowMap::setTiles recreated it).
    void writeShadowMapDescriptor();
    void playIntro();
    void writeBenchmarkReport();
    void safeInvoke(Module* m, const char* stage, const std::function<void()>& fn);

    Window m_window;
    std::unique_ptr<Renderer> m_renderer; // created after window, needs it for the surface
    std::unique_ptr<DebugUi> m_debugUi;
    Camera m_camera;
    Lighting m_lighting;
    std::optional<ToneMapper> m_toneMapperOverride; // KKE_TONEMAP
    std::unique_ptr<LightingBuffer> m_lightingBuffer;
    std::vector<View> m_views;
    // Views after the first: each its own lighting block (camera position
    // and view-projection differ per view).
    std::vector<std::unique_ptr<LightingBuffer>> m_viewLighting;
    // Draws m_lighting.sky; made the first time a sky is switched on.
    std::unique_ptr<SkyRenderer> m_skyRenderer;
    std::unique_ptr<Mood> m_mood;
    std::string m_moodOverride; // KKE_MOOD
    // Real shadow mapping infrastructure -- see kke::ShadowMap's own
    // class comment for the full scope (single directional light, one
    // shadow-casting pass, no PCF/soft shadows yet). The descriptor set
    // layout/pool/set here are for the shadow map *sampler* specifically
    // (set 1 in cube.frag) -- a separate, small piece of Vulkan state
    // from ShadowMap's own render pass/framebuffer, owned here rather
    // than inside ShadowMap itself because every pipeline that shares
    // cube.frag (CubeModule, PhysicsModule, DestructionModule) needs
    // this same layout at pipeline-creation time, before any of them
    // necessarily has a live ShadowMap reference yet.
    std::unique_ptr<ShadowMap> m_shadowMap;
    VkDescriptorSetLayout m_shadowMapSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_shadowMapDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_shadowMapDescriptorSet = VK_NULL_HANDLE;

    // The material albedo texture's own descriptor infrastructure (set
    // 2 in cube.frag) -- same reasoning, same pattern, as the shadow
    // map's own set above. m_defaultWhiteTexture/m_defaultTextureDescriptorSet
    // are what every object sharing cube.frag binds unless it has a
    // real texture of its own (see kke::Texture's own class comment,
    // and CubeModule for the first real, non-default consumer).
    std::unique_ptr<Texture> m_defaultWhiteTexture;
    VkDescriptorSetLayout m_materialTextureSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_materialTextureDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_defaultTextureDescriptorSet = VK_NULL_HANDLE;
    struct CachedTexture { std::unique_ptr<Texture> texture; VkDescriptorSet set = VK_NULL_HANDLE; };
    std::unordered_map<std::string, CachedTexture> m_textureCache;
    VkDescriptorPool m_textureCachePool = VK_NULL_HANDLE;
    float m_fixedDt;
    uint32_t m_maxFixedStepsPerFrame = 2;
    uint32_t m_fixedStepsLastFrame = 0;
    float m_fixedAlpha = 1.0f;

    std::vector<std::unique_ptr<Module>> m_modules;
    std::vector<Module*> m_initOrder; // m_modules reordered so dependencies come first
    std::unordered_map<std::type_index, Module*> m_moduleByType;

    bool m_paused = false;
    float m_timeScale = 1.0f; // setTimeScale()
    bool m_introEnabled = true;
    bool m_uiCapturesMouse = false;
    float m_frameRateLimit = 0.0f;
    bool m_sceneCovered = false;
    ResourceBudget m_budget;
    TargetChoice m_target;
    UiProfile m_uiProfile = UiProfile::Desktop;
    float m_effectiveLimit = 0.0f;
    bool m_stepRequested = false;

    std::unordered_set<Module*> m_faultedModules; // see safeInvoke() — never called again once here
    std::vector<BrokenModuleInfo> m_brokenModuleInfos;
    std::string m_title;
    std::chrono::steady_clock::time_point m_createdAt = std::chrono::steady_clock::now();
    std::unique_ptr<BenchRecorder> m_bench;
    std::unordered_map<const Module*, int> m_benchModuleIndex; // filled once init() is done
    // The benchmark's screenshots: requests waiting for their frame, and
    // the newest picture of each kind (written with the report).
    struct BenchShots;
    std::unique_ptr<BenchShots> m_benchShots;
    void writeBenchmarkShots(const std::filesystem::path& dir, const std::string& name);
};

} // namespace kke
