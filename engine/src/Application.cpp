#include "kke/Application.h"
#include "kke/SkyRenderer.h"
#include "BehindPass.h"
#include "ClusterLightPass.h"
#include "ShadowRayPass.h"
#include "VolumetricFogPass.h"
#include "RefractionRays.h"
#include "ReflectionPass.h"
#include "ScreenGiPass.h"
#include "kke/Capabilities.h"
#include "kke/RayScene.h"
#include "kke/SphereImpostors.h"
#include "kke/Mood.h"
#include "kke/EngineSettings.h"
#include "kke/LogoIntro.h"
#include "kke/DevTools.h"
#include "kke/BenchRecorder.h"
#include "kke/BenchmarkReport.h"
#include "kke/Picking.h"
#include "kke/Platform.h"

#include <cstdlib>
#include "kke/Log.h"
#include "kke/VulkanCheck.h"

#include <glm/gtc/matrix_transform.hpp>
#include <stb_image_write.h>
#include <algorithm>
#include <cstddef>
#include <atomic>
#include <cstdio>
#include <cctype>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <system_error>
#include <thread>
#include <queue>
#include <stdexcept>
#include <string>

namespace kke {

std::optional<ToneMapper> parseToneMapper(const std::string& name) {
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (lower == "agx") return ToneMapper::AgX;
    if (lower == "aces") return ToneMapper::ACES;
    if (lower == "reinhard") return ToneMapper::Reinhard;
    if (lower == "none") return ToneMapper::None;
    return std::nullopt;
}

namespace {

// Shaders, fonts and game manifests are opened by relative path
// ("shaders/x.spv"), i.e. relative to the working directory. That holds
// when a game is started from its own folder (build/bin, or a double-click
// on Windows), but not from a desktop launcher or `./build/bin/kke_demo`
// in the repo root. When the working directory has no shaders/ folder
// and the executable's own folder does, switch to the executable's folder
// (docs/RELEASES.md). Returns what happened, for the log.
std::string enterRuntimeDirectory() {
    namespace fs = std::filesystem;
    std::error_code ec;
    platform::captureConsoleOutput();
    // Android: the files come out of the APK into private storage first.
    std::string note;
    if (const std::string bundled = platform::bundledFilesDir(&note); !bundled.empty()) {
        fs::current_path(fs::path(bundled), ec);
        if (ec) return note + "; could not switch to " + bundled + ": " + ec.message();
        return note;
    }
    if (fs::is_directory("shaders", ec)) return {};
    const char* base = SDL_GetBasePath();
    if (!base) return std::string("no shaders/ folder here and the executable's folder is unknown: ") + SDL_GetError();
    const fs::path exeDir(reinterpret_cast<const char8_t*>(base));
    if (!fs::is_directory(exeDir / "shaders", ec)) return "no shaders/ folder here or next to the executable (" + std::string(base) + ")";
    fs::current_path(exeDir, ec);
    if (ec) return "could not switch to the executable's folder " + std::string(base) + ": " + ec.message();
    return "working directory set to the executable's folder " + std::string(base);
}

// Filled before the logger exists (see the constructor), logged after.
std::string g_runtimeDirNote;

} // namespace

Application::Application(const std::string& title, uint32_t width, uint32_t height, float fixedUpdateHz)
    // The working directory is fixed before the window (the first member)
    // exists, so every relative path the engine opens afterwards resolves.
    : m_window((g_runtimeDirNote = enterRuntimeDirectory(), title), width, height), m_fixedDt(1.0f / fixedUpdateHz) {
    log::init(title);
    m_title = title;
    if (!g_runtimeDirNote.empty()) log::get("Application")->info("{}", g_runtimeDirNote);
    // A game may ship its own tiers or tune the built-in ones
    // (targets.json / targets.yml next to the executable).
    {
        std::string error;
        if (int n = loadHardwareTargetsFile("targets.json", &error); n > 0)
            log::get("Application")->info("{} hardware target(s) from targets.json", n);
        if (!error.empty()) log::get("Application")->warn("{}", error);
    }
    m_target = detectHardwareTarget();
    log::get("Application")->info("Hardware target: {} ({}); platform backend: {}", m_target.target->name, m_target.reason,
                                  platform::backendName());
    {
        const char* profile = dev::env("KKE_UI_PROFILE");
        m_uiProfile = uiProfileForTarget(m_target.target->name, profile ? profile : "");
        log::get("Application")->info("UI profile: {}", uiProfileName(m_uiProfile));
    }
    {
        EngineSettings defaults = targetDefaultSettings();
        if (const char* all = dev::env("KKE_USE_EVERYTHING"); all && *all == '1') defaults.performance.useEverything = true;
        setResourceBudget(computeBudget(defaults, usableCpuCount()));
    }
    m_renderer = std::make_unique<Renderer>(m_window);
    {
        // A CPU-only Vulkan device draws every pixel and triangle on the CPU:
        // the automatic target moves to software-gpu (kke/HardwareTarget.h),
        // and the budget with it.
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_renderer->device().physicalDevice(), &props);
        const TargetChoice refined = refineForSoftwareGpu(m_target, m_renderer->device().isSoftwareRasterizer(), props.deviceName);
        if (refined.target != m_target.target) {
            m_target = refined;
            log::get("Application")->info("Hardware target: {} ({})", m_target.target->name, m_target.reason);
            EngineSettings defaults = targetDefaultSettings();
            if (const char* all = dev::env("KKE_USE_EVERYTHING"); all && *all == '1') defaults.performance.useEverything = true;
            setResourceBudget(computeBudget(defaults, usableCpuCount()));
        }
    }
    m_renderer->setRenderScale(m_budget.renderScale);
    {
        // MSAA is baked into every scene pipeline, so it's chosen here,
        // before any module creates one: the saved setting (settings.json
        // next to the executable, as SettingsModule writes it), off on a
        // software rasteriser (min-spec), KKE_MSAA=n overrides both.
        EngineSettings saved = loadSettingsFile("settings.json", nullptr, targetDefaultSettings());
        saved.sanitize();
        int msaa = m_renderer->device().isSoftwareRasterizer() ? 1 : saved.graphics.msaa;
        if (const char* env = dev::env("KKE_MSAA"); env && *env) msaa = std::max(1, std::atoi(env));
        m_renderer->setMsaaSamples(static_cast<uint32_t>(msaa));
        log::get("Application")->info("MSAA {}x{}", m_renderer->msaaSamples(),
                                      m_renderer->device().isSoftwareRasterizer() ? " (software rasteriser)" : "");
    }
    if (const char* env = dev::env("KKE_TONEMAP"); env && *env) {
        m_toneMapperOverride = parseToneMapper(env);
        if (!m_toneMapperOverride) log::get("Application")->warn("KKE_TONEMAP={} is not agx, aces, reinhard or none; ignored", env);
    }
    m_lightingBuffer = std::make_unique<LightingBuffer>(m_renderer->device());
    // KKE_REFLECTIONS=off|screen|kke|classic: that, whatever the settings say.
    if (const char* env = dev::env("KKE_REFLECTIONS"); env && *env) {
        m_reflectionsEnv = reflectionsFromName(env, Reflections::Off);
        setReflections(*m_reflectionsEnv);
    }
    // KKE_REFRACTIONS=0: see-through surfaces don't bend what's behind them, whatever the reflections (benchmarks).
    if (const char* env = dev::env("KKE_REFRACTIONS"); env && *env) m_refractionsOff = std::string(env) == "0";
    // KKE_REFLECT_ROUGHNESS=1: every surface up to that roughness reflects (benchmarks: the most work).
    if (const char* env = dev::env("KKE_REFLECT_ROUGHNESS"); env && *env)
        m_reflectionSettings.maxRoughness = std::clamp(static_cast<float>(std::atof(env)), 0.01f, 1.0f);
    // KKE_CLUSTERS=0: every lit pixel walks all the many lights (benchmarks: what clustering saves).
    if (const char* env = dev::env("KKE_CLUSTERS"); env && *env) m_clustersOff = env[0] == '0';
    // KKE_SHADOW_RAYS=off|kke|classic: that, whatever the settings say.
    if (const char* env = dev::env("KKE_SHADOW_RAYS"); env && *env) {
        m_shadowRaysEnv = shadowRaysFromName(env, ShadowRays::Off);
        setShadowRays(*m_shadowRaysEnv);
    }
    if (const char* env = dev::env("KKE_BLOOM"); env && *env) {
        m_bloomEnv = env[0] == '1';
        setBloom(*m_bloomEnv);
    }
    if (const char* env = dev::env("KKE_DOF"); env && *env) {
        m_dofEnv = env[0] == '1';
        setDepthOfField(*m_dofEnv);
    }
    if (const char* env = dev::env("KKE_DOF_FOCUS"); env && *env) depthOfFieldSettings().focusDistance = std::strtof(env, nullptr); // metres
    if (const char* env = dev::env("KKE_TAA"); env && *env) {
        m_taaEnv = env[0] == '1';
        setTaa(*m_taaEnv);
    }
    if (const char* env = dev::env("KKE_SMAA"); env && *env) {
        m_smaaEnv = env[0] == '1';
        setSmaa(*m_smaaEnv);
    }
    if (const char* env = dev::env("KKE_VOLUMETRIC_FOG"); env && env[0] == '1') {
        m_volumetricFogEnv = true;
        setVolumetricFog(true);
    }
    if (const char* env = dev::env("KKE_GI"); env && env[0] == '1') {
        m_screenGiEnv = true;
        setScreenGi(true);
    }
    // KKE_RAY_SELFTEST=1: trace a few rays through a small scene on the GPU and
    // check them (ENG-F02), so a driver's ray tracing is known good or not.
    if (const char* env = dev::env("KKE_RAY_SELFTEST"); env && env[0] == '1') {
        std::string report;
        if (rayQuerySelfTest(m_renderer->device(), &report)) log::get("Application")->info("ray query self-test passed: {}", report);
        else log::get("Application")->error("ray query self-test failed: {}", report);
    }
    m_mood = std::make_unique<Mood>();
    if (const char* env = dev::env("KKE_MOOD"); env && *env) m_moodOverride = env;

