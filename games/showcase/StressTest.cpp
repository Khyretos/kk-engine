// The end-user stress test (ACTION_PLAN.md 1.7). Same script on every
// machine, so reports compare: graphics (the course, the character with
// animation + IK, shadows), Jolt (a rain of 300 crates) and impacts:
// FEMFX iron balls breaking glass, wood and stone, or, in a build
// without FEMFX (the default), heavy Jolt blocks fired into the crate
// pile. Each is its own phase. Runs
// uncapped (no vsync, no frame cap) to measure what the machine can do,
// then puts the player's settings back.

#include "ShowcaseModule.h"
#include "kke/Application.h"
#include "kke/BenchmarkReport.h"
#include "kke/Log.h"
#include "kke/modules/PhysicsModule.h"
#include "kke/modules/RigidBodyModule.h"
#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>
#include <cstdlib>

namespace kke_showcase {

namespace {
// Phase boundaries in seconds. The first 2 s aren't recorded (the
// teleport, first-use pipeline and texture work).
constexpr float kWarmup = 2.0f;
constexpr float kPhaseEnd[] = { 12.0f, 24.0f, 36.0f };
const char* const kPhaseName[] = { "walk", "crates", "impacts" };
constexpr int kRainCrates = 300;
constexpr float kRainPerSecond = 30.0f;
constexpr float kShotsPerSecond = 4.0f;
const glm::vec3 kRainCenter(0.0f, 0.0f, -2.0f);
const glm::vec3 kYard(14.0f, 0.0f, -6.0f); // ShowcaseModule::spawnBreakables

double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

void ShowcaseModule::startStressTest() {
    if (m_stressActive) return;
    m_stressActive = true;
    m_stressTime = 0.0f;
    m_stressPhase = -1;
    m_stressSpawn = m_stressShot = 0.0f;
    m_stressShots = m_stressRained = 0;
    m_stressRandom = 12345u; // same crates every run
    m_stressStats.clear();
    m_stressFirstCrate = m_crates.size();
    m_stressLastTick = nowSeconds();
    // Uncapped: this measures the machine, not the player's cap.
    m_stressSavedBudget = m_app->resourceBudget();
    m_stressSavedVsync = m_app->renderer().vsync();
    kke::ResourceBudget b = m_stressSavedBudget;
    b.frameRateLimit = 0.0f;
    b.backgroundFrameRate = 0.0f;
    m_app->setResourceBudget(b);
    m_app->renderer().setVSync(false);
    // Walk phase: the parkour lane (or the scene you're in) by autopilot.
    m_autopilot = true;
    m_autopilotTime = 0.0f;
    if (m_autopilotStart == glm::vec3(0.0f)) m_autopilotStart = glm::vec3(20.0f, 0.05f, 28.0f);
    m_loco->teleport(m_autopilotStart);
    if (std::abs(m_autopilotStart.x) < 100.0f) m_rig.yaw = 0.0f;
    m_rig.mode = kke::CameraRig::Mode::ThirdPerson;
    m_status = "Stress test: warming up";
    kke::log::get(name())->info("stress test: started ({} s)", kPhaseEnd[2]);
}

void ShowcaseModule::updateStressTest(float dt) {
    if (!m_stressActive) return;
    const double now = nowSeconds();
    const double frameMs = (now - m_stressLastTick) * 1000.0;
    m_stressLastTick = now;
    m_stressTime += dt;

    int phase = -1;
    if (m_stressTime >= kWarmup)
        for (int p = 0; p < 3; ++p)
            if (m_stressTime < kPhaseEnd[p]) { phase = p; break; }
    if (m_stressTime >= kPhaseEnd[2]) {
        finishStressTest();
        return;
    }
    if (phase != m_stressPhase && phase >= 0) {
        m_stressPhase = phase;
        m_stressStats.beginPhase(kPhaseName[phase]);
        if (phase == 1) {
            // Crate rain on the course, watched from the start spot.
            m_autopilot = false;
            m_loco->teleport(glm::vec3(kRainCenter.x, 0.05f, kRainCenter.z + 9.0f));
            m_rig.yaw = 0.0f;
            m_rig.pitch = -15.0f;
        } else if (phase == 2 && m_femfx) {
            // The breaking yard, from in front of it.
            m_loco->teleport(kYard + glm::vec3(0.0f, 0.05f, 9.0f));
            m_rig.yaw = 0.0f;
            m_rig.pitch = -10.0f;
        }
    }
    if (m_stressPhase < 0) return; // still warming up

    kke::RigidWorld& w = m_rigid->world();
    kke::FrameStats::Frame f;
    f.frameMs = frameMs;
    f.gpuMs = m_app->renderer().lastGpuFrameTimeMs();
    f.physicsMs = w.lastStepMs();
    f.bodies = static_cast<int>(w.bodyCount());
#if KKE_ENABLE_FEMFX
    if (m_femfx) {
        f.physicsMs += m_femfx->lastStepMsAvg();
        f.bodies += static_cast<int>(m_femfx->objectCount());
    }
#endif
    m_stressStats.add(f);

    // Crates keep raining in the impacts phase too, until 300 fell.
    if (m_stressPhase >= 1 && m_stressRained < kRainCrates) {
        m_stressSpawn += dt * kRainPerSecond;
        auto rnd = [this] {
            m_stressRandom = m_stressRandom * 1664525u + 1013904223u;
            return (m_stressRandom >> 8) / 16777216.0f;
        };
        while (m_stressSpawn >= 1.0f && m_stressRained < kRainCrates) {
            m_stressSpawn -= 1.0f;
            ++m_stressRained;
            kke::RigidWorld::BodyDesc d;
            d.halfExtents = glm::vec3(0.2f + 0.25f * rnd());
            d.density = 250.0f;
            d.position = kRainCenter + glm::vec3((rnd() - 0.5f) * 5.0f, 6.0f + rnd() * 4.0f, (rnd() - 0.5f) * 5.0f);
            d.rotation = glm::angleAxis(rnd() * 6.2832f, glm::normalize(glm::vec3(rnd() - 0.5f, 1.0f, rnd() - 0.5f)));
            d.material = 2;
            m_crates.push_back({ w.add(d), d.halfExtents, static_cast<int>(m_crates.size() % 3) });
        }
    }
    if (m_stressPhase == 2) {
        m_stressShot += dt * kShotsPerSecond;
        const glm::vec3 targets[] = { kYard + glm::vec3(0.0f, 0.55f, 0.0f), kYard + glm::vec3(0.0f, 0.6f, 3.0f),
                                      kYard + glm::vec3(3.5f, 1.0f, -2.0f) };
        while (m_stressShot >= 1.0f) {
            m_stressShot -= 1.0f;
            const float side = -1.5f + (m_stressShots % 3) * 1.5f;
            if (m_femfx) stressShoot(kYard + glm::vec3(side, 2.5f, 7.0f), targets[m_stressShots % 3]);
            else stressShoot(kRainCenter + glm::vec3(side, 2.5f, 7.0f), kRainCenter + glm::vec3(side * 0.5f, 0.6f, 0.0f));
            ++m_stressShots;
        }
    }
    m_status = "Stress test: " + std::string(kPhaseName[m_stressPhase]) + " (" + std::to_string(static_cast<int>(kPhaseEnd[2] - m_stressTime)) +
               " s left)";
}

// Something heavy on a ballistic path that lands on `target`: a FEMFX
// iron ball, or without FEMFX an iron block (Jolt).
void ShowcaseModule::stressShoot(const glm::vec3& from, const glm::vec3& target) {
    const float speed = 18.0f, g = 9.81f;
    const glm::vec3 d = target - from;
    const float t = glm::length(glm::vec2(d.x, d.z)) / speed;
    const glm::vec3 v(d.x / t, d.y / t + 0.5f * g * t, d.z / t);
    if (!m_femfx) {
        kke::RigidWorld::BodyDesc b;
        b.halfExtents = glm::vec3(0.15f);
        b.density = 7800.0f;
        b.position = from;
        b.velocity = v;
        b.material = 1;
        m_crates.push_back({ m_rigid->world().add(b), b.halfExtents, 3 });
        return;
    }
#if KKE_ENABLE_FEMFX
    kke::Material iron;
    iron.density = 7800.0f; iron.stiffness = 2.0e7f; iron.poissonsRatio = 0.3f;
    iron.fractureStressThreshold = 1.0e12f; iron.metallic = 0.9f; iron.roughness = 0.35f; iron.textureId = 2;
    m_femfx->spawnFracturableTetMesh(kke::PhysicsModule::buildSphere(3, 0.15f), from, iron, v);
#endif
}

void ShowcaseModule::finishStressTest() {
    m_stressActive = false;
    m_autopilot = false;
    kke::RigidWorld& w = m_rigid->world();
    for (size_t i = m_stressFirstCrate; i < m_crates.size(); ++i) w.remove(m_crates[i].body);
    m_crates.resize(m_stressFirstCrate);
    m_app->setResourceBudget(m_stressSavedBudget);
    m_app->renderer().setVSync(m_stressSavedVsync);

    kke::BenchmarkReport r;
    r.name = "stress";
    r.system = kke::collectSystemInfo(m_app->device().physicalDevice());
    const VkExtent2D e = m_app->renderer().extent();
    const VkExtent2D re = m_app->renderer().renderExtent();
    r.config = {
        { "script", m_femfx ? "showcase stress test: walk (autopilot, animation + IK, shadows), crates (300 Jolt boxes rain), impacts (FEMFX iron balls breaking glass/wood/stone)"
                            : "showcase stress test: walk (autopilot, animation + IK, shadows), crates (300 Jolt boxes rain), impacts (Jolt iron blocks fired into the pile; FEMFX not in this build)" },
        { "phases_s", "warmup 2 (not recorded), walk 10, crates 12, impacts 12" },
        { "resolution", std::to_string(e.width) + "x" + std::to_string(e.height) },
        { "render_resolution", std::to_string(re.width) + "x" + std::to_string(re.height) },
        { "render_scale", std::to_string(m_stressSavedBudget.renderScale) },
        { "vsync_and_frame_cap", "off during the test (measures the machine, not the cap)" },
        { "worker_threads", std::to_string(m_stressSavedBudget.workerThreads) },
        { "usable_cores", std::to_string(kke::usableCpuCount()) },
        { "use_everything", m_stressSavedBudget.useEverything ? "yes" : "no" },
        { "character", m_character.empty() ? "UAL mannequin" : m_character },
#if KKE_ENABLE_FEMFX
        { "femfx", m_femfx ? "on" : "off" },
#else
        { "femfx", "not built" },
#endif
    };
    m_stressStats.fill(r);
    r.results.emplace_back("peak_rss_mb", kke::peakResidentMemoryMb());
    const char* dirEnv = std::getenv("KKE_BENCH_DIR");
    const std::string dir = dirEnv && *dirEnv ? dirEnv : "benchmark";
    m_stressReport = r.writeFiles(dir, kke::timestampForFileName(), kke::hostNameForFileName());
    const kke::FrameStats::Summary all = m_stressStats.overall();
    kke::log::get(name())->info("STRESS RESULT: {} - {:.1f} fps avg, {:.1f} fps 1% low, worst frame {:.1f} ms", kke::FrameStats::verdict(all),
                                all.fpsAvg, all.low1Fps, all.frameMaxMs);
    if (m_stressReport.empty()) {
        kke::log::get(name())->error("stress test: could not write the report to '{}'", dir);
        m_status = "Stress test done, but the report couldn't be written to " + dir;
    } else {
        kke::log::get(name())->info("STRESS REPORT: {}.txt and .json", m_stressReport);
        m_status = std::string("Stress test: ") + kke::FrameStats::verdict(all) + ". Report: " + m_stressReport + ".txt";
    }
    m_loco->teleport(m_spawn);
    m_rig.yaw = 0.0f;
    if (m_stressQuitAtEnd) {
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
    }
}

} // namespace kke_showcase
