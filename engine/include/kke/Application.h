#pragma once

#include "kke/Window.h"
#include "kke/Renderer.h"
#include "kke/DebugUi.h"
#include "kke/Module.h"
#include "kke/EngineError.h"
#include "kke/LightingBuffer.h"
#include "kke/Sky.h"
#include "kke/ToneMap.h"
#include "kke/Viewports.h"
#include "kke/ResourceGovernor.h"
#include "kke/HardwareTarget.h"
#include "kke/UiProfile.h"
#include "kke/RayScene.h"
#include "kke/Reflections.h"
#include "kke/ColorLut.h"
#include "kke/Decals.h"
#include "kke/VolumetricFog.h"
#include "kke/Taa.h"
#include "kke/ShadowRays.h"
#include "kke/ScreenGi.h"
#include "kke/ShadowMap.h"
#include "kke/Texture.h"

#include <glm/glm.hpp>
#include <algorithm>
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
class ScreenGiPass;
class ReflectionPass;
class BehindPass;
class ClusterLightPass;
class DecalTextures;
class ShadowRayPass;
class VolumetricFogPass;
class RefractionRays;
struct ReflectionRays;
struct SkyEnvironment;
class DynamicMeshRenderer;
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
    // Point lights only: cast shadows (ENG-G04, ShadowMap::pointFaceViewProj) out to shadowRange metres from
    // the light. Six extra shadow passes per light, so it is off unless a game turns it on.
    bool castsShadows = false;

    float shadowRange = 20.0f;
    // Whether this light draws point shadows.
    bool castsPointShadow() const { return enabled && !isDirectional && castsShadows; }

    // Point lights only: how far the light reaches in metres; 0 = everywhere (no falloff, as before ranges existed), otherwise it fades smoothly to nothing at range (falloffAt).
    float range = 0.0f;

    // The share of this light that reaches at: 1 for a directional light or range <= 0, else (1 - (d / range)^4)^2 clamped at 0, so 1 at the light and 0 at range; the shaders mirror it (lightFalloff in shaders/lighting_ubo.glsl).
    float falloffAt(const glm::vec3& at) const {
        if (isDirectional || range <= 0.0f) return 1.0f;
        const float t = glm::length(at - position) / range;
        const float w = std::clamp(1.0f - t * t * t * t, 0.0f, 1.0f);
        return w * w;
    }
};

// Same reasoning as Camera just above: nearly every module that draws
// real geometry wants to read the current lights, so this lives on
// Application directly rather than behind a getModule<>() lookup.
// kMaxLights is a real, fixed limit (not "as many as you want") because
// the GPU-side UBO this feeds has a fixed-size array — see
// LightingBuffer.h for how that's laid out. 16 since ENG-G07: every lit pixel loops over all of them, skipping lights that are off and point lights out of their range (Light::range), and each costs 48 bytes of the UBO.
// kke::ToneMapper (how scene light becomes displayable colour) is in
// kke/ToneMap.h.

// "agx", "aces", "reinhard" or "none" (any case); nullopt for anything else. Used by
// the KKE_TONEMAP override, which forces one curve over whatever the game
// picked, to compare them.
std::optional<ToneMapper> parseToneMapper(const std::string& name);