    // Real shadow mapping — see kke::ShadowMap's own class comment for
    // the full scope. 2048 is ShadowMap's own default resolution,
    // passed explicitly here anyway so the choice is visible at the
    // call site, not buried in a default argument.
    m_shadowMap = std::make_unique<ShadowMap>(m_renderer->device(), 2048);
    m_pointShadowMap = std::make_unique<ShadowMap>(m_renderer->device(), ShadowMap::kPointTileResolution);

    // The shadow map sampler's own descriptor set layout/pool/set (set
    // 1 in cube.frag) -- separate from ShadowMap's own render
    // pass/framebuffer (see Application.h's own comment on why this
    // lives here). One combined-image-sampler binding, fragment-stage
    // only (the vertex stage only needs the light-space matrix, which
    // already travels through LightingBuffer's own UBO, not this).
    {
        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding = 0;
        bindings[1].binding = 1;
        for (int i = 0; i < 2; ++i) {
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT; // compute: volumetric fog's shafts
        }

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 2;
        layoutInfo.pBindings = bindings;
        VK_CHECK(vkCreateDescriptorSetLayout(m_renderer->device().device(), &layoutInfo, nullptr, &m_shadowMapSetLayout));

        VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        VK_CHECK(vkCreateDescriptorPool(m_renderer->device().device(), &poolInfo, nullptr, &m_shadowMapDescriptorPool));

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_shadowMapDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_shadowMapSetLayout;
        VK_CHECK(vkAllocateDescriptorSets(m_renderer->device().device(), &allocInfo, &m_shadowMapDescriptorSet));

        writeShadowMapDescriptor();
    }

    // The material albedo texture's own descriptor infrastructure (set
    // 2 in cube.frag) -- same pattern as the shadow map's own set just
    // above. A 1x1 opaque white pixel as the default: sampling it and
    // multiplying into a fragment's albedo (see cube.frag) is an exact
    // no-op, so every object drawn through this shader keeps rendering
    // exactly as it did before this feature existed, unless it's given
    // a real texture of its own (see kke::Texture, CubeModule).
    {
        const uint8_t whitePixel[4] = { 255, 255, 255, 255 };
        m_defaultWhiteTexture = std::make_unique<Texture>(m_renderer->device(), whitePixel, 1, 1);

        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        VK_CHECK(vkCreateDescriptorSetLayout(m_renderer->device().device(), &layoutInfo, nullptr, &m_materialTextureSetLayout));

        // maxSets=2, not 1 -- this pool needs to cover both the default
        // texture's own set (allocated below) and CubeModule's real
        // one (allocated separately, using this same layout, when it
        // creates its own texture). A single shared pool for both, not
        // a second pool just for CubeModule, since there's only ever
        // one other real consumer of this exact layout right now.
        VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 2;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        VK_CHECK(vkCreateDescriptorPool(m_renderer->device().device(), &poolInfo, nullptr, &m_materialTextureDescriptorPool));

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_materialTextureDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_materialTextureSetLayout;
        VK_CHECK(vkAllocateDescriptorSets(m_renderer->device().device(), &allocInfo, &m_defaultTextureDescriptorSet));

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = m_defaultWhiteTexture->imageView();
        imageInfo.sampler = m_defaultWhiteTexture->sampler();

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_defaultTextureDescriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(m_renderer->device().device(), 1, &write, 0, nullptr);
    }

    m_debugUi = std::make_unique<DebugUi>(m_window, m_renderer->device(), m_renderer->overlayRenderPass(),
                                           m_renderer->swapChainImageCount(), !m_renderer->outputEncodesSrgb());
    // Shipping builds start (and stay) with the developer panels hidden.
    m_debugUi->setVisible(dev::kEnabled);
}

namespace {

// Quitting must never hang (Kees, 2026-09-28: SUPER+Q left racing stuck
// after "FEMFX scene destroyed" until it was killed). Once the main loop
// has ended, a watchdog gives the teardown kQuitGrace seconds; past that it
// says on stderr which step never finished (the log is closed by then)
// and ends the process. The step names are string literals (a module's
// name() too), so the watchdog can read them while the main thread
// destroys things.
constexpr int kQuitGraceSeconds = 8;
std::atomic<const char*> g_quitStep{ nullptr };
std::atomic<const char*> g_quitModule{ nullptr };

void quitStep(const char* step, const char* module = nullptr) {
    g_quitModule.store(module);
    g_quitStep.store(step);
}

void startQuitWatchdog() {
    quitStep("finishing the last frame");
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::seconds(kQuitGraceSeconds));
        const char* step = g_quitStep.load();
        const char* module = g_quitModule.load();
        std::fprintf(stderr, "[kke][warning]: quitting took longer than %d s, stuck while %s%s%s; ending the process (please report this)\n",
                     kQuitGraceSeconds, step ? step : "?", module ? " " : "", module ? module : "");
        std::fflush(stderr);
        std::_Exit(0);
    }).detach();
}

} // namespace

Application::~Application() {
    if (!g_quitStep.load()) startQuitWatchdog(); // run() never ran, or threw
    quitStep("waiting for the GPU");
    // A real, validation-layer-confirmed bug this fixes: without this
    // wait, a module's shutdown() (called below) could destroy Vulkan
    // resources — buffers, pipelines — while the GPU was still
    // executing the last frame's command buffer that referenced them.
    // Renderer's own destructor already calls vkDeviceWaitIdle(), but
    // that only runs *after* this whole destructor body finishes and
    // Application's members get torn down in reverse declaration
    // order — by then, every module's shutdown() has already run and
    // already destroyed things, too late for that wait to help.
    // Confirmed via real Vulkan validation errors during a live
    // kke_demo shutdown ("vkDestroyBuffer(): can't be called on
    // VkBuffer ... that is currently in use by VkCommandBuffer"),
    // found specifically because this session installed real
    // validation layers for the first time — this exact bug had
    // presumably always been there, just never visible before (it
    // never actually crashed, only mildly corrupted GPU-side state
    // that happened not to matter for a process about to exit anyway).
    if (m_renderer) {
        vkDeviceWaitIdle(m_renderer->device().device());
    }

    // Shut down in reverse dependency order: a module that depends on
    // another should tear itself down first, while what it depends on is
    // still alive to be torn down safely after. Faulted modules are
    // skipped entirely — see safeInvoke()'s comment on why even
    // shutdown() isn't trusted once a module has already thrown.
    for (auto it = m_initOrder.rbegin(); it != m_initOrder.rend(); ++it) {
        if (m_faultedModules.count(*it)) continue;
        quitStep("shutting down", (*it)->name());
        safeInvoke(*it, "shutdown", [&] { (*it)->shutdown(); });
    }
    quitStep("freeing the engine's descriptor pools");

    // The shadow map descriptor infrastructure created in the
    // constructor above -- destroyed here, after every module's own
    // shutdown() (which might reference m_shadowMapDescriptorSet via
    // RenderContext during its own teardown) but safely, since the
    // vkDeviceWaitIdle() at the top of this destructor already
    // guarantees nothing is still using it on the GPU by this point.
    if (m_renderer) {
        VkDevice dev = m_renderer->device().device();
        if (m_shadowMapDescriptorPool) vkDestroyDescriptorPool(dev, m_shadowMapDescriptorPool, nullptr);
        if (m_shadowMapSetLayout) vkDestroyDescriptorSetLayout(dev, m_shadowMapSetLayout, nullptr);
        if (m_materialTextureDescriptorPool) vkDestroyDescriptorPool(dev, m_materialTextureDescriptorPool, nullptr);
        m_textureCache.clear();
        if (m_textureCachePool) vkDestroyDescriptorPool(dev, m_textureCachePool, nullptr);
        if (m_materialTextureSetLayout) vkDestroyDescriptorSetLayout(dev, m_materialTextureSetLayout, nullptr);
    }

    quitStep("flushing the log");
    log::shutdown(); // flush the async queue before the process exits

    // The modules' destructors (worker threads join here: physics, skinning,
    // networking, audio), one at a time so a stuck one is named; then the
    // members below (the renderer, then the window and SDL).
    for (auto& m : m_modules) {
        quitStep("destroying", m->name());
        m.reset();
    }
    m_modules.clear();
    quitStep("closing the renderer, the window and SDL");
}

