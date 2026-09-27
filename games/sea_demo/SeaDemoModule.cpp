#include "SeaDemoModule.h"

#include "kke/Application.h"
#include "kke/Mesh.h"
#include "kke/Picking.h"
#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/OrbitCameraModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace kke_sea {

namespace {

const SeaDemoModule::Kind kKinds[] = {
    { "Foam block (100 kg/m3)", 100.0f, { 0.35f, 0.25f, 0.35f }, { 0.95f, 0.93f, 0.85f } },
    { "Wooden crate (500)", 500.0f, { 0.4f, 0.4f, 0.4f }, { 0.62f, 0.42f, 0.22f } },
    { "Sealed barrel (650)", 650.0f, { 0.3f, 0.45f, 0.3f }, { 0.25f, 0.45f, 0.3f } },
    { "Ice block (917)", 917.0f, { 0.5f, 0.35f, 0.5f }, { 0.8f, 0.92f, 1.0f } },
    { "Iron block (7800)", 7800.0f, { 0.25f, 0.25f, 0.25f }, { 0.35f, 0.36f, 0.4f } },
};
constexpr int kKindCount = static_cast<int>(sizeof(kKinds) / sizeof(kKinds[0]));
constexpr size_t kMaxBodies = 40;     // budget: oldest thrown object goes first
constexpr size_t kMaxSpray = 1500;
const glm::vec3 kBoatHalf(1.6f, 0.35f, 0.6f);

float rnd(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s >> 8) * (1.0f / 16777216.0f); }

void addBox(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx, const glm::vec3& mn, const glm::vec3& mx, const glm::vec3& color,
            float bowTaper = 0.0f) {
    // 6 faces, 4 verts each. bowTaper > 0 pinches the +x end (a hull bow).
    auto P = [&](float x, float y, float z) {
        glm::vec3 p(x, y, z);
        if (bowTaper > 0.0f && x > 0.0f) {
            float t = x / mx.x;
            p.z *= 1.0f - bowTaper * t;
            if (y < 0.0f) p.y *= 1.0f - 0.5f * bowTaper * t;
        }
        return p;
    };
    const glm::vec3 c[8] = { P(mn.x, mn.y, mn.z), P(mx.x, mn.y, mn.z), P(mx.x, mx.y, mn.z), P(mn.x, mx.y, mn.z),
                             P(mn.x, mn.y, mx.z), P(mx.x, mn.y, mx.z), P(mx.x, mx.y, mx.z), P(mn.x, mx.y, mx.z) };
    const int f[6][4] = { { 0, 3, 2, 1 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 }, { 3, 7, 6, 2 }, { 0, 4, 7, 3 }, { 1, 2, 6, 5 } };
    for (const auto& q : f) {
        glm::vec3 n = glm::normalize(glm::cross(c[q[1]] - c[q[0]], c[q[2]] - c[q[0]]));
        uint32_t base = static_cast<uint32_t>(v.size());
        for (int k = 0; k < 4; ++k) v.push_back(kke::Vertex{ c[q[k]], color, n, { 0, 0 } });
        idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    }
}

} // namespace

void SeaDemoModule::init(kke::Application& app) {
    m_app = &app;
    defineInput();
    buildPanel();
    m_ocean = std::make_unique<kke::OceanRenderer>(app);
    m_spheres = std::make_unique<kke::SphereImpostorRenderer>(app);
    // Boat: tapered hull, deck, cabin, mast.
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    addBox(v, idx, -kBoatHalf, kBoatHalf, { 0.75f, 0.2f, 0.15f }, 0.85f);
    addBox(v, idx, { -1.5f, 0.35f, -0.52f }, { 1.2f, 0.4f, 0.52f }, { 0.72f, 0.55f, 0.35f }, 0.8f);
    addBox(v, idx, { -1.1f, 0.4f, -0.4f }, { -0.1f, 1.1f, 0.4f }, { 0.92f, 0.92f, 0.88f });
    addBox(v, idx, { 0.35f, 0.4f, -0.05f }, { 0.45f, 2.6f, 0.05f }, { 0.4f, 0.3f, 0.2f });
    m_boatMesh = std::make_unique<kke::DynamicMeshRenderer>(app);
    m_boatMesh->upload(v, idx);
    for (const Kind& k : kKinds) {
        std::vector<kke::Vertex> cv;
        std::vector<uint32_t> ci;
        addBox(cv, ci, -k.halfExtents, k.halfExtents, k.color);
        auto mesh = std::make_unique<kke::DynamicMeshRenderer>(app);
        mesh->upload(cv, ci);
        m_kindMeshes.push_back(std::move(mesh));
    }
    reset();
}

