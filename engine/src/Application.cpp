#include "kke/Application.h"
#include "kke/SkyRenderer.h"
#include "kke/Mood.h"
#include "kke/EngineSettings.h"
#include "kke/LogoIntro.h"
#include "kke/DevTools.h"
#include "kke/BenchRecorder.h"
#include "kke/BenchmarkReport.h"
#include "kke/Platform.h"

#include <cstdlib>
#include "kke/Log.h"
#include "kke/VulkanCheck.h"

#include <glm/gtc/matrix_transform.hpp>
#include <stb_image_write.h>
#include <algorithm>
#include <cctype>
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
        EngineSettings defaults = targetDefaultSettings();
        if (const char* all = dev::env("KKE_USE_EVERYTHING"); all && *all == '1') defaults.performance.useEverything = true;
        setResourceBudget(computeBudget(defaults, usableCpuCount()));
    }
    m_renderer = std::make_unique<Renderer>(m_window);
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
        if (!m_toneMapperOverride) log::get("Application")->warn("KKE_TONEMAP={} is not agx, aces or reinhard; ignored", env);
    }
    m_lightingBuffer = std::make_unique<LightingBuffer>(m_renderer->device());
    m_mood = std::make_unique<Mood>();
    if (const char* env = dev::env("KKE_MOOD"); env && *env) m_moodOverride = env;

    // Real shadow mapping — see kke::ShadowMap's own class comment for
    // the full scope. 2048 is ShadowMap's own default resolution,
    // passed explicitly here anyway so the choice is visible at the
    // call site, not buried in a default argument.
    m_shadowMap = std::make_unique<ShadowMap>(m_renderer->device(), 2048);

    // The shadow map sampler's own descriptor set layout/pool/set (set
    // 1 in cube.frag) -- separate from ShadowMap's own render
    // pass/framebuffer (see Application.h's own comment on why this
    // lives here). One combined-image-sampler binding, fragment-stage
    // only (the vertex stage only needs the light-space matrix, which
    // already travels through LightingBuffer's own UBO, not this).
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        VK_CHECK(vkCreateDescriptorSetLayout(m_renderer->device().device(), &layoutInfo, nullptr, &m_shadowMapSetLayout));

        VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
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

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = m_shadowMap->imageView();
        imageInfo.sampler = m_shadowMap->sampler();

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_shadowMapDescriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(m_renderer->device().device(), 1, &write, 0, nullptr);
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
                                           m_renderer->swapChainImageCount());
    // Shipping builds start (and stay) with the developer panels hidden.
    m_debugUi->setVisible(dev::kEnabled);
}

Application::~Application() {
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
        safeInvoke(*it, "shutdown", [&] { (*it)->shutdown(); });
    }

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

    log::shutdown(); // flush the async queue before the process exits
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
                FixedUpdateContext fixedCtx{ m_fixedDt, tickIndex };
                for (Module* m : m_initOrder) {
                    safeInvoke(m, "fixedUpdate", [&] { m->fixedUpdate(fixedCtx); });
                }
                accumulator -= m_fixedDt;
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
            float effectiveDt = m_paused ? m_fixedDt : dt;
            UpdateContext updateCtx{ effectiveDt, totalTime };
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
            d.proj = glm::perspective(glm::radians(c.fovDegrees), d.aspect, c.nearPlane, c.farPlane);
            d.proj[1][1] *= -1.0f; // Vulkan's clip space Y is flipped relative to GLM's assumption.
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
        const glm::mat4& view = drawViews[0].view;
        const glm::mat4& proj = drawViews[0].proj;

        // Real shadow mapping (see kke::ShadowMap) — computed from the
        // key light (lights[0]) only, over a 15 m radius around what the
        // first view's camera looks at, so shadows follow the player
        // through a level bigger than that. 15 keeps the texels dense
        // enough that edges don't turn blocky (checked against a
        // screenshot). The centre is snapped to shadow texels so the
        // edges don't shimmer as the camera moves.
        glm::mat4 lightViewProj = ShadowMap::computeLightViewProj(
            m_lighting.lights[0].direction, drawViews[0].camera.target, 15.0f, m_shadowMap->resolution());

        RenderContext renderCtx{};
        renderCtx.renderPass = m_renderer->renderPass();
        renderCtx.shadowMapDescriptorSet = m_shadowMapDescriptorSet;
        renderCtx.defaultMaterialTextureDescriptorSet = m_defaultTextureDescriptorSet;
        renderCtx.viewCount = viewCount;

        // Once per frame, before any module's render() might bind and
        // draw using it — every lit module shares one buffer and
        // descriptor set per view (see LightingBuffer.h).
        if (m_toneMapperOverride) m_lighting.toneMapper = *m_toneMapperOverride;
        const SkyEnvironment* skyEnv = nullptr;
        if (m_lighting.sky.kind != Sky::Kind::None && !m_skyRenderer) m_skyRenderer = std::make_unique<SkyRenderer>(*this);
        if (m_skyRenderer) skyEnv = &m_skyRenderer->prepare(m_lighting.sky, m_lighting.fog, m_lighting.ambientColor);
        for (uint32_t i = 0; i < viewCount; ++i)
            drawViews[i].lighting->update(m_lighting, drawViews[i].camera.position, lightViewProj, drawViews[i].proj * drawViews[i].view, skyEnv);

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
            m_debugUi->beginFrame();
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
                for (Module* m : m_initOrder) {
                    safeInvoke(m, "renderShadow", [&] { m->renderShadow(shadowCtx); });
                }
                m_shadowMap->endRenderPass(cmd);
            }

            PrepassContext prepassCtx{ cmd, view, proj, drawViews[0].camera.position, sceneExtent, m_lightingBuffer->descriptorSet(),
                                       m_renderer->currentFrameIndex(), m_sceneCovered };
            for (Module* m : m_initOrder) {
                safeInvoke(m, "prepass", [&] { m->prepass(prepassCtx); });
            }

            m_renderer->beginRenderPass();
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
            m_bench->setFrameInfo(m_renderer->lastGpuFrameTimeMs(), static_cast<int>(m_fixedStepsLastFrame), static_cast<int>(m_maxFixedStepsPerFrame));
        }
    }
    if (m_bench) writeBenchmarkReport();
}

void Application::setResourceBudget(const ResourceBudget& budget) {
    m_budget = budget;
    m_frameRateLimit = budget.frameRateLimit;
    if (m_renderer) m_renderer->setRenderScale(budget.renderScale);
    log::get("Governor")->info("budget: {} worker thread(s), frame cap {}, background cap {}, render scale {:.2f}{}", budget.workerThreads,
                               budget.frameRateLimit, budget.backgroundFrameRate, budget.renderScale,
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

} // namespace kke