// The one place a Module's own code can hand control back to the engine
// mid-lifecycle-call — and therefore the one place a bug in a module
// (an uncaught exception, or something that isn't even a C++ exception)
// shouldn't be allowed to take the whole engine down with it. A module
// that throws here is: logged clearly (module name, which lifecycle
// stage, the actual message) via the same spdlog-based logger every
// other module uses; recorded in m_brokenModuleInfos for
// DebugControlModule's "emergency window" to display; and added to
// m_faultedModules, meaning it is NEVER CALLED AGAIN for the rest of
// this session — not update(), not render(), not even shutdown() when
// the Application itself is torn down. That last part is a deliberate
// choice: a module that has already misbehaved once is not a module
// whose cleanup code should be trusted either. Everything ELSE keeps
// running exactly as if this one module didn't exist.
void Application::safeInvoke(Module* m, const char* stage, const std::function<void()>& fn) {
    if (m_faultedModules.count(m)) return;
    const bool timed = m_bench && !m_benchModuleIndex.empty();
    const auto started = timed ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    const size_t brokenBefore = m_brokenModuleInfos.size();

    try {
        fn();
    } catch (const EngineError& e) {
        // The rich path: a module threw something that already carries a
        // plain-language message, source, and (usually) a file/line —
        // see EngineError.h. Nothing to guess here; just record what it
        // told us.
        m_faultedModules.insert(m);
        BrokenModuleInfo info;
        info.moduleName = m->name();
        info.stage = stage;
        info.friendlyMessage = e.friendlyMessage();
        info.technicalMessage = e.what();
        info.source = e.source();
        info.file = e.file();
        info.line = e.line();
        m_brokenModuleInfos.push_back(info);

        auto logger = log::get(m->name());
        if (e.hasLocation()) {
            logger->error("disabled for the rest of this session after throwing during {}() at {}:{} — {}",
                           stage, e.file(), e.line(), e.friendlyMessage());
        } else {
            logger->error("disabled for the rest of this session after throwing during {}(): {}",
                           stage, e.friendlyMessage());
        }
    } catch (const std::exception& e) {
        // The plain path: whatever threw this had no way to tell us
        // anything beyond what(). friendlyMessage and technicalMessage
        // end up identical — there's no richer text to split them with —
        // and source is honestly Unknown rather than guessed at.
        m_faultedModules.insert(m);
        m_brokenModuleInfos.push_back({ m->name(), stage, e.what(), e.what(), ErrorSource::Unknown, "", 0 });
        log::get(m->name())->error("disabled for the rest of this session after throwing during {}(): {}", stage, e.what());
    } catch (...) {
        m_faultedModules.insert(m);
        const char* message = "threw something that isn't a std::exception — no message available";
        m_brokenModuleInfos.push_back({ m->name(), stage, message, message, ErrorSource::Unknown, "", 0 });
        log::get(m->name())->error("disabled for the rest of this session after throwing a non-standard exception during {}()", stage);
    }
    if (timed) {
        if (auto it = m_benchModuleIndex.find(m); it != m_benchModuleIndex.end())
            m_bench->addModule(it->second, stage, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
        if (m_brokenModuleInfos.size() > brokenBefore)
            m_bench->addEvent(std::string("module ") + m->name() + " disabled in " + std::string(stage) + "(): " + m_brokenModuleInfos.back().friendlyMessage);
    }
}

void Application::writeShadowMapDescriptor() {
    VkDescriptorImageInfo shadowMapInfo{};
    shadowMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    shadowMapInfo.imageView = m_shadowMap->imageView();
    shadowMapInfo.sampler = m_shadowMap->sampler();

    VkDescriptorImageInfo pointShadowMapInfo{};
    pointShadowMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    pointShadowMapInfo.imageView = m_pointShadowMap->imageView();
    pointShadowMapInfo.sampler = m_pointShadowMap->sampler();

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = m_shadowMapDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &shadowMapInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = m_shadowMapDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &pointShadowMapInfo;

    vkUpdateDescriptorSets(m_renderer->device().device(), 2, writes, 0, nullptr);
}

void Application::resolveInitOrder() {
    m_moduleByType.clear();
    for (auto& m : m_modules) {
        // typeid(*m) resolves through the vtable to the concrete type, so
        // this map key is the module's real type regardless of how it's
        // stored (unique_ptr<Module>).
        const Module& module = *m; // named first: typeid of an expression with a call is flagged by clang
        m_moduleByType[std::type_index(typeid(module))] = m.get();
    }

    std::unordered_map<Module*, std::vector<Module*>> dependents; // dependency -> [modules that need it]
    std::unordered_map<Module*, int> inDegree;
    for (auto& m : m_modules) inDegree[m.get()] = 0;

    for (auto& m : m_modules) {
        for (const auto& dep : m->dependencies()) {
            auto it = m_moduleByType.find(dep.type);
            if (it == m_moduleByType.end()) {
                if (dep.required) {
                    throw std::runtime_error(
                        std::string("Module '") + m->name() + "' requires a module of type '" +
                        dep.type.name() + "'" +
                        (std::string(dep.reason).empty() ? "" : (std::string(" (") + dep.reason + ")")) +
                        ", but it wasn't added to the Application.");
                }
                continue; // optional and absent — fine, the module must handle getModule<T>()==nullptr itself
            }
            dependents[it->second].push_back(m.get());
            inDegree[m.get()]++;
        }
    }

    // Kahn's algorithm: dependencies before dependents.
    std::queue<Module*> ready;
    for (auto& m : m_modules) {
        if (inDegree[m.get()] == 0) ready.push(m.get());
    }

    m_initOrder.clear();
    while (!ready.empty()) {
        Module* n = ready.front();
        ready.pop();
        m_initOrder.push_back(n);
        for (Module* dependent : dependents[n]) {
            if (--inDegree[dependent] == 0) ready.push(dependent);
        }
    }

    if (m_initOrder.size() != m_modules.size()) {
        throw std::runtime_error("Circular module dependency detected — check dependencies() on your modules.");
    }
}

void Application::playIntro() {
    if (dev::flag("KKE_SKIP_INTRO")) return;
    std::unique_ptr<LogoIntro> intro;
    try {
        intro = std::make_unique<LogoIntro>(*this);
    } catch (const std::exception& e) {
        // A missing shader or a driver quirk must never stop the game itself.
        log::get("Intro")->error("intro skipped: {}", e.what());
        return;
    }
    // KKE_INTRO_AT=<seconds>: freeze the intro at that moment (screenshots).
    float freezeAt = -1.0f;
    if (const char* at = dev::env("KKE_INTRO_AT"); at && *at) freezeAt = static_cast<float>(std::atof(at));

    // The intro's colours are final display colours: shown as they are.
    m_renderer->setPostParams(makePostParams(ToneMapper::None, 1.0f, ColorGrade{}));
    auto last = std::chrono::high_resolution_clock::now();
    bool playing = true, skipped = false;
    while (playing) {
        if (!m_window.pollEvents([&](const SDL_Event& e) {
                if ((e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                    e.type == SDL_EVENT_FINGER_DOWN)
                    skipped = true;
            }))
            break; // closed during the intro: run() sees the same close and ends
        auto now = std::chrono::high_resolution_clock::now();
        const float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        if (skipped) intro->skip();
        if (freezeAt >= 0.0f) intro->setTime(std::min(freezeAt, LogoIntro::kDuration));
        else playing = intro->update(dt);
        // Drawn even on the last pass, so the finished logo stays on
        // screen while the modules below load.
        if (m_renderer->beginFrame()) {
            m_renderer->beginRenderPass();
            intro->render(m_renderer->currentCommandBuffer());
            m_renderer->endFrame();
        }
        if (m_frameRateLimit > 0.0f)
            std::this_thread::sleep_until(now + std::chrono::duration<double>(1.0 / m_frameRateLimit));
    }
    vkDeviceWaitIdle(device().device());
}

bool Application::setMood(const std::string& nameOrPath, std::string* error) {
    if (!m_moodOverride.empty()) return true; // KKE_MOOD wins (applied in run())
    std::string err;
    std::optional<Mood> mood = loadMood(nameOrPath, &err);
    if (!mood) {
        log::get("Application")->warn("{}", err);
        if (error) *error = err;
        return false;
    }
    mood->applyTo(m_lighting);
    *m_mood = std::move(*mood);
    log::get("Application")->info("mood '{}' ({})", m_mood->name, m_mood->file.generic_string());
    return true;
}

struct Application::BenchShots {
    std::vector<BenchRecorder::ShotRequest> requests; // index = capture tag
    // The newest picture of each kind, in the order kinds first appeared.
    std::vector<std::pair<BenchRecorder::ShotRequest, Renderer::FrameCapture>> latest;
    void keep(uint64_t tag, Renderer::FrameCapture&& capture) {
        if (tag >= requests.size()) return;
        const BenchRecorder::ShotRequest& r = requests[tag];
        for (auto& [req, cap] : latest) {
            if (req.kind == r.kind) {
                // Captures arrive in request order, so this one is newer.
                req = r;
                cap = std::move(capture);
                return;
            }
        }
        latest.emplace_back(r, std::move(capture));
    }
};

void Application::startBenchmark(const BenchOptions& options) {
    m_bench = std::make_unique<BenchRecorder>(options);
    if (options.screenshots) {
        m_benchShots = std::make_unique<BenchShots>();
        m_renderer->enableCapture();
    }
    log::get("Benchmark")->info("benchmark: {:.0f} s warm-up, then {:.0f} s measured{}", options.warmup, options.seconds,
                                options.uncapped ? ", uncapped (no vsync, no frame cap)" : "");
}

void Application::writeBenchmarkReport() {
    auto logger = log::get("Benchmark");
    const BenchOptions& o = m_bench->options();
    std::string name = o.name;
    if (name.empty()) {
        for (char c : m_title) name += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '_';
        name += "_" + timestampForFileName();
    }
    auto fromUtf8 = [](const std::string& u) { return std::filesystem::path(std::u8string(u.begin(), u.end())); };
    auto toUtf8 = [](const std::filesystem::path& p) {
        const std::u8string u = p.u8string();
        return std::string(u.begin(), u.end());
    };
    const std::filesystem::path dir = o.dir.empty() ? std::filesystem::path("benchmark") : fromUtf8(o.dir);
    std::vector<std::pair<std::string, std::string>> config;
    config.emplace_back("game", m_title);
    if (const char* item = SDL_getenv("KKE_BENCH_ITEM"); item && *item) config.emplace_back("suite_item", item);
    const VkExtent2D extent = m_renderer->renderExtent();
    config.emplace_back("render_resolution", std::to_string(extent.width) + "x" + std::to_string(extent.height));
    config.emplace_back("render_scale", std::to_string(m_budget.renderScale).substr(0, 4));
    config.emplace_back("msaa", std::to_string(m_renderer->msaaSamples()));
    config.emplace_back("vsync", m_renderer->vsync() ? "on" : "off");
    config.emplace_back("frame_cap", m_frameRateLimit > 0.0f ? std::to_string(static_cast<int>(m_frameRateLimit)) : "none");
    config.emplace_back("worker_threads", std::to_string(m_budget.workerThreads));
    config.emplace_back("fixed_update_hz", std::to_string(static_cast<int>(std::lround(1.0f / m_fixedDt))));
    config.emplace_back("mood", m_mood->name.empty() ? "none" : m_mood->name);
    std::vector<std::string> broken;
    for (const BrokenModuleInfo& b : m_brokenModuleInfos)
        broken.push_back(b.moduleName + " " + b.stage + "(): " + b.friendlyMessage +
                         (b.technicalMessage != b.friendlyMessage ? " [" + b.technicalMessage + "]" : ""));
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (m_benchShots) writeBenchmarkShots(dir, name);
    const nlohmann::json report = m_bench->toJson(collectSystemInfo(device().physicalDevice()), config, broken);

    const std::filesystem::path file = dir / (name + ".json");
    std::ofstream out(file, std::ios::binary);
    out << report.dump(1) << "\n";
    if (!out) {
        logger->error("benchmark: could not write {}", toUtf8(file));
        return;
    }
    const auto& sum = report["summary"];
    logger->info("benchmark: {} frames, {:.1f} fps avg, {:.1f} fps 1% low, {} hitch(es), verdict {}; report {}", sum["frames"].get<int>(),
                 sum["fps_avg"].get<double>(), sum["fps_1pct_low"].get<double>(), sum["hitches"]["count"].get<int>(),
                 sum["verdict"].get<std::string>(), toUtf8(std::filesystem::absolute(file, ec)));
}

// JPEG, quality 90: a phone's 3200x1440 PNG is ~5 MB, this ~1 MB.
void Application::writeBenchmarkShots(const std::filesystem::path& dir, const std::string& name) {
    auto logger = log::get("Benchmark");
    const std::filesystem::path shots = dir / "shots";
    std::error_code ec;
    std::filesystem::create_directories(shots, ec);
    // Pictures an earlier run under this name left behind.
    const std::string prefix = name + "_";
    for (const auto& e : std::filesystem::directory_iterator(shots, ec)) {
        const std::u8string f = e.path().filename().u8string();
        const std::string file(f.begin(), f.end());
        if (file.rfind(prefix, 0) == 0 && e.path().extension() == ".jpg") std::filesystem::remove(e.path(), ec);
    }
    if (!m_renderer->canCapture()) {
        logger->info("benchmark: no screenshots, this display's images can't be read back");
        return;
    }
    for (const auto& [req, cap] : m_benchShots->latest) {
        const std::string fileName = name + "_" + req.label + ".jpg";
        const std::filesystem::path file = shots / std::filesystem::path(std::u8string(fileName.begin(), fileName.end()));
        std::ofstream out(file, std::ios::binary);
        auto write = [](void* ctx, void* data, int size) { static_cast<std::ofstream*>(ctx)->write(static_cast<const char*>(data), size); };
        const int okEncode = stbi_write_jpg_to_func(write, &out, static_cast<int>(cap.width), static_cast<int>(cap.height), 4, cap.rgba.data(), 90);
        if (!okEncode || !out) {
            logger->error("benchmark: could not write screenshot {}", fileName);
            continue;
        }
        m_bench->shotSaved(req, "shots/" + fileName);
    }
    logger->info("benchmark: {} screenshot(s) in {}", m_benchShots->latest.size(), shots.generic_string());
}

void Application::run() {
    resolveInitOrder();
    // A benchmark measures the game, not the logo.
    if (!m_bench)
        if (auto options = BenchRecorder::fromEnvironment()) startBenchmark(*options);
    if (m_introEnabled && !m_bench) playIntro();
    using BenchClock = std::chrono::steady_clock;
    auto msSince = [](BenchClock::time_point a, BenchClock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    std::vector<double> initMs;
    for (Module* m : m_initOrder) {
        const auto initStart = BenchClock::now();
        // A module that throws during init() is disabled the same as any
        // other stage — see safeInvoke(). One real caveat worth stating:
        // if init() throws partway through creating GPU resources, this
        // module's own destructor (still called normally later, via
        // unique_ptr) inherits whatever half-built state it left behind.
        // Every module in this engine builds its GPU resources through
        // RAII wrappers (Buffer, Pipeline, etc.) specifically so a
        // partial init() still leaves safely-destructible state — but a
        // module that doesn't follow that pattern could still misbehave
        // on destruction. Fault isolation reduces this risk; it can't
        // eliminate it for code this engine doesn't control.
        safeInvoke(m, "init", [&] { m->init(*this); });
        initMs.push_back(msSince(initStart, BenchClock::now()));
    }
    if (m_bench) {
        std::vector<std::string> names;
        for (Module* m : m_initOrder) {
            m_benchModuleIndex[m] = static_cast<int>(names.size());
            names.push_back(m->name());
        }
        m_bench->setModules(std::move(names), std::move(initMs));
        for (const BrokenModuleInfo& b : m_brokenModuleInfos) m_bench->addEvent("module " + b.moduleName + " disabled in " + b.stage + "()");
        if (m_bench->options().uncapped) {
            ResourceBudget b = m_budget;
            b.frameRateLimit = 0.0f;
            b.backgroundFrameRate = 0.0f;
            setResourceBudget(b);
            m_renderer->setVSync(false);
        }
    }
    // After init(): a game that picks its mood while starting up is overridden.
    if (!m_moodOverride.empty()) {
        std::string name = m_moodOverride;
        m_moodOverride.clear();
        setMood(name);
        m_moodOverride = name;
    }
    // KKE_HIDE_UI=1: start with every module's panels hidden (clean
    // screenshots and recordings; modules that toggle panels, e.g. F1 in
    // the sandbox, can still show them).
    if (dev::flag("KKE_HIDE_UI"))
        for (Module* m : m_initOrder) m->setUiVisible(false);

    auto startTime = std::chrono::high_resolution_clock::now();
    auto lastFrameTime = startTime;
    float accumulator = 0.0f;
    uint64_t tickIndex = 0;

    const BenchClock::time_point benchLoopStart = BenchClock::now();
    BenchClock::time_point benchFrameStart{}, benchIdleEnd{}, benchLastRss{};
    bool benchCopyThisFrame = false;
    bool benchFrameOpen = false;
    while (m_window.pollEvents([this](const SDL_Event& e) {
        if (m_bench) {
            if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) m_bench->addEvent("window lost focus");
            else if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) m_bench->addEvent("window got focus back");
            else if (e.type == SDL_EVENT_WINDOW_MINIMIZED) m_bench->addEvent("window minimized");
            else if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
                m_bench->addEvent("window resized to " + std::to_string(e.window.data1) + "x" + std::to_string(e.window.data2));
        }
        for (const auto& [id, claim] : m_eventClaims)
            if (claim(e)) return;
        m_debugUi->processEvent(e);
        for (Module* m : m_initOrder) {
            safeInvoke(m, "onEvent", [&] { m->onEvent(e); });
        }
    })) {
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - lastFrameTime).count();
        float totalTime = std::chrono::duration<float>(now - startTime).count();
        lastFrameTime = now;

        // Benchmark: close the previous frame (its events were just
        // polled), open this one.
        const auto benchNow = BenchClock::now();
        if (m_bench) {
            if (benchFrameOpen) {
                m_bench->addStage(BenchRecorder::Events, msSince(benchIdleEnd, benchNow));
                double rss = 0.0;
                if (msSince(benchLastRss, benchNow) >= 250.0) {
                    rss = platform::residentMemoryMb();
                    benchLastRss = benchNow;
                }
                m_bench->endFrame(msSince(benchFrameStart, benchNow), rss);
                if (m_benchShots) {
                    for (BenchRecorder::ShotRequest& r : m_bench->takeShotRequests()) {
                        m_renderer->requestCapture(m_benchShots->requests.size());
                        m_benchShots->requests.push_back(std::move(r));
                        benchCopyThisFrame = true;
                    }
                }
                if (m_bench->done()) {
                    SDL_Event quit{};
                    quit.type = SDL_EVENT_QUIT;
                    SDL_PushEvent(&quit);
                }
            } else {
                m_bench->setLoadSeconds(std::chrono::duration<double>(benchNow - m_createdAt).count());
            }
            m_bench->beginFrame(std::chrono::duration<double>(benchNow - benchLoopStart).count(),
                                std::chrono::duration<double, std::milli>(std::chrono::system_clock::now().time_since_epoch()).count());
            benchFrameOpen = true;
            benchFrameStart = benchNow;
            // This frame copies a screenshot: not a measure of the game.
            if (benchCopyThisFrame) m_bench->excludeFrame();
            benchCopyThisFrame = false;
        }

        // --- Pause/step: see Application.h for the full reasoning.
        // Rendering below always runs regardless of m_paused — only
        // simulation (fixedUpdate/update/compute) is gated here.
        bool advancingThisFrame = !m_paused || m_stepRequested;
        {
            UpdateContext frameCtx{ dt, totalTime };
            for (Module* m : m_initOrder) safeInvoke(m, "frameStart", [&] { m->frameStart(frameCtx); });
        }

        uint32_t stepCap = m_maxFixedStepsPerFrame;
        if (!m_paused) {
            // Cap how much simulation time a single slow frame can inject
            // — otherwise a long stall (asset load, breakpoint) causes a
            // burst of catch-up fixed ticks that then takes even longer,
            // stalling further: the classic "spiral of death."
            // See setMaxFixedStepsPerFrame() for why this cap is small.
            accumulator = std::min(accumulator + dt, m_fixedDt * static_cast<float>(m_maxFixedStepsPerFrame));

            m_fixedStepsLastFrame = 0;
            while (accumulator >= m_fixedDt) {
                m_fixedStepsLastFrame++;
                tickIndex++;
                const auto tickStart = BenchClock::now();
                // setTimeScale(): as many ticks, each a shorter (or longer) step.
                FixedUpdateContext fixedCtx{ m_fixedDt * m_timeScale, tickIndex };
                for (Module* m : m_initOrder) {
                    safeInvoke(m, "fixedUpdate", [&] { m->fixedUpdate(fixedCtx); });
                }
                accumulator -= m_fixedDt;
                // A tick that took longer than the time it simulates can
                // never be caught up: a second one would only double this
                // frame's stall (cloth landing on a phone: two 90 ms ticks
                // in one frame). Run slow motion for this frame instead.
                const float tickCost = std::chrono::duration<float>(BenchClock::now() - tickStart).count();
                if (tickCost > m_fixedDt && accumulator >= m_fixedDt) {
                    accumulator = std::fmod(accumulator, m_fixedDt);
                    stepCap = m_fixedStepsLastFrame; // counts as falling behind
                    break;
                }
            }
        } else if (m_stepRequested) {
            // Single-step: exactly one fixed tick, ignoring whatever the
            // accumulator happens to hold — stepping should feel like
            // "advance by one deterministic tick," not "however much
            // wall-clock time happened to pass while paused."
            tickIndex++;
            FixedUpdateContext fixedCtx{ m_fixedDt, tickIndex };
            for (Module* m : m_initOrder) {
                safeInvoke(m, "fixedUpdate", [&] { m->fixedUpdate(fixedCtx); });
            }
        }

        const auto benchAfterSimulate = BenchClock::now();
        if (advancingThisFrame) {
            // While stepping, dt itself is frozen (time isn't really
            // passing), so hand modules the fixed tick length instead of
            // a real wall-clock delta — a well-defined, reproducible
            // "one step" rather than an arbitrary tiny number.
            float effectiveDt = m_paused ? m_fixedDt : dt * m_timeScale;
            m_fixedAlpha = m_paused ? 1.0f : std::clamp(accumulator / m_fixedDt, 0.0f, 1.0f);
            UpdateContext updateCtx{ effectiveDt, totalTime, m_fixedAlpha };
            for (Module* m : m_initOrder) {
                safeInvoke(m, "update", [&] { m->update(updateCtx); });
            }
        }
        m_stepRequested = false; // consumed whether or not we were actually paused
        const auto benchAfterUpdate = BenchClock::now();
        if (m_bench) {
            m_bench->addStage(BenchRecorder::Simulate, msSince(benchNow, benchAfterSimulate));
            m_bench->addStage(BenchRecorder::Update, msSince(benchAfterSimulate, benchAfterUpdate));
        }

        // The views drawn this frame: split screen / picture-in-picture
        // (views()), or camera() over the whole window.
        struct DrawView {
            Camera camera;
            ViewRect rect;
            glm::mat4 view, proj;
            float aspect;
            LightingBuffer* lighting;
        };
        std::array<DrawView, kMaxViews> drawViews;
        uint32_t viewCount = 0;
        const VkExtent2D sceneExtent = m_renderer->renderExtent();
        auto addView = [&](const Camera& c, const ViewRect& r) {
            DrawView& d = drawViews[viewCount];
            d.camera = c;
            d.rect = r;
            d.aspect = viewAspect(r, sceneExtent);
            d.view = glm::lookAt(c.position, c.target, c.up);
            d.proj = engineProjection(c.fovDegrees, d.aspect, c.nearPlane, c.farPlane); // Vulkan's flipped Y, tall screens
            if (viewCount == 0) {
                d.lighting = m_lightingBuffer.get();
            } else {
                while (m_viewLighting.size() < viewCount) m_viewLighting.push_back(std::make_unique<LightingBuffer>(m_renderer->device()));
                d.lighting = m_viewLighting[viewCount - 1].get();
            }
            ++viewCount;
        };
        if (m_views.empty()) addView(m_camera, ViewRect{});
        for (const View& v : m_views)
            if (viewCount < kMaxViews) addView(v.camera, v.rect);
        // TAA (ENG-F05 part 2): one view drawn with a sub-pixel offset that changes every frame.
        const bool taaOn = m_taaOn && viewCount == 1;
        if (taaOn)
            drawViews[0].proj = taaJitteredProjection(drawViews[0].proj, taaJitter(m_taaFrame++, m_taaSettings),
                                                      glm::vec2(static_cast<float>(sceneExtent.width), static_cast<float>(sceneExtent.height)));
        m_renderer->setTaa(taaOn);
        const glm::mat4& view = drawViews[0].view;
        const glm::mat4& proj = drawViews[0].proj;
        m_renderer->setTaaCamera(proj * view);
        m_renderer->setDofCamera(proj, dt);

        // Cascaded shadow maps (kke::ShadowMap) from the key light (lights[0])
        // only. Each view's frustum, out to kShadowDistance, is cut into
        // ShadowMap::kCascades slices, nearest first, each drawn into its own
        // tile of the map, so near shadows are sharp and far ones still exist.
        // lightViewProjs[i] is view i's far cascade; the nearer ones are it
        // scaled and shifted (ShadowMap::cascadeScaleOffset), which is how the
        // shaders pick one. Every cascade is snapped to its own texels so
        // edges don't shimmer as the camera moves. Split screen: each view has
        // its own cascades, framed on its own camera.
        constexpr float kShadowDistance = 80.0f;
        if (m_shadowMap->setTiles(viewCount * ShadowMap::kCascades)) writeShadowMapDescriptor();
        std::array<glm::mat4, kMaxViews> lightViewProjs;
        std::array<std::array<glm::mat4, ShadowMap::kCascades>, kMaxViews> cascadeViewProjs;
        std::array<std::array<glm::vec4, ShadowMap::kCascades>, kMaxViews> cascadeScaleOffsets, cascadeTiles;
        const glm::vec3 lightDir = m_lighting.lights[0].direction;
        const uint32_t tileRes = m_shadowMap->tileResolution();
        for (uint32_t i = 0; i < viewCount; ++i) {
            const DrawView& d = drawViews[i];
            glm::vec3 forward = d.camera.target - d.camera.position;
            if (glm::dot(forward, forward) < 1e-12f) forward = glm::vec3(0.0f, 0.0f, -1.0f);
            const float tanHalfFovY = 1.0f / std::abs(d.proj[1][1]);
            const float aspect = std::abs(d.proj[1][1] / d.proj[0][0]);
            // The lens from the projection (it already handles tall screens).
            // A near plane at 0 would put every logarithmic split at 0.
            const float nearPlane = std::max(0.01f, d.camera.nearPlane);
            const auto spheres = ShadowMap::cascadeSpheres(d.camera.position, forward, tanHalfFovY, aspect, nearPlane,
                                                           std::min(d.camera.farPlane, kShadowDistance));
            const glm::vec4& far = spheres[ShadowMap::kCascades - 1];
            lightViewProjs[i] = ShadowMap::computeLightViewProj(lightDir, glm::vec3(far), far.w, tileRes);
            for (uint32_t c = 0; c < ShadowMap::kCascades; ++c) {
                cascadeScaleOffsets[i][c] = c + 1 == ShadowMap::kCascades
                    ? glm::vec4(1.0f, 1.0f, 0.0f, 0.0f)
                    : ShadowMap::cascadeScaleOffset(lightViewProjs[i], lightDir, far.w, glm::vec3(spheres[c]), spheres[c].w, tileRes);
                cascadeViewProjs[i][c] = ShadowMap::cascadeViewProj(lightViewProjs[i], cascadeScaleOffsets[i][c]);
                cascadeTiles[i][c] = m_shadowMap->tileRect(i * ShadowMap::kCascades + c);
            }
        }
        const glm::mat4& lightViewProj = lightViewProjs[0];

        // Point-light shadows: the first ShadowMap::kMaxPointShadows point lights that cast shadows
        // (Lighting::pointShadowSlots) get six faces each, in their slot's tiles of the point map:
        // tile = slot * ShadowMap::kPointFaces + face. The map shrinks to one tile when none casts.
        const std::array<int, Lighting::kMaxLights> pointSlots = m_lighting.pointShadowSlots();
        bool anyPointShadow = false;
        for (int s : pointSlots) anyPointShadow = anyPointShadow || s >= 0;
        if (m_pointShadowMap->setTiles(anyPointShadow ? ShadowMap::kMaxPointShadows * ShadowMap::kPointFaces : 1)) writeShadowMapDescriptor();
        std::array<glm::mat4, ShadowMap::kMaxPointShadows * ShadowMap::kPointFaces> pointViewProjs{};
        std::array<glm::vec4, ShadowMap::kMaxPointShadows * ShadowMap::kPointFaces> pointTiles{};
        for (uint32_t t = 0; t < ShadowMap::kMaxPointShadows * ShadowMap::kPointFaces; ++t) pointTiles[t] = m_pointShadowMap->tileRect(t);
        for (int l = 0; l < Lighting::kMaxLights; ++l) {
            if (pointSlots[l] < 0) continue;
            const Light& light = m_lighting.lights[l];
            for (uint32_t f = 0; f < ShadowMap::kPointFaces; ++f)
                pointViewProjs[static_cast<uint32_t>(pointSlots[l]) * ShadowMap::kPointFaces + f] =
                    ShadowMap::pointFaceViewProj(light.position, f, light.shadowRange, m_pointShadowMap->tileResolution());
        }

        RenderContext renderCtx{};
        renderCtx.renderPass = m_renderer->renderPass();
        renderCtx.shadowMapDescriptorSet = m_shadowMapDescriptorSet;
        renderCtx.defaultMaterialTextureDescriptorSet = m_defaultTextureDescriptorSet;
        renderCtx.viewCount = viewCount;

        // The lighting buffers are written once per frame, after
        // beginFrame() below and before any module's render() binds them:
        // every lit module shares one buffer and descriptor set per view
        // and frame in flight (see LightingBuffer.h).
        if (m_toneMapperOverride) m_lighting.toneMapper = *m_toneMapperOverride;
        m_renderer->setPostParams(makePostParams(m_lighting.toneMapper, m_lighting.exposure, m_lighting.grade));
        m_renderer->setColorLut(m_lighting.lut);
        const SkyEnvironment* skyEnv = nullptr;
        if (m_lighting.sky.kind != Sky::Kind::None && !m_skyRenderer) m_skyRenderer = std::make_unique<SkyRenderer>(*this);
        if (m_skyRenderer) skyEnv = &m_skyRenderer->prepare(m_lighting.sky, m_lighting.fog, m_lighting.ambientColor);

        // ImGui's NewFrame() (inside beginFrame()) must only be called
        // for a frame that will also reach Render() — calling it here,
        // unconditionally, before knowing whether m_renderer->beginFrame()
        // will even succeed, was a real bug: when the swapchain is out
        // of date (e.g. mid-resize) and beginFrame() returns false, this
        // whole block gets skipped, so Render() never runs for that
        // frame — and the *next* frame's NewFrame() call then fires
        // without a matching Render() in between, which is exactly
        // ImGui's own "Forgot to call Render() or EndFrame()" assertion.
        // Found by actually reproducing it (resizing the window under
        // Xvfb crashed a real running session), not by inspection alone.
        // Fixed by moving both calls inside the success branch below,
        // so they only ever run for a frame guaranteed to complete.
        const auto benchBeforeBegin = BenchClock::now();
        const bool frameBegun = m_renderer->beginFrame();
        if (m_benchShots) {
            // beginFrame() read back an earlier frame's screenshot: this
            // frame paid for the copy, so it isn't measured either.
            std::vector<Renderer::FrameCapture> captures = m_renderer->takeCaptures();
            if (!captures.empty()) m_bench->excludeFrame();
            for (Renderer::FrameCapture& c : captures) m_benchShots->keep(c.tag, std::move(c));
        }
        const auto benchAfterBegin = BenchClock::now();
        auto benchBeforeEnd = benchAfterBegin, benchAfterEnd = benchAfterBegin;
        if (frameBegun) {
            // After beginFrame's fence wait: this frame slot's buffers are
            // free, while the other slot may still be drawing on the GPU.
            // Screen-space GI: last frame's result for the first view's shading
            // (one view only), its images sized for this frame first.
            const bool giOn = m_screenGiOn && viewCount == 1;
            if (giOn) {
                if (!m_screenGi) m_screenGi = std::make_unique<ScreenGiPass>(m_renderer->device());
                m_screenGi->prepare(sceneExtent);
            }
            const bool giReady = giOn && m_screenGi->ready();
            // Reflections the same way (ENG-F02).
            const bool reflOn = m_reflections != Reflections::Off && viewCount == 1;
            if (reflOn) {
                if (!m_reflectionPass) m_reflectionPass = std::make_unique<ReflectionPass>(m_renderer->device());
                m_reflectionPass->prepare(sceneExtent);
            }
            const bool reflReady = reflOn && m_reflectionPass->ready();
            // Refraction: this frame's scene before the see-through surfaces, copied mid-pass (ENG-F02 part 4).
            const bool refractOn = reflOn && !m_refractionsOff && !m_sceneCovered;
            // Volumetric fog (ENG-F03): this frame's grid, lit after the shadow pass, read by this frame's shading.
            const bool fogVolumeOn = m_volumetricFogOn && viewCount == 1 && m_lighting.fog.enabled && !m_sceneCovered;
            if (fogVolumeOn && !m_volumetricFog)
                m_volumetricFog = std::make_unique<VolumetricFogPass>(m_renderer->device(), m_lightingBuffer->descriptorSetLayout(),
                                                                      m_shadowMapSetLayout);
            if (fogVolumeOn) m_volumetricFog->prepare(m_volumetricFogSettings);
            if (refractOn) {
                if (!m_behindPass) m_behindPass = std::make_unique<BehindPass>(m_renderer->device());
                m_behindPass->prepare(sceneExtent);
            }
            // Meshes record themselves for rays while they draw (recordRayDraw).
            m_rayDraws.clear();
            // Ray-traced sun shadows (ENG-F07): last frame's rays for this frame's shading, cast again after it.
            const bool shadowRaysOn = m_shadowRays != ShadowRays::Off && viewCount == 1 && m_renderer->device().rayQuery() && !m_sceneCovered;
            if (shadowRaysOn) {
                if (!m_shadowRayPass) m_shadowRayPass = std::make_unique<ShadowRayPass>(m_renderer->device());
                m_shadowRayPass->prepare(sceneExtent);
            }
            const bool shadowRaysReady = shadowRaysOn && m_shadowRayPass->ready();
            // Many lights (ENG-F08): this frame's, listed per cell after the shadow pass (one view; split screen walks them all).
            std::vector<ClusterLightPass::GpuLight> manyLights;
            for (const Light& l : m_lighting.manyLights)
                if (l.enabled && !l.isDirectional && l.intensity > 0.0f)
                    manyLights.push_back({ glm::vec4(l.position, l.range > 0.0f ? l.range : Lighting::kDefaultManyRange), glm::vec4(l.color, l.intensity) });
            if (!manyLights.empty()) {
                if (!m_clusterLights) m_clusterLights = std::make_unique<ClusterLightPass>(m_renderer->device());
                m_clusterLights->upload(m_renderer->currentFrameIndex(), manyLights);
            }
            const bool manyOn = !manyLights.empty();
            m_recordRays = shadowRaysOn || (reflOn && (m_reflections == Reflections::Kke || m_reflections == Reflections::Classic) &&
                                            m_renderer->device().rayQuery());
            for (uint32_t i = 0; i < viewCount; ++i) {
                if (i == 0 && giReady)
                    drawViews[i].lighting->setScreenGi(m_screenGi->light(), m_screenGi->depth(), m_screenGi->sampler(), m_screenGi->viewProj(),
                                                       m_screenGi->size());
                else
                    drawViews[i].lighting->setScreenGi(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, glm::mat4(1.0f), glm::vec2(0.0f));
                if (i == 0 && fogVolumeOn) drawViews[i].lighting->setFogVolume(m_volumetricFog->volume(), m_volumetricFog->sampler(), m_volumetricFog->settings());
                else drawViews[i].lighting->setFogVolume(VK_NULL_HANDLE, VK_NULL_HANDLE, m_volumetricFogSettings);
                if (manyOn) {
                    const uint32_t f = m_renderer->currentFrameIndex();
                    drawViews[i].lighting->setManyLights(m_clusterLights->lights(f), m_clusterLights->counts(f), m_clusterLights->indices(f),
                                                         m_clusterLights->count(f), m_clusterLights->grid(),
                                                         glm::vec2(static_cast<float>(sceneExtent.width), static_cast<float>(sceneExtent.height)),
                                                         viewCount > 1 || m_clustersOff);
                } else {
                    drawViews[i].lighting->setManyLights(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, 0, ClusterGrid{}, glm::vec2(1.0f), false);
                }
                if (i == 0 && shadowRaysReady)
                    drawViews[i].lighting->setShadowRays(m_shadowRayPass->image(), m_shadowRayPass->sampler(), m_shadowRayPass->viewProj(),
                                                         m_shadowRayPass->size(), m_shadowRaySettings.reprojectSlack);
                else
                    drawViews[i].lighting->setShadowRays(VK_NULL_HANDLE, VK_NULL_HANDLE, glm::mat4(1.0f), glm::vec2(0.0f), m_shadowRaySettings.reprojectSlack);
                if (i == 0 && refractOn) drawViews[i].lighting->setBehind(m_behindPass->image(), m_behindPass->sampler(), m_behindPass->size());
                else drawViews[i].lighting->setBehind(VK_NULL_HANDLE, VK_NULL_HANDLE, glm::vec2(0.0f));
                if (i == 0 && reflReady)
                    drawViews[i].lighting->setReflections(m_reflectionPass->light(), m_reflectionPass->depth(), m_reflectionPass->sampler(),
                                                          m_reflectionPass->viewProj(), m_reflectionPass->size(), m_reflectionSettings.maxRoughness);
                else
                    drawViews[i].lighting->setReflections(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, glm::mat4(1.0f), glm::vec2(0.0f),
                                                          m_reflectionSettings.maxRoughness);
                drawViews[i].lighting->setFrame(m_renderer->currentFrameIndex());
                drawViews[i].lighting->setEnvironmentMap(m_skyRenderer ? m_skyRenderer->reflections() : nullptr,
                                                         m_skyRenderer ? m_skyRenderer->reflectionLevels() : 0);
                drawViews[i].lighting->update(m_lighting, drawViews[i].camera.position, lightViewProjs[i],
                                              drawViews[i].proj * drawViews[i].view, skyEnv,
                                              cascadeTiles[i].data(), cascadeScaleOffsets[i].data(),
                                              anyPointShadow ? pointViewProjs.data() : nullptr, pointTiles.data());
            }
            {
                const VkExtent2D extent = m_renderer->extent();
                m_debugUi->beginFrame(uiSafeRect(), static_cast<float>(extent.width), static_cast<float>(extent.height));
            }
            // Developer panels: compiled out of shipping builds (kke/DevTools.h).
            if constexpr (dev::kEnabled) {
                for (Module* m : m_initOrder) {
                    if (m->uiVisible()) safeInvoke(m, "renderUi", [&] { m->renderUi(); });
                }
            }

            VkCommandBuffer cmd = m_renderer->currentCommandBuffer();

            // compute() is gated by the same advancingThisFrame flag as
            // fixedUpdate/update: a compute-driven simulation (e.g.
            // ParticleModule) that ran unconditionally here would keep
            // visibly moving every frame even while "paused," defeating
            // the entire point of holding a frame steady for inspection.
            if (advancingThisFrame) {
                for (Module* m : m_initOrder) {
                    safeInvoke(m, "compute", [&] { m->compute(cmd); });
                }
            }
            // computeEveryFrame() runs paused or not, unless a menu covers the
            // scene (nothing is drawn then): see Module.h.
            if (!m_sceneCovered) {
                for (Module* m : m_initOrder) {
                    safeInvoke(m, "computeEveryFrame", [&] { m->computeEveryFrame(cmd); });
                }
            }

            // The shadow pass — a real, separate render pass, entirely
            // before the main color pass begins (see ShadowMap.h's own
            // comment on why: its render pass ends with the depth
            // image transitioned to SHADER_READ_ONLY, which the main
            // pass's fragment shader then samples directly). Most
            // modules' renderShadow() is the Module base class's own
            // no-op default; only real shadow casters (see CubeModule)
            // do anything here.
            ShadowRenderContext shadowCtx{};
            shadowCtx.cmd = cmd;
            shadowCtx.renderPass = m_shadowMap->renderPass();
            shadowCtx.lightViewProj = lightViewProj;
            shadowCtx.frameIndex = m_renderer->currentFrameIndex();
            // A full-screen opaque menu hides the 3D view (setSceneCovered):
            // the shadow pass and render() are skipped. The scene pass
            // still begins, so its clear and the overlay are unchanged;
            // prepass() still runs (thumbnails for that very menu are made
            // there) and scene-only prepass work checks sceneCovered.
            const bool drawScene = !m_sceneCovered;
            if (drawScene) {
                m_shadowMap->beginRenderPass(cmd);
                for (uint32_t i = 0; i < viewCount; ++i) {
                    for (uint32_t c = 0; c < ShadowMap::kCascades; ++c) {
                        const uint32_t tile = i * ShadowMap::kCascades + c;
                        m_shadowMap->beginTile(cmd, tile);
                        shadowCtx.lightViewProj = cascadeViewProjs[i][c];
                        shadowCtx.viewIndex = i;
                        shadowCtx.cascade = c;
                        shadowCtx.tile = tile;
                        for (Module* m : m_initOrder) {
                            safeInvoke(m, "renderShadow", [&] { m->renderShadow(shadowCtx); });
                        }
                    }
                }
                m_shadowMap->endRenderPass(cmd);
                // The point map's pass runs every frame, even when no point light casts:
                // it clears the map and leaves it readable for the shaders.
                m_pointShadowMap->beginRenderPass(cmd);
                if (anyPointShadow) {
                    shadowCtx.renderPass = m_pointShadowMap->renderPass();
                    for (int l = 0; l < Lighting::kMaxLights; ++l) {
                        if (pointSlots[l] < 0) continue;
                        for (uint32_t f = 0; f < ShadowMap::kPointFaces; ++f) {
                            const uint32_t t = static_cast<uint32_t>(pointSlots[l]) * ShadowMap::kPointFaces + f;
                            m_pointShadowMap->beginTile(cmd, t);
                            shadowCtx.lightViewProj = pointViewProjs[t];
                            shadowCtx.viewIndex = 0;
                            shadowCtx.cascade = 0;
                            shadowCtx.pointLight = static_cast<int>(l);
                            shadowCtx.tile = kMaxViews * ShadowMap::kCascades + t;
                            for (Module* m : m_initOrder) {
                                safeInvoke(m, "renderShadow", [&] { m->renderShadow(shadowCtx); });
                            }
                        }
                    }
                }
                m_pointShadowMap->endRenderPass(cmd);
            }

            if (manyOn && viewCount == 1 && !m_clustersOff) m_clusterLights->record(cmd, m_renderer->currentFrameIndex(), view, proj);
            if (fogVolumeOn) m_volumetricFog->record(cmd, drawViews[0].lighting->descriptorSet(), m_shadowMapDescriptorSet, glm::inverse(proj * view));
            PrepassContext prepassCtx{ cmd, view, proj, drawViews[0].camera.position, sceneExtent, m_lightingBuffer->descriptorSet(),
                                       m_renderer->currentFrameIndex(), m_sceneCovered };
            for (Module* m : m_initOrder) {
                safeInvoke(m, "prepass", [&] { m->prepass(prepassCtx); });
            }

            ReflectionRays frameRays; // the frame's ray scene, once (buildRayScene), for refraction and reflections
            m_renderer->beginRenderPass(refractOn);
            renderCtx.cmd = cmd;
            renderCtx.frameIndex = m_renderer->currentFrameIndex();
            for (uint32_t i = 0; drawScene && i < viewCount; ++i) {
                const DrawView& d = drawViews[i];
                renderCtx.view = d.view;
                renderCtx.proj = d.proj;
                renderCtx.cameraPos = d.camera.position;
                renderCtx.aspectRatio = d.aspect;
                renderCtx.lightingDescriptorSet = d.lighting->descriptorSet();
                renderCtx.viewIndex = i;
                // The first view's part was cleared with the pass; later
                // ones may sit over it (picture-in-picture).
                if (viewCount > 1) m_renderer->beginView(viewPixels(d.rect, sceneExtent), i > 0);
                if (m_skyRenderer) m_skyRenderer->draw(cmd, d.view, d.proj, renderCtx.lightingDescriptorSet);
                for (Module* m : m_initOrder) {
                    safeInvoke(m, "render", [&] { m->render(renderCtx); });
                }
                if (refractOn) {
                    // What's drawn so far, readable by the see-through surfaces drawn next.
                    m_renderer->pauseScenePass();
                    m_behindPass->record(cmd, m_renderer->currentFrameIndex(), m_renderer->sceneImages(), d.proj);
                    if (m_recordRays) {
                        // Rays for refraction: the ray scene from the opaque draws, built now (outside the pass).
                        buildRayScene(cmd, d.view, skyEnv, frameRays);
                        if (!m_refractionRays) m_refractionRays = std::make_unique<RefractionRays>(m_renderer->device());
                        renderCtx.refractionRays = m_refractionRays->update(m_renderer->currentFrameIndex(), *m_rayScene, m_rayMaterials,
                                                                            frameRays.viewToWorld, frameRays.sunDirection, frameRays.sunColor,
                                                                            frameRays.ambient, frameRays.classic);
                    }
                    m_renderer->resumeScenePass();
                }
                renderCtx.refraction = refractOn;
                for (Module* m : m_initOrder) {
                    safeInvoke(m, "renderTranslucent", [&] { m->renderTranslucent(renderCtx); });
                }
                renderCtx.refraction = false;
                renderCtx.refractionRays = VK_NULL_HANDLE;
            }
            // GI from this frame's scene, for the next frame's shading.
            if (giOn && drawScene) {
                m_renderer->endScenePass();
                m_screenGi->record(cmd, m_renderer->currentFrameIndex(), m_renderer->sceneImages(), view, proj, m_screenGiSettings);
            } else if (m_screenGi) {
                m_screenGi->invalidate();
            }
            if (reflOn && drawScene) {
                m_renderer->endScenePass();
                // KKE ray tracing: this frame's stand-ins from every module that has them (built already when refraction traced).
                const bool traced = (m_reflections == Reflections::Kke || m_reflections == Reflections::Classic) && m_renderer->device().rayQuery();
                if (traced && !frameRays.built) buildRayScene(cmd, view, skyEnv, frameRays);
                m_reflectionPass->record(cmd, m_renderer->currentFrameIndex(), m_renderer->sceneImages(), view, proj, m_reflectionSettings,
                                         traced ? &frameRays : nullptr);
            } else if (m_reflectionPass) {
                m_reflectionPass->invalidate();
            }
            if (shadowRaysOn && drawScene) {
                m_renderer->endScenePass();
                if (!frameRays.built) buildRayScene(cmd, view, skyEnv, frameRays);
                ReflectionRays shadowRays = frameRays;
                shadowRays.classic = m_shadowRays == ShadowRays::Classic; // every pixel, or only past the shadow map
                m_shadowRayPass->record(cmd, m_renderer->currentFrameIndex(), m_renderer->sceneImages(), view, proj, shadowRays, m_shadowRaySettings);
            } else if (m_shadowRayPass) {
                m_shadowRayPass->invalidate();
            }
            // The overlay covers the whole window with the first camera.
            renderCtx.view = view;
            renderCtx.proj = proj;
            renderCtx.cameraPos = drawViews[0].camera.position;
            renderCtx.aspectRatio = m_renderer->aspectRatio();
            renderCtx.lightingDescriptorSet = m_lightingBuffer->descriptorSet();
            renderCtx.viewIndex = 0;
            m_renderer->beginOverlayPass();
            for (Module* m : m_initOrder) {
                safeInvoke(m, "renderOverlay", [&] { m->renderOverlay(renderCtx); });
            }
            m_debugUi->render(cmd);

            benchBeforeEnd = BenchClock::now();
            m_renderer->endFrame();
            benchAfterEnd = BenchClock::now();
        }

        for (Module* m : m_initOrder) safeInvoke(m, "frameEnd", [&] { m->frameEnd(); });

        // In the background the governor's cap wins (a paused game in
        // another window shouldn't keep a core and the GPU busy).
        float limit = m_frameRateLimit;
        const SDL_WindowFlags flags = SDL_GetWindowFlags(m_window.handle());
        const bool background = !(flags & SDL_WINDOW_INPUT_FOCUS) || (flags & SDL_WINDOW_MINIMIZED);
        if (background && m_budget.backgroundFrameRate > 0.0f)
            limit = limit > 0.0f ? std::min(limit, m_budget.backgroundFrameRate) : m_budget.backgroundFrameRate;
        m_effectiveLimit = limit;
        if (limit > 0.0f) {
            auto frameEnd = lastFrameTime + std::chrono::duration<double>(1.0 / limit);
            std::this_thread::sleep_until(frameEnd);
        }
        if (m_bench) {
            benchIdleEnd = BenchClock::now();
            m_bench->addStage(BenchRecorder::GpuWait, msSince(benchBeforeBegin, benchAfterBegin));
            m_bench->addStage(BenchRecorder::Record, msSince(benchAfterUpdate, benchBeforeBegin) + msSince(benchAfterBegin, benchBeforeEnd));
            m_bench->addStage(BenchRecorder::Present, msSince(benchBeforeEnd, benchAfterEnd));
            m_bench->addStage(BenchRecorder::Idle, msSince(benchAfterEnd, benchIdleEnd));
            m_bench->setFrameInfo(m_renderer->lastGpuFrameTimeMs(), static_cast<int>(m_fixedStepsLastFrame), static_cast<int>(stepCap));
        }
    }
    if (m_bench) writeBenchmarkReport();
    startQuitWatchdog();
}

