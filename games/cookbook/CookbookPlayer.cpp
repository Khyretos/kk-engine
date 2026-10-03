#include "CookbookPlayer.h"

#include "Bindings.h"
#include "Procedural.h"

#include "kke/Application.h"
#include "kke/DevTools.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/ScriptModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <lua.h>
#include <lauxlib.h>

#include <algorithm>
#include <cmath>
#include <typeindex>

namespace cookbook {

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
        if (glm::dot(glm::cross(u, w), n) >= 0.0f) idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        else idx.insert(idx.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
    }
}

constexpr const char* kViewNames[] = { "first", "third", "orbit", "topdown", "iso", "side", "fixed", "cinematic" };

} // namespace

const char* CookbookPlayer::viewName(View v) { return kViewNames[static_cast<int>(v)]; }

bool CookbookPlayer::viewFromName(const std::string& name, View& out) {
    for (int i = 0; i < static_cast<int>(View::Count); ++i)
        if (name == kViewNames[i]) {
            out = static_cast<View>(i);
            return true;
        }
    return false;
}

CookbookPlayer::CookbookPlayer() = default;
CookbookPlayer::~CookbookPlayer() = default;

std::vector<kke::ModuleDependency> CookbookPlayer::dependencies() const {
    return { { std::type_index(typeid(kke::RigidBodyModule)), true, "the character controller and collision" },
             { std::type_index(typeid(kke::InputModule)), true, "move, look and camera actions" },
             { std::type_index(typeid(kke::ScriptModule)), false, "the view.* and player.* Lua bindings" } };
}

void CookbookPlayer::init(kke::Application& app) {
    m_app = &app;
    m_rigid = app.getModule<kke::RigidBodyModule>();
    m_input = app.getModule<kke::InputModule>();

    kke::InputMap& in = m_input->map(0);
    kke::InputModule::defineCharacterActions(in);
    addCookbookBindings(in); // Bindings.h
    in.defineAction({ "panels", "Developer panels", "Game", "game" });
    in.addBinding(kke::InputModule::bind("panels", kke::InputModule::key(SDL_SCANCODE_F1)));
    m_input->commitDefaults();

    kke::RigidWorld::CharacterDesc cd;
    cd.position = spawn;
    m_player = m_rigid->world().addCharacter(cd);
    m_loco = std::make_unique<kke::Locomotion>(m_rigid->world(), m_player);
    m_rig.pitch = -12.0f;

    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    appendBox({ 0.0f, 0.9f, 0.0f }, { 0.28f, 0.9f, 0.2f }, { 0.2f, 0.45f, 0.9f }, v, idx);
    appendBox({ 0.0f, 1.55f, -0.2f }, { 0.2f, 0.08f, 0.03f }, { 0.1f, 0.1f, 0.15f }, v, idx);
    m_body = std::make_unique<kke::DynamicMeshRenderer>(app);
    m_body->upload(v, idx);

    // A slow tour of the level, for the "cinematic" camera until a script
    // gives it another path (view.path).
    m_rig.setCinematic({ { { -14.0f, 6.0f, 14.0f }, { 0.0f, 1.0f, 0.0f }, 0.0f },
                         { { 14.0f, 4.0f, 12.0f }, { 0.0f, 1.0f, -4.0f }, 6.0f },
                         { { 12.0f, 8.0f, -14.0f }, { -4.0f, 0.0f, 0.0f }, 12.0f },
                         { { -14.0f, 6.0f, 14.0f }, { 0.0f, 1.0f, 0.0f }, 18.0f } },
                       true);

    // KKE_COOKBOOK_VIEW=topdown: start with that camera (screenshots).
    View start = View::Third;
    if (const char* v0 = kke::dev::env("KKE_COOKBOOK_VIEW"); v0 && *v0) viewFromName(v0, start);
    setView(start);

    app.window().setQuitOnEscape(false); // Esc releases the mouse instead
    registerLua();
}

void CookbookPlayer::setView(View v) {
    m_view = v;
    // Four of the eight are kke::CameraRig's own modes; the rest are a
    // camera placed by hand in update().
    switch (v) {
    case View::First: m_rig.mode = kke::CameraRig::Mode::FirstPerson; break;
    case View::Orbit: m_rig.mode = kke::CameraRig::Mode::Orbit; break;
    case View::Cinematic: m_rig.mode = kke::CameraRig::Mode::Cinematic; break;
    default: m_rig.mode = kke::CameraRig::Mode::ThirdPerson; break;
    }
}

void CookbookPlayer::shake(float amount) { m_trauma = std::clamp(m_trauma + amount, 0.0f, 1.0f); }

