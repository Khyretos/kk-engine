#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/VulkanCheck.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <queue>
#include <stdexcept>
#include <string>

namespace kke {

Application::Application(const std::string& title, uint32_t width, uint32_t height, float fixedUpdateHz)
    : m_window(title, width, height), m_fixedDt(1.0f / fixedUpdateHz) {
    log::init(title);
    m_renderer = std::make_unique<Renderer>(m_window);
    m_lightingBuffer = std::make_unique<LightingBuffer>(m_renderer->device());

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

    m_debugUi = std::make_unique<DebugUi>(m_window, m_renderer->device(), m_renderer->renderPass(),
                                           m_renderer->swapChainImageCount());
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
}

void Application::resolveInitOrder() {
    m_moduleByType.clear();
    for (auto& m : m_modules) {
        // typeid(*m) resolves through the vtable to the concrete type, so
        // this map key is the module's real type regardless of how it's
        // stored (unique_ptr<Module>).
        m_moduleByType[std::type_index(typeid(*m))] = m.get();
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

void Application::run() {
    resolveInitOrder();
    for (Module* m : m_initOrder) {
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
    }

    auto startTime = std::chrono::high_resolution_clock::now();
    auto lastFrameTime = startTime;
    float accumulator = 0.0f;
    uint64_t tickIndex = 0;

    while (m_window.pollEvents([this](const SDL_Event& e) {
        m_debugUi->processEvent(e);
        for (Module* m : m_initOrder) {
            safeInvoke(m, "onEvent", [&] { m->onEvent(e); });
        }
    })) {
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - lastFrameTime).count();
        float totalTime = std::chrono::duration<float>(now - startTime).count();
        lastFrameTime = now;

        // --- Pause/step: see Application.h for the full reasoning.
        // Rendering below always runs regardless of m_paused — only
        // simulation (fixedUpdate/update/compute) is gated here.
        bool advancingThisFrame = !m_paused || m_stepRequested;

        if (!m_paused) {
            // Cap how much simulation time a single slow frame can inject
            // — otherwise a long stall (asset load, breakpoint) causes a
            // burst of catch-up fixed ticks that then takes even longer,
            // stalling further: the classic "spiral of death."
            accumulator = std::min(accumulator + dt, m_fixedDt * 8.0f);

            while (accumulator >= m_fixedDt) {
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

        glm::mat4 view = glm::lookAt(m_camera.position, m_camera.target, m_camera.up);
        glm::mat4 proj = glm::perspective(glm::radians(m_camera.fovDegrees), m_renderer->aspectRatio(),
                                           m_camera.nearPlane, m_camera.farPlane);
        proj[1][1] *= -1.0f; // Vulkan's clip space Y is flipped relative to GLM's assumption.

        // Real shadow mapping (see kke::ShadowMap) — computed from the
        // key light (lights[0]) only, framed around a fixed scene
        // region near the origin (radius 15) rather than any real
        // scene-bounds tracking, which this project doesn't have yet.
        // 15 was chosen to comfortably cover both kke_demo's cube and
        // physics_demo's ground plane + falling objects without the
        // shadow map's resolution being spread so thin the shadow
        // edges turn visibly blocky — checked against a real
        // screenshot, not picked blindly.
        glm::mat4 lightViewProj = ShadowMap::computeLightViewProj(
            m_lighting.lights[0].direction, glm::vec3(0.0f, 0.0f, 0.0f), 15.0f);

        RenderContext renderCtx{};
        renderCtx.view = view;
        renderCtx.proj = proj;
        renderCtx.cameraPos = m_camera.position;
        renderCtx.aspectRatio = m_renderer->aspectRatio();
        renderCtx.renderPass = m_renderer->renderPass();
        renderCtx.lightingDescriptorSet = m_lightingBuffer->descriptorSet();
        renderCtx.shadowMapDescriptorSet = m_shadowMapDescriptorSet;

        // Once per frame, before any module's render() might bind and
        // draw using it — every lit module shares this same one buffer
        // and descriptor set (see LightingBuffer.h).
        m_lightingBuffer->update(m_lighting, m_camera.position, lightViewProj);

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
        if (m_renderer->beginFrame()) {
            m_debugUi->beginFrame();
            for (Module* m : m_initOrder) {
                safeInvoke(m, "renderUi", [&] { m->renderUi(); });
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
            m_shadowMap->beginRenderPass(cmd);
            for (Module* m : m_initOrder) {
                safeInvoke(m, "renderShadow", [&] { m->renderShadow(shadowCtx); });
            }
            m_shadowMap->endRenderPass(cmd);

            m_renderer->beginRenderPass();
            renderCtx.cmd = cmd;
            for (Module* m : m_initOrder) {
                safeInvoke(m, "render", [&] { m->render(renderCtx); });
            }
            m_debugUi->render(cmd);

            m_renderer->endFrame();
        }
    }
}

} // namespace kke