void Application::setScreenGi(bool on) {
    m_screenGiOn = on || m_screenGiEnv;
    // The scene's depth is kept only while something reads it.
    updateSceneDepthReadable();
    if (!m_screenGiOn && m_screenGi) m_screenGi->invalidate();
}

void Application::forgetRayDraws(DynamicMeshRenderer* mesh) {
    m_rayDraws.erase(std::remove_if(m_rayDraws.begin(), m_rayDraws.end(), [mesh](const RayDraw& d) { return d.mesh == mesh; }), m_rayDraws.end());
    auto drop = [&](std::unordered_map<const DynamicMeshRenderer*, MeshRay>& map) {
        if (auto it = map.find(mesh); it != map.end()) {
            if (m_rayScene) m_rayScene->remove(it->second.geometry);
            map.erase(it);
        }
    };
    drop(m_meshBoxes);
    for (auto& slot : m_meshTriangles) drop(slot);
}

namespace {
constexpr uint32_t kKkeMeshTriangles = 20000; // KKE ray tracing: meshes up to this many triangles are traced as they are
} // namespace

void Application::addMeshesToRayScene(bool classic, VkCommandBuffer cmd, uint32_t frame) {
    RayScene& scene = *m_rayScene;
    for (const RayDraw& d : m_rayDraws) {
        const DynamicMeshRenderer::RayView v = d.mesh->rayView(frame);
        if (v.indexCount < 3) continue;
        // KKE takes small meshes as they are (a floor, a prop, a tentacle: a box round
        // those would cover the floor it stands on) and stands a box in for big ones.
        if (!classic && v.indexCount / 3 > kKkeMeshTriangles) {
            // A box round the vertices in their average colour, remade when they change.
            MeshRay& r = m_meshBoxes[d.mesh];
            if (r.version != v.version || r.vertexCount == 0) {
                if (r.vertexCount != 0) scene.remove(r.geometry);
                r.geometry = scene.addBox(glm::max((v.hi - v.lo) * 0.5f, glm::vec3(0.01f)));
                r.version = v.version;
                r.vertexCount = v.vertexCount;
            }
            RayMaterial m;
            m.color = glm::vec4(v.color, 1.0f);
            m_rayMaterials.push_back(m);
            scene.add(r.geometry, d.model * glm::translate(glm::mat4(1.0f), (v.hi + v.lo) * 0.5f), static_cast<uint32_t>(m_rayMaterials.size() - 1));
            continue;
        }
        // Their triangles: this frame slot's buffers, refitted when the vertices change, rebuilt when their number or buffer does.
        if (!v.vertices) continue;
        MeshRay& r = m_meshTriangles[frame % 2][d.mesh];
        if (r.buffer != v.vertexBuffer || r.vertexCount != v.vertexCount || r.indexCount != v.indexCount) {
            if (r.buffer) scene.remove(r.geometry);
            r.geometry = scene.addDynamic(v.vertices, sizeof(Vertex), v.vertexCount, v.indices, v.indexCount);
            r.buffer = v.vertexBuffer;
            r.vertexCount = v.vertexCount;
            r.indexCount = v.indexCount;
            r.version = v.version;
        } else if (r.version != v.version) {
            scene.refit(cmd, r.geometry, v.vertices);
            r.version = v.version;
        }
        RayMaterial m;
        m.color = glm::vec4(1.0f); // the vertex colours carry it
        m.vertices = v.vertices;
        m.indices = v.indices;
        m.stride = sizeof(Vertex);
        m.normalOffset = offsetof(Vertex, normal);
        m.colorOffset = offsetof(Vertex, color);
        m_rayMaterials.push_back(m);
        scene.add(r.geometry, d.model, static_cast<uint32_t>(m_rayMaterials.size() - 1));
    }
    m_rayDraws.clear();
}