// --8<-- [start:lua]
// Lua bindings: a table name, a function name and a C++ lambda. Arguments
// come off the Lua stack; push the results and return how many.
void CookbookPlayer::registerLua() {
    auto* scripts = m_app->getModule<kke::ScriptModule>();
    if (!scripts) return;
    kke::ScriptVM& vm = scripts->vm();
    // view.mode("topdown") switches; view.mode() says which one is on.
    vm.registerFunction("view", "mode", [this](lua_State* L) {
        if (lua_isstring(L, 1)) {
            View v = View::Third;
            if (!viewFromName(lua_tostring(L, 1), v))
                return luaL_error(L, "view.mode: no camera '%s' (first, third, orbit, topdown, iso, side, fixed, cinematic)", lua_tostring(L, 1));
            setView(v);
        }
        lua_pushstring(L, viewName(m_view));
        return 1;
    });
    vm.registerFunction("view", "shake", [this](lua_State* L) {
        shake(static_cast<float>(luaL_optnumber(L, 1, 0.5)));
        return 0;
    });
    // view.path({ {pos = Vec(..), target = Vec(..), time = 0}, ... }, loop)
    vm.registerFunction("view", "path", [this](lua_State* L) {
        luaL_checktype(L, 1, LUA_TTABLE);
        std::vector<kke::CameraRig::Keyframe> keys;
        const lua_Integer n = luaL_len(L, 1);
        for (lua_Integer i = 1; i <= n; ++i) {
            lua_geti(L, 1, i);
            const int k = lua_gettop(L);
            keys.push_back({ kke::ScriptVM::fieldVec3(L, k, "pos", glm::vec3(0.0f)), kke::ScriptVM::fieldVec3(L, k, "target", glm::vec3(0.0f)),
                             kke::ScriptVM::fieldNumber(L, k, "time", static_cast<float>(i - 1)) });
            lua_pop(L, 1);
        }
        if (keys.size() < 2) return luaL_error(L, "view.path: needs at least two keyframes");
        m_rig.setCinematic(std::move(keys), lua_toboolean(L, 2) != 0);
        setView(View::Cinematic);
        return 0;
    });
    // The same player.* as the starter template, so recipes run in both.
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
// --8<-- [end:lua]

void CookbookPlayer::setCaptured(bool on) {
    m_captured = on;
    SDL_SetWindowRelativeMouseMode(m_app->window().handle(), on);
}

void CookbookPlayer::onEvent(const SDL_Event& e) {
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !m_captured && !ImGui::GetIO().WantCaptureMouse && !m_app->uiCapturesMouse() && e.button.button == SDL_BUTTON_LEFT)
        setCaptured(true);
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.key == SDLK_ESCAPE) setCaptured(false);
}

// --8<-- [start:forward]
glm::vec3 CookbookPlayer::moveForward() const {
    switch (m_view) {
    case View::TopDown: return { 0.0f, 0.0f, -1.0f };                               // up on the screen
    case View::Iso: return glm::normalize(glm::vec3(-1.0f, 0.0f, -1.0f));          // away from the camera
    case View::Side: return { 0.0f, 0.0f, 0.0f };                                   // only left and right
    case View::Fixed: {                                                             // away from the wall camera
        glm::vec3 f = m_rigid->world().characterPosition(m_player) - fixedCameraAt;
        f.y = 0.0f;
        return glm::length(f) > 1e-3f ? glm::normalize(f) : glm::vec3(0, 0, -1);
    }
    default: return m_rig.forward();
    }
}
// --8<-- [end:forward]

