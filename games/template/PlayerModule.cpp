#include "PlayerModule.h"

#include "kke/Application.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/ScriptModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <lua.h>

#include <algorithm>
#include <cmath>
#include <typeindex>

namespace starter {

namespace {

// One box as 24 vertices (flat-shaded faces), for the player's body.
void appendBox(const glm::vec3& center, const glm::vec3& half, const glm::vec3& color, std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx) {
    const glm::vec3 normals[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (const glm::vec3& n : normals) {
        const glm::vec3 u = std::abs(n.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        const glm::vec3 w = glm::cross(n, u);
        const uint32_t base = static_cast<uint32_t>(v.size());
        for (glm::vec2 k : { glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1) })
            v.push_back({ center + (n + u * k.x + w * k.y) * half, color, n, glm::vec2(0.0f) });
        // Counter-clockwise seen from outside.
        if (glm::dot(glm::cross(u, w), n) >= 0.0f) idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        else idx.insert(idx.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
    }
}

} // namespace

PlayerModule::PlayerModule() = default;
PlayerModule::~PlayerModule() = default;

std::vector<kke::ModuleDependency> PlayerModule::dependencies() const {
    return { { std::type_index(typeid(kke::RigidBodyModule)), true, "the character controller and collision" },
             { std::type_index(typeid(kke::InputModule)), true, "move, look and jump actions" },
             // Optional: when scripts exist, they get the `player` table.
             { std::type_index(typeid(kke::ScriptModule)), false, "the player.* Lua bindings" } };
}

void PlayerModule::init(kke::Application& app) {
    m_app = &app;
    m_rigid = app.getModule<kke::RigidBodyModule>();
    m_input = app.getModule<kke::InputModule>();

    // The standard character controls: WASD, mouse, Space, Shift, C, V
    // and the same on a controller. Players can rebind them.
    kke::InputMap& in = m_input->map(0);
    kke::InputModule::defineCharacterActions(in);
    in.defineAction({ "panels", "Developer panels", "Game", "game" });
    in.addBinding(kke::InputModule::bind("panels", kke::InputModule::key(SDL_SCANCODE_F1)));
    m_input->commitDefaults();

    kke::RigidWorld::CharacterDesc cd;
    cd.position = spawn;
    m_player = m_rigid->world().addCharacter(cd);
    m_loco = std::make_unique<kke::Locomotion>(m_rigid->world(), m_player);
    m_rig.mode = kke::CameraRig::Mode::ThirdPerson;
    m_rig.pitch = -12.0f;

    // The body: a torso and a darker "visor" on the front, so you can see
    // which way the character faces.
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    appendBox({ 0.0f, 0.9f, 0.0f }, { 0.28f, 0.9f, 0.2f }, { 0.2f, 0.45f, 0.9f }, v, idx);
    appendBox({ 0.0f, 1.55f, -0.2f }, { 0.2f, 0.08f, 0.03f }, { 0.1f, 0.1f, 0.15f }, v, idx);
    m_body = std::make_unique<kke::DynamicMeshRenderer>(app);
    m_body->upload(v, idx);

    app.window().setQuitOnEscape(false); // Esc releases the mouse instead
    registerLua();
}

// Your own Lua bindings: a table name, a function name and a C++ lambda.
// Arguments come off the Lua stack; push the results and return how many.
void PlayerModule::registerLua() {
    auto* scripts = m_app->getModule<kke::ScriptModule>();
    if (!scripts) return;
    kke::ScriptVM& vm = scripts->vm();
    vm.registerFunction("player", "position", [this](lua_State* L) {
        kke::ScriptVM::pushVec3(L, m_rigid->world().characterPosition(m_player));
        return 1;
    });
    vm.registerFunction("player", "teleport", [this](lua_State* L) {
        m_loco->teleport(kke::ScriptVM::toVec3(L, 1, spawn));
        return 0;
    });
    vm.registerFunction("player", "facing", [this](lua_State* L) {
        kke::ScriptVM::pushVec3(L, m_loco->facing());
        return 1;
    });
}

void PlayerModule::setCaptured(bool on) {
    m_captured = on;
    SDL_SetWindowRelativeMouseMode(m_app->window().handle(), on);
}

// Clicking the view grabs the mouse (so it can turn the camera); Esc lets
// it go.
void PlayerModule::onEvent(const SDL_Event& e) {
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !m_captured && !ImGui::GetIO().WantCaptureMouse && e.button.button == SDL_BUTTON_LEFT)
        setCaptured(true);
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.key == SDLK_ESCAPE) setCaptured(false);
}

void PlayerModule::update(const kke::UpdateContext& ctx) {
    const float dt = ctx.dt;
    kke::InputMap& in = m_input->map(0);
    kke::RigidWorld& world = m_rigid->world();

    // Look: the mouse while it's captured, a stick or gyro any time.
    if (m_captured) {
        const glm::vec2 look = in.axis2("look");
        m_rig.addLook(look.x * m_mouseSensitivity, look.y * m_mouseSensitivity);
    }
    const glm::vec2 rate = in.axis2("look.rate");
    m_rig.addLook(rate.x * m_stickSpeed * dt, rate.y * m_stickSpeed * 0.7f * dt);
    if (in.pressed("camera.toggle"))
        m_rig.mode = m_rig.mode == kke::CameraRig::Mode::ThirdPerson ? kke::CameraRig::Mode::FirstPerson : kke::CameraRig::Mode::ThirdPerson;
    if (in.pressed("jump")) m_jumpQueued = true;
    // F1: the engine's developer panels (scripts and their console, stats).
    if (in.pressed("panels")) m_app->debugUi().setVisible(!m_app->debugUi().visible());

    // Move relative to where the camera looks. What "jump" turns into
    // (a jump, a vault over a fence, a climb onto a ledge) is Locomotion's
    // call, from what it sees in front of the character.
    const glm::vec2 move = in.axis2("move");
    kke::Locomotion::Input li;
    li.move = m_rig.forward() * move.y + m_rig.right() * move.x;
    li.move.y = 0.0f;
    if (glm::length(li.move) > 1e-3f) li.move = glm::normalize(li.move) * std::min(1.0f, glm::length(move));
    li.fast = in.held("sprint");
    li.slow = in.held("walk");
    li.goUp = m_jumpQueued;
    m_jumpQueued = false;
    if (m_rig.mode == kke::CameraRig::Mode::FirstPerson) m_loco->setFacing(m_rig.forward());
    m_loco->update(li, dt);

    const glm::vec3 feet = world.characterPosition(m_player);
    if (feet.y < -20.0f) m_loco->teleport(spawn); // fell off the world

    // The camera never goes through walls: it asks Jolt what's in the way.
    m_rig.update(dt, feet, [&world](const glm::vec3& from, const glm::vec3& dir, float maxDist) {
        const auto hit = world.raycast(from, dir, maxDist);
        return hit.hit ? hit.distance : maxDist;
    }, m_app->camera());
}

void PlayerModule::render(const kke::RenderContext& ctx) {
    if (m_rig.mode == kke::CameraRig::Mode::FirstPerson) return;
    const glm::vec3 feet = m_rigid->world().characterPosition(m_player);
    const glm::mat4 t = glm::rotate(glm::translate(glm::mat4(1.0f), feet), glm::radians(-m_loco->facingYaw()), glm::vec3(0, 1, 0));
    m_body->draw(ctx, t, 0.0f, 0.6f);
}

void PlayerModule::renderShadow(const kke::ShadowRenderContext& ctx) {
    const glm::vec3 feet = m_rigid->world().characterPosition(m_player);
    m_body->drawShadow(ctx, glm::rotate(glm::translate(glm::mat4(1.0f), feet), glm::radians(-m_loco->facingYaw()), glm::vec3(0, 1, 0)));
}

} // namespace starter