void Application::buildRayScene(VkCommandBuffer cmd, const glm::mat4& view, const SkyEnvironment* sky, ReflectionRays& rays) {
    if (!m_rayScene) m_rayScene = std::make_unique<RayScene>(m_renderer->device());
    m_rayScene->clear();
    m_rayMaterials.clear();
    // Full meshes when either ray user is classic; stand-ins otherwise.
    RaySceneContext context{ m_reflections == Reflections::Classic || m_shadowRays == ShadowRays::Classic, cmd, m_renderer->currentFrameIndex() };
    for (IRayContributor* c : findCapability<IRayContributor>()) c->addToRayScene(*m_rayScene, m_rayMaterials, context);
    addMeshesToRayScene(context.classic, cmd, context.frame);
    m_rayScene->build(cmd, context.frame);
    rays.scene = m_rayScene.get();
    rays.materials = &m_rayMaterials;
    rays.viewToWorld = glm::inverse(view);
    const Light& sun = m_lighting.lights[0];
    if (sun.enabled && sun.isDirectional) {
        rays.sunDirection = -sun.direction;
        rays.sunColor = sun.color * sun.intensity;
    }
    rays.ambient = sky ? sky->ambientAverage : m_lighting.ambientColor;
    rays.classic = context.classic;
    rays.budget = context.classic ? ~0u : m_reflectionSettings.rayBudget; // classic: every smooth pixel
    rays.built = true;
}