struct Lighting {
    static constexpr int kMaxLights = 16;
    std::array<Light, kMaxLights> lights;
    // Many lights (ENG-F08, kke/ClusteredLights.h): point lights past the 16
    // key ones, without shadows, each lit pixel touched only by those near it
    // (clustered on the GPU). Up to kMaxManyLights; a light with range 0 gets
    // kDefaultManyRange. Script lights spill over here when the key slots are full.
    static constexpr size_t kMaxManyLights = 1024;
    static constexpr float kDefaultManyRange = 10.0f;
    std::vector<Light> manyLights;
    // A light by slot: 0..15 the key lights, 16 and up the many lights (ScriptLights hands out both).
    Light& at(int slot) { return slot < kMaxLights ? lights[static_cast<size_t>(slot)] : manyLights[static_cast<size_t>(slot - kMaxLights)]; }
    // Point shadow slots: -1 means no slot, otherwise the index (0..3) of the point-shadow map this light draws into.
    // Slots are assigned in light order to the first ShadowMap::kMaxPointShadows lights that cast shadows and have shadows enabled.
    std::array<int, kMaxLights> pointShadowSlots() const {
        std::array<int, kMaxLights> slots{};
        int next = 0;
        for (int i = 0; i < kMaxLights; ++i) {
            if (shadowsEnabled && lights[i].castsPointShadow() && next < static_cast<int>(ShadowMap::kMaxPointShadows)) {
                slots[i] = next++;
            } else {
                slots[i] = -1;
            }
        }
        return slots;
    }
    glm::vec3 ambientColor{0.15f, 0.15f, 0.15f}; // flat fill light so unlit faces read as dim, not pure black
    // Light 0's shadow map is still rendered either way (so toggling is
    // instant), but lit shaders ignore it when this is false.
    bool shadowsEnabled = true;
    ToneMapper toneMapper = ToneMapper::AgX;
    // A colour LUT applied after the tone map (ENG-F10, kke/ColorLut.h); null = none. A mood sets it.
    std::shared_ptr<const ColorLut> lut;
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
    // Screen-space global illumination (ENG-F01, kke/ScreenGi.h): light
    // bounced off what is on screen and shadow in corners and under things,
    // one frame late. Off by default (a compute pass, and the depth kept);
    // the graphics setting "globalIllumination" or KKE_GI=1 turn it on. With
    // one view only (split screen draws without it).
    void setScreenGi(bool on);
    bool screenGi() const { return m_screenGiOn; }
    ScreenGiSettings& screenGiSettings() { return m_screenGiSettings; }
    // Reflections on smooth surfaces (ENG-F02, kke/Reflections.h).
    // Kke and Classic fall back to Screen without ray query
    // (VulkanDevice::rayQuery). The graphics setting "reflections" or
    // KKE_REFLECTIONS=off|screen|kke|classic choose it. One view only.
    void setReflections(Reflections mode);
    // Meshes drawn this frame that rays should see (ENG-F02): while
    // ray-traced reflections are on, kke::DynamicMeshRenderer::draw records
    // itself here and the frame's ray scene takes it in (classic: its
    // triangles; KKE: a box round them). A renderer going away forgets itself.
    bool recordingRays() const { return m_recordRays; }
    void recordRayDraw(DynamicMeshRenderer* mesh, const glm::mat4& model) { m_rayDraws.push_back({ mesh, model }); }
    void forgetRayDraws(DynamicMeshRenderer* mesh);
    Reflections reflections() const { return m_reflections; }
    // Volumetric fog (ENG-F03, kke/VolumetricFog.h): the height fog (Lighting::fog)
    // lit with the sun's shadow and the point lights, in a grid over the view.
    // Off by default; the graphics setting "volumetricFog" or KKE_VOLUMETRIC_FOG=1
    // turn it on. One view only, and only while the fog is on.
    void setVolumetricFog(bool on) { m_volumetricFogOn = on || m_volumetricFogEnv; }
    // Morphological anti-aliasing (ENG-F05, Renderer::setSmaa): the graphics
    // setting "smaa"; KKE_SMAA=1 or 0 wins over it.
    void setSmaa(bool on);
    // Temporal anti-aliasing (ENG-F05 part 2, kke/Taa.h), an option over the
    // clean image: the graphics setting "taa"; KKE_TAA=1 or 0 wins over it.
    // One view only (split screen draws without it).
    void setTaa(bool on);
    // Bloom (ENG-F10, Renderer::setBloom): the graphics setting "bloom"; KKE_BLOOM=1 or 0 wins over it.
    void setBloom(bool on);
    // Depth of field (ENG-F10 part 3, kke/DepthOfField.h): the graphics setting "depthOfField"; KKE_DOF=1 or 0
    // wins over it. Its focus, aperture and blur: depthOfFieldSettings() (focusDistance 0 = auto focus).
    void setDepthOfField(bool on);
    DofSettings& depthOfFieldSettings();
    // Motion blur from the camera's movement (ENG-F10 part 4, kke/MotionBlur.h): the graphics setting "motionBlur";
    // KKE_MOTION_BLUR=1 or 0 wins over it.
    void setMotionBlur(bool on);
    MotionBlurSettings& motionBlurSettings();
    // Work an engine part (not a module) records each frame before the 3D
    // pass, paused or not (ENG-F11: kke::ParticleEffects steps its GPU
    // particles); `owner` removes it again.
    void addFrameCompute(const void* owner, std::function<void(VkCommandBuffer)> step);
    void removeFrameCompute(const void* owner);
    // Decals (ENG-F12, kke/Decals.h): scorch marks, splats, cracks, holes,
    // puddles and footprints laid onto whatever lit surface is in their box.
    // placeDecal returns 0 when its texture is neither a built-in nor
    // assets/decals/<name>.png. At most DecalSet::kMax; the oldest goes.
    DecalId placeDecal(const Decal& decal);
    bool removeDecal(DecalId id);
    void clearDecals();
    size_t decalCount() const { return m_decals.count(); }
    // Asks for the scene's depth to be kept for the next frame (ENG-F11: GPU
    // particles bounce off it); `owner` gives the wish back with false.
    void wantSceneDepth(const void* owner, bool want);
    // Ray-traced sun shadows (ENG-F07, kke/ShadowRays.h): Kke traces only
    // past the shadow map's reach, Classic every pixel; ray-query devices,
    // one view. The graphics setting "shadowRays" or KKE_SHADOW_RAYS.
    void setShadowRays(ShadowRays mode);
    ShadowRays shadowRays() const { return m_shadowRays; }
    ShadowRaySettings& shadowRaySettings() { return m_shadowRaySettings; }
    TaaSettings& taaSettings() { return m_taaSettings; }
    bool volumetricFog() const { return m_volumetricFogOn; }
    VolumetricFogSettings& volumetricFogSettings() { return m_volumetricFogSettings; }
    // Set 3's layout for see-through surfaces that trace refracted rays
    // (RenderContext::refractionRays); made on first use, ray-query devices only.
    VkDescriptorSetLayout refractionRayLayout();
    ReflectionSettings& reflectionSettings() { return m_reflectionSettings; }
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
    // The same cache's Texture, for modules that build their own
    // descriptor sets (kke::ModelModule's material maps); nullptr when
    // the file can't be loaded. linearData: a data map (normals,
    // roughness), cached apart from the colour copy (Texture::LinearData).
    const Texture* cachedTexture(const std::string& path, bool linearData = false);
    // The 1x1 white texture behind defaultTextureDescriptorSet().
    const Texture& defaultWhiteTexture() const { return *m_defaultWhiteTexture; }

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