void SeaDemoModule::reset() {
    m_waves.setWind(m_windSpeed, m_windDir, m_chop);
    m_bodies.clear();
    m_kindOf.clear();
    m_spray.clear();
    // Boat: hull + air inside = low average density; drag tuned so it
    // glides forward but resists going sideways (a keel, the cheap way).
    m_boat = m_bodies.add(kBoatHalf, 280.0f, { 0.0f, 0.2f, 0.0f });
    m_bodies.bodies()[m_boat].linearDrag = 0.8f;
    m_bodies.bodies()[m_boat].centerOfMassOffset = glm::vec3(0.0f, -0.3f, 0.0f); // ballast keel: stays upright
    m_kindOf.push_back(-1);
    for (int i = 0; i < kKindCount; ++i) {
        size_t b = m_bodies.add(kKinds[i].halfExtents, kKinds[i].density, { 4.0f + i * 1.8f, 1.5f, 3.0f });
        (void)b;
        m_kindOf.push_back(i);
    }
}

void SeaDemoModule::throwObject(int kind, bool atMouse) {
    const kke::Camera& cam = m_app->camera();
    const auto& mouse = m_app->window().mouseState();
    int w = 1, h = 1;
    SDL_GetWindowSize(m_app->window().handle(), &w, &h);
    glm::mat4 view = glm::lookAt(cam.position, cam.target, cam.up);
    glm::mat4 proj = kke::engineProjection(cam.fovDegrees, float(w) / float(std::max(h, 1)), cam.nearPlane, cam.farPlane);
    // The mouse throws where it points; a controller throws at the middle
    // of the screen (where the camera looks: at the boat when it follows).
    const glm::vec2 at = atMouse ? glm::vec2(mouse.x, mouse.y) : glm::vec2(float(w) * 0.5f, float(h) * 0.5f);
    kke::Ray ray = kke::screenToRay(at, { float(w), float(h) }, view, proj);
    // Budget: drop the oldest thrown object (never the boat).
    size_t alive = 0;
    for (size_t i = 0; i < m_bodies.bodies().size(); ++i) alive += m_bodies.bodies()[i].alive;
    if (alive >= kMaxBodies) {
        for (size_t i = 0; i < m_bodies.bodies().size(); ++i) {
            if (i != m_boat && m_bodies.bodies()[i].alive) { m_bodies.remove(i); break; }
        }
    }
    const Kind& k = kKinds[kind];
    size_t b = m_bodies.add(k.halfExtents, k.density, ray.origin + ray.direction * 2.0f,
                            glm::angleAxis(rnd(m_rng) * 6.28f, glm::normalize(glm::vec3(rnd(m_rng), 1.0f, rnd(m_rng)))));
    m_bodies.bodies()[b].velocity = ray.direction * 12.0f + glm::vec3(0, 2.0f, 0);
    m_bodies.bodies()[b].angularVelocity = glm::vec3(rnd(m_rng) - 0.5f, rnd(m_rng) - 0.5f, rnd(m_rng) - 0.5f) * 4.0f;
    m_kindOf.push_back(kind);
}

