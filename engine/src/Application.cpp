#include "kke/Application.h"
#include "kke/Log.h"

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
    m_debugUi = std::make_unique<DebugUi>(m_window, m_renderer->device(), m_renderer->renderPass(),
                                           m_renderer->swapChainImageCount());
}

Application::~Application() {
    // Shut down in reverse dependency order: a module that depends on
    // another should tear itself down first, while what it depends on is
    // still alive to be torn down safely after. Faulted modules are
    // skipped entirely — see safeInvoke()'s comment on why even
    // shutdown() isn't trusted once a module has already thrown.
    for (auto it = m_initOrder.rbegin(); it != m_initOrder.rend(); ++it) {
        if (m_faultedModules.count(*it)) continue;
        safeInvoke(*it, "shutdown", [&] { (*it)->shutdown(); });
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

        RenderContext renderCtx{};
        renderCtx.view = view;
        renderCtx.proj = proj;
        renderCtx.cameraPos = m_camera.position;
        renderCtx.aspectRatio = m_renderer->aspectRatio();
        renderCtx.renderPass = m_renderer->renderPass();

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