void CookbookPlayer::update(const kke::UpdateContext& ctx) {
    const float dt = ctx.dt;
    m_time += dt;
    kke::InputMap& in = m_input->map(0);
    kke::RigidWorld& world = m_rigid->world();

    if (m_captured) {
        const glm::vec2 look = in.axis2("look");
        m_rig.addLook(look.x * m_mouseSensitivity, look.y * m_mouseSensitivity);
    }
    const glm::vec2 rate = in.axis2("look.rate");
    m_rig.addLook(rate.x * m_stickSpeed * dt, rate.y * m_stickSpeed * 0.7f * dt);
    if (in.pressed("camera.next")) setView(static_cast<View>((static_cast<int>(m_view) + 1) % static_cast<int>(View::Count)));
    if (in.pressed("camera.toggle")) setView(m_view == View::First ? View::Third : View::First);
    if (in.pressed("jump")) m_jumpQueued = true;
    if (in.pressed("panels")) m_app->debugUi().setVisible(!m_app->debugUi().visible());
    m_rig.settings.armLength = std::clamp(m_rig.settings.armLength - in.axis("zoom") * 4.0f * dt, 1.5f, 8.0f);

    // Stick "forward" means whatever is forward for this camera.
    const glm::vec2 move = in.axis2("move");
    const glm::vec3 fwd = moveForward();
    const glm::vec3 right = m_view == View::Side ? glm::vec3(1, 0, 0) : glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
    kke::Locomotion::Input li;
    li.move = fwd * move.y + right * move.x;
    li.move.y = 0.0f;
    if (glm::length(li.move) > 1e-3f) li.move = glm::normalize(li.move) * std::min(1.0f, glm::length(move));
    li.fast = in.held("sprint");
    li.slow = in.held("walk");
    li.goUp = m_jumpQueued;
    m_jumpQueued = false;
    if (m_view == View::First) m_loco->setFacing(m_rig.forward());
    m_loco->update(li, dt);

    if (world.characterPosition(m_player).y < -20.0f) m_loco->teleport(spawn);
    // Cameras follow the feet where they're drawn: between the last two
    // physics steps (the raw position jumps at 60 Hz and shakes the view).
    const glm::vec3 feet = world.characterDrawPosition(m_player, m_app->fixedAlpha());

    kke::Camera& cam = m_app->camera();
    auto ray = [&world](const glm::vec3& from, const glm::vec3& dir, float maxDist) {
        const auto hit = world.raycast(from, dir, maxDist);
        return hit.hit ? hit.distance : maxDist;
    };
    // --8<-- [start:cameras]
    const glm::vec3 chest = feet + glm::vec3(0.0f, 1.2f, 0.0f);
    switch (m_view) {
    case View::First:
    case View::Third:
    case View::Orbit:
    case View::Cinematic:
        // kke::CameraRig does these four: eyes, a spring arm that never
        // goes through walls, an orbit, and a smooth keyframed path.
        m_rig.update(dt, feet, ray, cam);
        break;
    case View::TopDown:
        // Straight down, a little tilted so walls still read as walls.
        cam.position = feet + glm::vec3(0.0f, 11.0f, 2.0f);
        cam.target = feet;
        break;
    case View::Iso:
        // High, far and at 45 degrees, with a narrow lens: almost no
        // perspective, the look of isometric games.
        cam.position = feet + glm::vec3(9.0f, 11.0f, 9.0f);
        cam.target = feet;
        cam.fovDegrees = 30.0f;
        break;
    case View::Side:
        // Beside the level, looking along -Z; the stick only goes left
        // and right (moveForward() is zero), like a platformer.
        cam.position = glm::vec3(feet.x, chest.y + 1.0f, feet.z + 12.0f);
        cam.target = glm::vec3(feet.x, chest.y, feet.z);
        break;
    case View::Fixed:
        // Bolted to the wall, turning to watch the player.
        cam.position = fixedCameraAt;
        cam.target = chest;
        break;
    case View::Count: break;
    }
    if (m_view == View::TopDown || m_view == View::Side || m_view == View::Fixed) cam.fovDegrees = 50.0f;
    // --8<-- [end:cameras]

    // --8<-- [start:shake]
    // Shake on top of whichever camera: turn the view by a small, smooth
    // wobble that fades as the trauma does (Procedural.h).
    if (m_trauma > 0.0f) {
        const glm::vec3 wobble = shakeOffset(m_trauma, m_time);
        const glm::vec3 look = cam.target - cam.position;
        const glm::vec3 side = glm::normalize(glm::cross(look, glm::vec3(0, 1, 0)));
        glm::mat4 turn = glm::rotate(glm::mat4(1.0f), glm::radians(wobble.x), glm::vec3(0, 1, 0));
        turn = glm::rotate(turn, glm::radians(wobble.y), side);
        cam.target = cam.position + glm::vec3(turn * glm::vec4(look, 0.0f));
        cam.up = glm::vec3(glm::rotate(glm::mat4(1.0f), glm::radians(wobble.z), glm::normalize(look)) * glm::vec4(0, 1, 0, 0));
        m_trauma = std::max(0.0f, m_trauma - 0.9f * dt); // gone in about a second
    } else {
        cam.up = glm::vec3(0, 1, 0);
    }
    // --8<-- [end:shake]
}

void CookbookPlayer::render(const kke::RenderContext& ctx) {
    if (m_view == View::First) return;
    const glm::vec3 feet = m_rigid->world().characterDrawPosition(m_player, m_app->fixedAlpha());
    const glm::mat4 t = glm::rotate(glm::translate(glm::mat4(1.0f), feet), glm::radians(-m_loco->facingYaw()), glm::vec3(0, 1, 0));
    m_body->draw(ctx, t, 0.0f, 0.6f);
}

void CookbookPlayer::renderShadow(const kke::ShadowRenderContext& ctx) {
    const glm::vec3 feet = m_rigid->world().characterDrawPosition(m_player, m_app->fixedAlpha());
    m_body->drawShadow(ctx, glm::rotate(glm::translate(glm::mat4(1.0f), feet), glm::radians(-m_loco->facingYaw()), glm::vec3(0, 1, 0)));
}

} // namespace cookbook