VkDescriptorSetLayout Application::refractionRayLayout() {
    if (!m_refractionRays) m_refractionRays = std::make_unique<RefractionRays>(m_renderer->device());
    return m_refractionRays->setLayout();
}

void Application::setSmaa(bool on) { m_renderer->setSmaa(m_smaaEnv ? *m_smaaEnv : on); }

void Application::setShadowRays(ShadowRays mode) {
    m_shadowRays = m_shadowRaysEnv ? *m_shadowRaysEnv : mode;
    updateSceneDepthReadable();
    if (m_shadowRays == ShadowRays::Off && m_shadowRayPass) m_shadowRayPass->invalidate();
}

void Application::setBloom(bool on) { m_renderer->setBloom(m_bloomEnv ? *m_bloomEnv : on); }

void Application::setTaa(bool on) {
    m_taaOn = m_taaEnv ? *m_taaEnv : on;
    m_renderer->setTaa(m_taaOn);
    updateSceneDepthReadable();
}

void Application::setDepthOfField(bool on) {
    m_dofOn = m_dofEnv ? *m_dofEnv : on;
    m_renderer->setDepthOfField(m_dofOn);
    updateSceneDepthReadable();
}

DofSettings& Application::depthOfFieldSettings() { return m_renderer->depthOfFieldSettings(); }