    // Events some module must see before every other one: on-screen touch
    // controls take the fingers on them, so a thumb on the stick is not
    // also a click or a camera drag in the game. `claim` returns true for
    // an event it has handled; then no module (and not the developer UI)
    // sees it. Returns an id for removeEventClaim().
    using EventClaim = std::function<bool(const SDL_Event&)>;
    int addEventClaim(EventClaim claim);
    void removeEventClaim(int id);

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
    std::vector<std::pair<int, EventClaim>> m_eventClaims;
    int m_nextEventClaim = 1;
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
    std::unique_ptr<ScreenGiPass> m_screenGi; // made the first time GI is on
    bool m_screenGiOn = false, m_screenGiEnv = false; // KKE_GI=1 keeps it on whatever the settings say
    ScreenGiSettings m_screenGiSettings;
    std::unique_ptr<ReflectionPass> m_reflectionPass; // made the first time reflections are on
    Reflections m_reflections = Reflections::Off;
    std::optional<Reflections> m_reflectionsEnv; // KKE_REFLECTIONS
    // Refraction (ENG-F02 part 4): on with reflections (one view), unless
    // KKE_REFRACTIONS=0. The scene before the see-through surfaces.
    std::unique_ptr<BehindPass> m_behindPass;
    std::unique_ptr<VolumetricFogPass> m_volumetricFog; // made the first time volumetric fog is on
    VolumetricFogSettings m_volumetricFogSettings;
    bool m_volumetricFogOn = false, m_volumetricFogEnv = false; // KKE_VOLUMETRIC_FOG=1 keeps it on
    std::optional<bool> m_smaaEnv;                              // KKE_SMAA
    std::optional<bool> m_taaEnv;                               // KKE_TAA
    std::optional<bool> m_dofEnv;                               // KKE_DOF
    bool m_dofOn = false;
    std::optional<bool> m_motionBlurEnv;                        // KKE_MOTION_BLUR
    bool m_motionBlurOn = false;
    std::vector<std::pair<const void*, std::function<void(VkCommandBuffer)>>> m_frameCompute;
    std::vector<const void*> m_depthWanted;
    std::optional<bool> m_bloomEnv;                             // KKE_BLOOM
    std::unique_ptr<ShadowRayPass> m_shadowRayPass; // made the first time shadow rays are on
    std::unique_ptr<ClusterLightPass> m_clusterLights; // made the first time there are many lights (ENG-F08)
    // Decals (ENG-F12): every one placed, their textures, their per-cell lists and per frame their records.
    DecalSet m_decals;
    std::unique_ptr<DecalTextures> m_decalTextures; // made by the first placeDecal
    std::unique_ptr<ClusterLightPass> m_decalCells;
    std::unique_ptr<Buffer> m_decalBuffers[Renderer::kMaxFramesInFlight];
    std::vector<GpuDecal> m_decalRecords;
    std::vector<glm::vec4> m_decalSpheres;
    float m_now = 0.0f; // seconds since the start, this frame (decals fade by it)
    bool m_clustersOff = false;                         // KKE_CLUSTERS=0
    ShadowRays m_shadowRays = ShadowRays::Off;
    std::optional<ShadowRays> m_shadowRaysEnv; // KKE_SHADOW_RAYS
    ShadowRaySettings m_shadowRaySettings;
    bool m_taaOn = false;
    TaaSettings m_taaSettings;
    uint32_t m_taaFrame = 0; // which sub-pixel offset this frame draws with
    void updateSceneDepthReadable(); // kept when GI, reflections or TAA read it
    std::unique_ptr<RefractionRays> m_refractionRays; // made the first time refraction traces rays
    // This frame's kke::RayScene from what is drawn (KKE and classic ray tracing), into rays; once a frame.
    void buildRayScene(VkCommandBuffer cmd, const glm::mat4& view, const SkyEnvironment* sky, ReflectionRays& rays);
    bool m_refractionsOff = false;
    ReflectionSettings m_reflectionSettings;
    std::unique_ptr<RayScene> m_rayScene;    // KKE ray tracing's stand-ins, made the first time they're traced
    std::vector<RayMaterial> m_rayMaterials; // how each instance looks to a ray, this frame
    bool m_recordRays = false;
    struct RayDraw { DynamicMeshRenderer* mesh; glm::mat4 model; };
    std::vector<RayDraw> m_rayDraws; // this frame's (recordRayDraw)
    // Their geometry: KKE a box per mesh (by its upload version), classic per mesh and frame slot.
    struct MeshRay { uint32_t geometry = 0; uint64_t version = 0; const void* buffer = nullptr; uint32_t vertexCount = 0, indexCount = 0; };
    std::unordered_map<const DynamicMeshRenderer*, MeshRay> m_meshBoxes;
    std::unordered_map<const DynamicMeshRenderer*, MeshRay> m_meshTriangles[2];
    void addMeshesToRayScene(bool classic, VkCommandBuffer cmd, uint32_t frame);
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
    // Point-light shadows: six faces per shadow-casting point light (ShadowMap::pointFaceViewProj).
    std::unique_ptr<ShadowMap> m_pointShadowMap;
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
