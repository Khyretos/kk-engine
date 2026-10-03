#include "ParticlesDemoModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/Mesh.h"
#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/InputModule.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

namespace kke_particles {

namespace {

constexpr float kCircle = 7.0f;   // m: the radius "all at once" spreads the effects round
constexpr float kStone = 0.35f;   // m: half the stone's width (and its height)

// A box as 24 vertices (flat normals) into v / idx.
void box(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx, const glm::vec3& mn, const glm::vec3& mx, const glm::vec3& color) {
    const glm::vec3 c[8] = { { mn.x, mn.y, mn.z }, { mx.x, mn.y, mn.z }, { mx.x, mx.y, mn.z }, { mn.x, mx.y, mn.z },
                             { mn.x, mn.y, mx.z }, { mx.x, mn.y, mx.z }, { mx.x, mx.y, mx.z }, { mn.x, mx.y, mx.z } };
    const int faces[6][4] = { { 4, 5, 6, 7 }, { 1, 0, 3, 2 }, { 5, 1, 2, 6 }, { 0, 4, 7, 3 }, { 7, 6, 2, 3 }, { 0, 1, 5, 4 } };
    const glm::vec3 normals[6] = { { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
    for (int f = 0; f < 6; ++f) {
        const uint32_t base = static_cast<uint32_t>(v.size());
        for (int k = 0; k < 4; ++k) v.push_back(kke::Vertex{ c[faces[f][k]], color, normals[f], { 0, 0 } });
        for (uint32_t k : { 0u, 1u, 2u, 0u, 2u, 3u }) idx.push_back(base + k);
    }
}

} // namespace

void ParticlesDemoModule::init(kke::Application& app) {
    m_app = &app;
    m_fx = std::make_unique<kke::ParticleEffects>(app, 12000);
    m_library = std::make_unique<kke::ParticleLibrary>(*m_fx);
    // Effects of your own: every .yaml / .yml / .json in assets/effects
    // beside the game adds to the library (or replaces one of the same name).
    const std::filesystem::path folder = "assets/effects";
    std::error_code ec;
    if (std::filesystem::is_directory(folder, ec))
        for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
            const std::string ext = entry.path().extension().string();
            if (ext != ".yaml" && ext != ".yml" && ext != ".json") continue;
            std::string error;
            if (m_library->load(entry.path(), &error)) m_loaded += (m_loaded.empty() ? "" : ", ") + entry.path().filename().string();
            else kke::log::get(name())->warn("{}", error);
        }
    m_names = m_library->names();
    if (const char* pick = std::getenv("KKE_PARTICLES_EFFECT")) {
        auto it = std::find(m_names.begin(), m_names.end(), pick);
        if (it != m_names.end()) m_current = static_cast<int>(it - m_names.begin());
    }
    if (const char* all = std::getenv("KKE_PARTICLES_ALL")) m_all = all[0] == '1';

    // A dark stone floor, a stone in the middle and one per place on the circle.
    m_ground = std::make_unique<kke::DynamicMeshRenderer>(app);
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    box(v, idx, glm::vec3(-12.0f, -0.2f, -12.0f), glm::vec3(12.0f, 0.0f, 12.0f), glm::vec3(0.2f, 0.21f, 0.22f));
    box(v, idx, glm::vec3(-kStone, 0.0f, -kStone), glm::vec3(kStone, kStone, kStone), glm::vec3(0.35f, 0.33f, 0.3f));
    const size_t n = std::max<size_t>(1, m_names.size());
    for (size_t i = 0; i < n; ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(n);
        const glm::vec3 c(std::cos(a) * kCircle, 0.0f, std::sin(a) * kCircle);
        box(v, idx, c - glm::vec3(kStone * 0.6f, 0.0f, kStone * 0.6f), c + glm::vec3(kStone * 0.6f, kStone * 0.6f, kStone * 0.6f),
            glm::vec3(0.3f, 0.29f, 0.27f));
    }
    m_ground->upload(v, idx);
    defineInput();
    buildPanel();
    restart();
}

void ParticlesDemoModule::shutdown() {
    if (m_library) m_library->stopAll();
    if (m_fx) m_fx->clear();
}

bool ParticlesDemoModule::weather(size_t index) const {
    const kke::ParticleLibrary::Effect* e = m_library->effect(m_names[index]);
    if (!e) return false;
    return std::any_of(e->layers.begin(), e->layers.end(), [](const kke::ParticleLibrary::Layer& l) { return l.box.x > 1.5f; });
}

void ParticlesDemoModule::playOne(size_t index, const glm::vec3& at) {
    kke::ParticleLibrary::Spot spot;
    const bool sky = weather(index);
    // Weather falls from above onto the whole floor; everything else comes off the stone.
    spot.position = sky ? glm::vec3(0.0f, 7.0f, 0.0f) : at + glm::vec3(0.0f, (at == glm::vec3(0.0f) ? kStone : kStone * 0.6f) + 0.02f, 0.0f);
    spot.scale = sky ? 1.0f : m_scale;
    spot.floor = 0.0f;
    if (m_library->looping(m_names[index])) m_library->start(m_names[index], spot);
    else m_library->play(m_names[index], spot);
}

void ParticlesDemoModule::restart() {
    m_library->stopAll();
    m_fx->clear();
    m_clock = 0.0f;
    m_stones.clear();
    if (m_names.empty()) return;
    if (!m_all) {
        m_stones.push_back(glm::vec3(0.0f));
        playOne(static_cast<size_t>(m_current), m_stones[0]);
        return;
    }
    for (size_t i = 0; i < m_names.size(); ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(m_names.size());
        m_stones.push_back(glm::vec3(std::cos(a) * kCircle, 0.0f, std::sin(a) * kCircle));
        // One sky at a time: the rest of the weather would bury the gallery.
        if (weather(i) && m_names[i] != "leaves") continue;
        playOne(i, m_stones.back());
    }
}

void ParticlesDemoModule::update(const kke::UpdateContext& ctx) {
    readInput();
    m_clock += ctx.dt;
    // Bursts again every few seconds (streams are still running).
    if (m_repeat && m_clock >= m_every) {
        m_clock = 0.0f;
        if (!m_all) {
            if (!m_library->looping(m_names[static_cast<size_t>(m_current)])) playOne(static_cast<size_t>(m_current), m_stones[0]);
        } else {
            for (size_t i = 0; i < m_names.size() && i < m_stones.size(); ++i)
                if (!m_library->looping(m_names[i]) && !weather(i)) playOne(i, m_stones[i]);
        }
    }
    m_library->update(ctx.dt, glm::vec3(m_wind, 0.0f, 0.0f));
}

void ParticlesDemoModule::render(const kke::RenderContext& ctx) { m_ground->draw(ctx, glm::mat4(1.0f), 0.0f, 0.9f); }

void ParticlesDemoModule::renderShadow(const kke::ShadowRenderContext& ctx) { m_ground->drawShadow(ctx); }

void ParticlesDemoModule::renderTranslucent(const kke::RenderContext& ctx) { m_fx->draw(ctx); }

void ParticlesDemoModule::defineInput() {
    auto* in = m_app->getModule<kke::InputModule>();
    if (!in) return;
    using IM = kke::InputModule;
    kke::InputMap& m = in->map(0);
    auto action = [&](const char* id, const char* label, SDL_Scancode key, SDL_GamepadButton pad) {
        m.defineAction({ id, label, "Particles" });
        m.addBinding(IM::bind(id, IM::key(key)));
        m.addBinding(IM::bind(id, IM::pad(pad)));
    };
    action("particles.play", "Play it again", SDL_SCANCODE_SPACE, SDL_GAMEPAD_BUTTON_SOUTH);
    action("particles.next", "Next effect", SDL_SCANCODE_RIGHT, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    action("particles.previous", "Previous effect", SDL_SCANCODE_LEFT, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    action("particles.all", "All at once", SDL_SCANCODE_A, SDL_GAMEPAD_BUTTON_NORTH);
    in->commitDefaults();
}

void ParticlesDemoModule::readInput() {
    auto* in = m_app->getModule<kke::InputModule>();
    if (!in || m_names.empty()) return;
    const kke::InputMap& m = in->map(0);
    const int n = static_cast<int>(m_names.size());
    if (m.pressed("particles.play")) {
        if (m_all) restart();
        else playOne(static_cast<size_t>(m_current), m_stones.empty() ? glm::vec3(0.0f) : m_stones[0]);
    }
    if (m.pressed("particles.next")) {
        m_current = (m_current + 1) % n;
        restart();
    }
    if (m.pressed("particles.previous")) {
        m_current = (m_current + n - 1) % n;
        restart();
    }
    if (m.pressed("particles.all")) {
        m_all = !m_all;
        restart();
    }
}

// The settings (RmlUi, kke::DemoPanelModule): a controller opens them with
// View, the keyboard with F3, the mouse just clicks.
void ParticlesDemoModule::buildPanel() {
    auto* panel = m_app->getModule<kke::DemoPanelModule>();
    if (!panel) return;
    auto& s = panel->section("Particles");
    s.text("{particles.previous} {particles.next} effect  {particles.play} play again  {particles.all} all at once");
    s.choice("Effect", &m_current, m_names, [this] { restart(); });
    s.text([this] { return m_names.empty() ? std::string() : m_library->description(m_names[static_cast<size_t>(m_current)]); });
    s.toggle("All at once", &m_all, [this] { restart(); });
    s.toggle("Bursts again by themselves", &m_repeat);
    s.slider("Every", &m_every, 0.5f, 6.0f, "%.1f s", {}, 0.5f).showIf([this] { return m_repeat; });
    s.slider("Size", &m_scale, 0.25f, 3.0f, "x%.2f", [this] { restart(); }, 0.25f);
    s.slider("Wind", &m_wind, -8.0f, 8.0f, "%.0f m/s", {}, 1.0f);
    s.button("Play it again", [this] { restart(); });
    s.separator();
    s.text([this] {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%zu particles of %zu, %zu emitters running", m_fx->count(), m_fx->capacity(), m_library->running());
        return std::string(buf);
    });
    s.text([this] { return "Your own effects from assets/effects: " + m_loaded; }).showIf([this] { return !m_loaded.empty(); });
    s.note("Every effect is a few lines of YAML (docs/PARTICLE_EFFECTS.md): copy one into assets/effects/my_effects.yaml beside the "
           "game, change it, and restart to see it here.");
}

} // namespace kke_particles