void Application::updateSceneDepthReadable() {
    m_renderer->setSceneDepthReadable(m_screenGiOn || m_reflections != Reflections::Off || m_taaOn || m_shadowRays != ShadowRays::Off || m_dofOn);
}

void Application::setReflections(Reflections mode) {
    m_reflections = m_reflectionsEnv ? *m_reflectionsEnv : mode;
    // Kke and classic trace rays only where the device can; screen space works everywhere.
    updateSceneDepthReadable();
    if (m_reflections == Reflections::Off && m_reflectionPass) m_reflectionPass->invalidate();
}

Reflections reflectionsFromName(const std::string& name, Reflections fallback) {
    if (name == "off") return Reflections::Off;
    if (name == "screen") return Reflections::Screen;
    if (name == "kke") return Reflections::Kke;
    if (name == "classic") return Reflections::Classic;
    return fallback;
}

void Application::setResourceBudget(const ResourceBudget& budget) {
    m_budget = budget;
    // KKE_FULL_SPEED_IN_BACKGROUND=1 (developer builds; tools/check_game and
    // kkrun set it): no background cap. Tests and demos run in windows that
    // don't have the focus (on soucouyant Kees is using the desktop), and
    // at 15 fps they measured the cap, not the game. Players keep the cap.
    if (dev::flag("KKE_FULL_SPEED_IN_BACKGROUND")) m_budget.backgroundFrameRate = 0.0f;
    m_frameRateLimit = budget.frameRateLimit;
    if (m_renderer) m_renderer->setRenderScale(budget.renderScale);
    log::get("Governor")->info("budget: {} worker thread(s), frame cap {}, background cap {}, render scale {:.2f}{}", budget.workerThreads,
                               budget.frameRateLimit, m_budget.backgroundFrameRate, budget.renderScale,
                               budget.useEverything ? " (use everything)" : "");
}