void SeaDemoModule::splash(const glm::vec3& at, float strength) {
    int n = std::min(200, static_cast<int>(strength * 18.0f));
    for (int i = 0; i < n && m_spray.size() < kMaxSpray; ++i) {
        float a = rnd(m_rng) * 6.2831853f, r = 0.3f + rnd(m_rng) * 0.5f;
        glm::vec3 dir(std::cos(a) * r, 1.0f, std::sin(a) * r);
        m_spray.push_back({ at + glm::vec3(std::cos(a), 0, std::sin(a)) * 0.3f, dir * (1.5f + rnd(m_rng) * strength * 0.8f), 1.5f });
    }
}

void SeaDemoModule::fixedUpdate(const kke::FixedUpdateContext& ctx) {
    const float dt = ctx.fixedDt;
    m_time += dt;
    // Boat controls: thrust at the stern, below the waterline; the rudder
    // is a sideways force at the stern that scales with speed (no speed,
    // no steering, like a real boat).
    // Actions (defineInput): arrows or the triggers for the throttle, arrows
    // or the left stick for the rudder. Reverse is half as strong.
    float targetThrottle = 0.0f, targetRudder = 0.0f;
    if (auto* in = m_app->getModule<kke::InputModule>()) {
        const float t = in->map(0).axis("sea.throttle");
        targetThrottle = t < 0.0f ? t * 0.5f : t;
        targetRudder = -in->map(0).axis("sea.rudder");
    }
    m_throttle += (targetThrottle - m_throttle) * std::min(1.0f, dt * 2.0f);
    m_rudder += (targetRudder - m_rudder) * std::min(1.0f, dt * 4.0f);
    kke::FloatingBody& boat = m_bodies.bodies()[m_boat];
    const glm::mat3 R = glm::mat3_cast(boat.orientation);
    const glm::vec3 fwd = R * glm::vec3(1, 0, 0), side = R * glm::vec3(0, 0, 1);
    const glm::vec3 stern = boat.position + R * glm::vec3(-kBoatHalf.x, -0.2f, 0.0f);
    if (boat.submerged > 0.1f) {
        m_bodies.applyForce(m_boat, fwd * (m_throttle * boat.mass * 3.0f), stern);
        float speed = glm::dot(boat.velocity, fwd);
        m_bodies.applyForce(m_boat, side * (m_rudder * boat.mass * 0.8f * std::clamp(speed, -2.0f, 4.0f)), stern);
        // Keel: water resists sideways motion far more than forward motion.
        m_bodies.applyForce(m_boat, -side * (glm::dot(boat.velocity, side) * boat.mass * 2.0f), boat.position);
        if (m_throttle > 0.2f && m_spray.size() < kMaxSpray && rnd(m_rng) < m_throttle * 0.8f)
            m_spray.push_back({ stern, -fwd * 2.0f + glm::vec3(rnd(m_rng) - 0.5f, 1.2f, rnd(m_rng) - 0.5f), 1.0f });
    }
    m_bodies.step(dt, m_waves, m_time);
    for (const kke::FloatingBody& b : m_bodies.bodies())
        if (b.alive && b.impactSpeed > 1.5f) splash(b.position, b.impactSpeed);
    // Spray: ballistic, gone when it falls back into the sea.
    for (Spray& s : m_spray) {
        s.vel.y -= 9.81f * dt;
        s.pos += s.vel * dt;
        s.life -= dt;
    }
    m_spray.erase(std::remove_if(m_spray.begin(), m_spray.end(),
                                 [&](const Spray& s) { return s.life <= 0.0f || (s.vel.y < 0.0f && s.pos.y < m_waves.height({ s.pos.x, s.pos.z }, m_time)); }),
                  m_spray.end());
}