VkDescriptorSet Application::textureSet(const std::string& path) {
    if (path.empty()) return VK_NULL_HANDLE;
    if (auto it = m_textureCache.find(path); it != m_textureCache.end()) return it->second.set;
    VkDevice dev = m_renderer->device().device();
    if (!m_textureCachePool) {
        constexpr uint32_t kMaxCachedTextures = 1024;
        VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxCachedTextures };
        VkDescriptorPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        info.maxSets = kMaxCachedTextures;
        info.poolSizeCount = 1;
        info.pPoolSizes = &size;
        VK_CHECK(vkCreateDescriptorPool(dev, &info, nullptr, &m_textureCachePool));
    }
    CachedTexture entry;
    try {
        entry.texture = std::make_unique<Texture>(m_renderer->device(), path);
        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool = m_textureCachePool;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &m_materialTextureSetLayout;
        if (vkAllocateDescriptorSets(dev, &alloc, &entry.set) != VK_SUCCESS) {
            log::get("Textures")->warn("texture cache full; '{}' drawn untextured", path);
            entry = {};
        } else {
            VkDescriptorImageInfo image{ entry.texture->sampler(), entry.texture->imageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = entry.set;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &image;
            vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);
        }
    } catch (const std::exception& e) {
        log::get("Textures")->warn("texture '{}' failed to load ({}); drawn untextured", path, e.what());
        entry = {};
    }
    VkDescriptorSet set = entry.set;
    m_textureCache[path] = std::move(entry);
    return set;
}

const Texture* Application::cachedTexture(const std::string& path, bool linearData) {
    if (!linearData) {
        if (!textureSet(path)) return nullptr;
        return m_textureCache[path].texture.get();
    }
    // Data maps share the cache under their own key and need no set.
    const std::string key = "linear:" + path;
    auto it = m_textureCache.find(key);
    if (it != m_textureCache.end()) return it->second.texture.get();
    CachedTexture entry;
    try {
        entry.texture = std::make_unique<Texture>(m_renderer->device(), path, Texture::LinearData{});
    } catch (const std::exception& e) {
        log::get("Textures")->warn("texture '{}' failed to load ({}); drawn without it", path, e.what());
    }
    const Texture* texture = entry.texture.get();
    m_textureCache[key] = std::move(entry);
    return texture;
}

int Application::addEventClaim(EventClaim claim) {
    m_eventClaims.emplace_back(m_nextEventClaim, std::move(claim));
    return m_nextEventClaim++;
}

void Application::removeEventClaim(int id) {
    std::erase_if(m_eventClaims, [id](const auto& c) { return c.first == id; });
}

ScreenRect Application::uiSafeRect() const {
    const VkExtent2D extent = m_renderer->extent();
    const float width = static_cast<float>(extent.width), height = static_cast<float>(extent.height);
    ScreenRect system;
    int windowW = 0, windowH = 0;
    SDL_Rect safe{};
    if (SDL_GetWindowSize(m_window.handle(), &windowW, &windowH) && windowW > 0 && windowH > 0 &&
        SDL_GetWindowSafeArea(m_window.handle(), &safe)) {
        // Window points -> the frame's pixels (the frame follows the window's shape).
        const float sx = width / static_cast<float>(windowW), sy = height / static_cast<float>(windowH);
        system = { static_cast<float>(safe.x) * sx, static_cast<float>(safe.y) * sy, static_cast<float>(safe.w) * sx,
                   static_cast<float>(safe.h) * sy };
    }
    const float margin = std::round(uiMarginFraction(m_uiProfile) * std::min(width, height));
    return safeScreenRect(width, height, system, margin);
}

} // namespace kke