void SeaDemoModule::update(const kke::UpdateContext&) {
    if (auto* in = m_app->getModule<kke::InputModule>()) {
        const kke::InputMap& m = in->map(0);
        if (m.pressed("sea.throw")) throwObject(m_kind, false);
        if (m.pressed("sea.next")) m_kind = (m_kind + 1) % kKindCount;
        for (int k = 0; k < kKindCount; ++k)
            if (m.pressed("sea.kind" + std::to_string(k + 1))) m_kind = k;
        if (m.pressed("sea.follow")) m_followBoat = !m_followBoat;
        if (m.pressed("sea.reset")) reset();
    }
    if (m_followBoat) {
        glm::vec3 target = m_bodies.bodies()[m_boat].position + glm::vec3(0, 0.8f, 0);
        glm::vec3& camTarget = m_app->camera().target;
        camTarget += (target - camTarget) * 0.1f;
    }
}

void SeaDemoModule::render(const kke::RenderContext& ctx) {
    m_ocean->drawOcean(ctx, m_waves, m_time, ctx.cameraPos);
    const auto& bodies = m_bodies.bodies();
    for (size_t i = 0; i < bodies.size(); ++i) {
        if (!bodies[i].alive) continue;
        int kind = m_kindOf[i];
        if (kind < 0) m_boatMesh->draw(ctx, bodies[i].transform(), 0.0f, 0.5f);
        else m_kindMeshes[kind]->draw(ctx, bodies[i].transform(), kind == 4 ? 0.8f : 0.0f, kind == 3 ? 0.15f : 0.6f);
    }
    m_sphereScratch.clear();
    for (const Spray& s : m_spray) m_sphereScratch.push_back({ s.pos, 0.06f, glm::vec3(0.9f, 0.95f, 1.0f), 0.0f, 0.2f });
    m_spheres->draw(ctx, m_sphereScratch);
}

void SeaDemoModule::renderShadow(const kke::ShadowRenderContext& ctx) {
    const auto& bodies = m_bodies.bodies();
    for (size_t i = 0; i < bodies.size(); ++i) {
        if (!bodies[i].alive) continue;
        if (m_kindOf[i] < 0) m_boatMesh->drawShadow(ctx, bodies[i].transform());
        else m_kindMeshes[m_kindOf[i]]->drawShadow(ctx, bodies[i].transform());
    }
}

void SeaDemoModule::onEvent(const SDL_Event& event) {
    // The mouse throws where it points (the pad throws with sea.throw).
    // Not while it's over a panel: the RmlUi settings or an F1 ImGui one.
    const bool overUi = ImGui::GetIO().WantCaptureMouse || m_app->uiCapturesMouse();
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT && !overUi) {
        throwObject(m_kind, true);
        return;
    }
}

void SeaDemoModule::defineInput() {
    auto* in = m_app->getModule<kke::InputModule>();
    if (!in) return;
    using IM = kke::InputModule;
    kke::InputMap& m = in->map(0);
    m.defineAction({ "sea.throttle", "Throttle / reverse", "Boat", "game", kke::ActionType::Axis1D });
    m.defineAction({ "sea.rudder", "Rudder", "Boat", "game", kke::ActionType::Axis1D });
    m.defineAction({ "sea.throw", "Throw", "Sea" });
    m.defineAction({ "sea.next", "Next thing to throw", "Sea" });
    m.defineAction({ "sea.follow", "Camera follows the boat", "Sea" });
    m.defineAction({ "sea.reset", "Reset", "Sea" });
    auto axis = [&](const char* action, kke::InputSource src, float scale) {
        kke::Binding b = IM::bind(action, src, kke::Trigger::Continuous);
        b.scale = scale;
        b.deadzone = src.kind == kke::SourceKind::GamepadAxis ? 0.15f : 0.0f;
        m.addBinding(b);
    };
    axis("sea.throttle", IM::key(SDL_SCANCODE_UP), 1.0f);
    axis("sea.throttle", IM::key(SDL_SCANCODE_DOWN), -1.0f);
    axis("sea.throttle", IM::padAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), 1.0f);
    axis("sea.throttle", IM::padAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), -1.0f);
    axis("sea.rudder", IM::key(SDL_SCANCODE_RIGHT), 1.0f);
    axis("sea.rudder", IM::key(SDL_SCANCODE_LEFT), -1.0f);
    axis("sea.rudder", IM::padAxis(SDL_GAMEPAD_AXIS_LEFTX), 1.0f);
    m.addBinding(IM::bind("sea.throw", IM::pad(SDL_GAMEPAD_BUTTON_SOUTH)));
    m.addBinding(IM::bind("sea.next", IM::key(SDL_SCANCODE_T)));
    m.addBinding(IM::bind("sea.next", IM::pad(SDL_GAMEPAD_BUTTON_NORTH)));
    m.addBinding(IM::bind("sea.follow", IM::key(SDL_SCANCODE_C)));
    m.addBinding(IM::bind("sea.follow", IM::pad(SDL_GAMEPAD_BUTTON_RIGHT_STICK)));
    m.addBinding(IM::bind("sea.reset", IM::key(SDL_SCANCODE_R)));
    m.addBinding(IM::bind("sea.reset", IM::pad(SDL_GAMEPAD_BUTTON_WEST)));
    // Keys 1-5 pick what to throw (keyboard shortcuts, rebindable; a
    // controller steps through them with sea.next).
    for (int k = 0; k < kKindCount; ++k) {
        const std::string id = "sea.kind" + std::to_string(k + 1);
        m.defineAction({ id, kKinds[k].name, "Sea" });
        m.addBinding(IM::bind(id, IM::key(static_cast<SDL_Scancode>(SDL_SCANCODE_1 + k))));
    }
    in->commitDefaults();
}

// The settings (RmlUi, kke::DemoPanelModule): the same rows work with a
// controller (View opens them), the keyboard (F3) and the mouse.
void SeaDemoModule::buildPanel() {
    auto* panel = m_app->getModule<kke::DemoPanelModule>();
    if (!panel) return;
    auto& s = panel->section("Sea");
    s.hint("{sea.throttle} throttle  {sea.rudder} steer  {mouse:left} throw where you point  1-5 or {sea.next} what  {sea.follow} follow  "
           "{sea.reset} reset",
           "{sea.throttle} throttle  {sea.rudder} steer  {sea.throw} throw  {sea.next} what  {sea.follow} follow  {sea.reset} reset  "
           "{camera.orbit} look  {camera.zoom} zoom");
    auto wind = [this] { m_waves.setWind(m_windSpeed, m_windDir, m_chop); };
    s.heading("Waves");
    s.slider("Wind", &m_windSpeed, 0.0f, 14.0f, "%.1f m/s", wind, 0.5f);
    m_windDirDeg = glm::degrees(m_windDir);
    s.slider("Wind direction", &m_windDirDeg, -180.0f, 180.0f, "%.0f deg", [this, wind] { m_windDir = glm::radians(m_windDirDeg); wind(); }, 5.0f);
    s.slider("Choppiness", &m_chop, 0.0f, 0.95f, "%.2f", wind, 0.05f);
    s.heading("Throw");
    std::vector<std::string> kinds;
    for (const Kind& k : kKinds) kinds.push_back(k.name);
    s.choice("What", &m_kind, kinds);
    s.toggle("Camera follows the boat", &m_followBoat);
    s.button("Reset", [this] { reset(); });
    s.text([this] {
        size_t alive = 0;
        for (const auto& b : m_bodies.bodies()) alive += b.alive;
        const kke::FloatingBody& boat = m_bodies.bodies()[m_boat];
        char buf[160];
        std::snprintf(buf, sizeof(buf), "Boat %.1f m/s, %.0f%% under water. %zu floating (max %zu), %zu spray drops",
                      static_cast<double>(glm::length(boat.velocity)), static_cast<double>(boat.submerged * 100.0f), alive, kMaxBodies,
                      m_spray.size());
        return std::string(buf);
    });
    s.note("Water is 1025 kg/m3: lighter things float, heavier ones sink. Waves are Gerstner swell.");
}

} // namespace kke_sea
