// camera.*: what the player sees, and cutscenes (kke/CameraDirector.h).
#include "kke/modules/ScriptModule.h"

#include <lauxlib.h>
#include <lua.h>

#include <cstdio>
#include <string>
#include <vector>

namespace kke {

namespace {
constexpr size_t kMaxShots = 256;

// {pos=..., look=..., fov=..., t=...}; `timed` is set when it has a t.
CameraDirector::Shot readShot(lua_State* L, int table, bool& timed) {
    CameraDirector::Shot s;
    s.position = ScriptVM::fieldVec3(L, table, "pos", glm::vec3(0.0f));
    s.target = ScriptVM::fieldVec3(L, table, "look", s.position + glm::vec3(0.0f, 0.0f, -1.0f));
    s.fovDegrees = ScriptVM::fieldNumber(L, table, "fov", 0.0f);
    lua_getfield(L, table, "t");
    timed = lua_isnumber(L, -1) != 0;
    s.time = timed ? static_cast<float>(lua_tonumber(L, -1)) : 0.0f;
    lua_pop(L, 1);
    if (s.fovDegrees != 0.0f && (s.fovDegrees < 5.0f || s.fovDegrees > 150.0f)) luaL_error(L, "camera: fov %f is outside 5..150 degrees", s.fovDegrees);
    return s;
}
} // namespace

const Camera& ScriptModule::shownCamera() const { return m_cameraShown ? m_shownCamera : m_app->camera(); }

void ScriptModule::bindCamera() {
    ScriptVM& vm = *m_vm;
    m_app->setCameraDirector([this](Camera& camera, float dt) {
        const CameraDirector::Result r = m_director.update(dt, camera);
        if (r.finished) m_cameraDone = true; // the CameraDone hook runs at the next update, before Think
        m_cameraShown = r.changed;
        m_shownCamera = camera;
        return r.changed;
    });

    // What the player saw last frame: during a cutscene, the cutscene's shot.
    vm.registerFunction("camera", "position", [this](lua_State* L) { ScriptVM::pushVec3(L, shownCamera().position); return 1; });
    vm.registerFunction("camera", "target", [this](lua_State* L) { ScriptVM::pushVec3(L, shownCamera().target); return 1; });
    vm.registerFunction("camera", "forward", [this](lua_State* L) {
        const glm::vec3 f = shownCamera().target - shownCamera().position;
        ScriptVM::pushVec3(L, glm::length(f) > 1e-6f ? glm::normalize(f) : glm::vec3(0, 0, -1));
        return 1;
    });
    vm.registerFunction("camera", "fov", [this](lua_State* L) { lua_pushnumber(L, shownCamera().fovDegrees); return 1; });

    // camera.path({ {pos=, look=, fov=, t=}, ... }, {loop, hold, blend, blendOut, seconds}) -> seconds long
    vm.registerFunction("camera", "path", [this](lua_State* L) {
        luaL_checktype(L, 1, LUA_TTABLE);
        const bool hasOptions = lua_istable(L, 2);
        const size_t n = lua_rawlen(L, 1);
        if (n == 0) luaL_error(L, "camera.path: no shots (a list of {pos=..., look=...})");
        if (n > kMaxShots) luaL_error(L, "camera.path: %d shots, at most %d", static_cast<int>(n), static_cast<int>(kMaxShots));
        std::vector<CameraDirector::Shot> shots;
        shots.reserve(n);
        size_t timed = 0;
        for (size_t i = 1; i <= n; ++i) {
            lua_rawgeti(L, 1, static_cast<lua_Integer>(i));
            if (!lua_istable(L, -1)) luaL_error(L, "camera.path: shot %d is not a table", static_cast<int>(i));
            bool t = false;
            shots.push_back(readShot(L, lua_gettop(L), t));
            timed += t ? 1 : 0;
            lua_pop(L, 1);
        }
        if (timed != 0 && timed != n) luaL_error(L, "camera.path: give every shot a t, or none (then they are spread over `seconds`)");
        CameraDirector::PathOptions o;
        float seconds = 2.0f * static_cast<float>(n - 1);
        if (hasOptions) {
            o.loop = ScriptVM::fieldBool(L, 2, "loop", false);
            o.hold = ScriptVM::fieldBool(L, 2, "hold", false);
            o.blendIn = ScriptVM::fieldNumber(L, 2, "blend", o.blendIn);
            o.blendOut = ScriptVM::fieldNumber(L, 2, "blendOut", o.blendIn);
            seconds = ScriptVM::fieldNumber(L, 2, "seconds", seconds);
        }
        if (timed == 0 && n > 1) {
            if (seconds <= 0.0f) luaL_error(L, "camera.path: seconds must be more than 0");
            for (size_t i = 0; i < n; ++i) shots[i].time = seconds * static_cast<float>(i) / static_cast<float>(n - 1);
        }
        m_director.play(std::move(shots), o);
        m_directorOwner = m_vm->currentSource();
        lua_pushnumber(L, m_director.duration());
        return 1;
    });
    // camera.hold(pos, look [, {fov=, blend=}]): one shot until camera.release.
    vm.registerFunction("camera", "hold", [this](lua_State* L) {
        CameraDirector::Shot s;
        s.position = ScriptVM::toVec3(L, 1);
        s.target = ScriptVM::toVec3(L, 2, s.position + glm::vec3(0.0f, 0.0f, -1.0f));
        float blend = 0.5f;
        if (lua_istable(L, 3)) {
            s.fovDegrees = ScriptVM::fieldNumber(L, 3, "fov", 0.0f);
            blend = ScriptVM::fieldNumber(L, 3, "blend", blend);
            if (s.fovDegrees != 0.0f && (s.fovDegrees < 5.0f || s.fovDegrees > 150.0f)) luaL_error(L, "camera.hold: fov %f is outside 5..150 degrees", s.fovDegrees);
        }
        m_director.hold(s, blend);
        m_directorOwner = m_vm->currentSource();
        return 0;
    });
    vm.registerFunction("camera", "release", [this](lua_State* L) {
        m_director.release(static_cast<float>(luaL_optnumber(L, 1, 0.5)));
        return 0;
    });
    vm.registerFunction("camera", "directing", [this](lua_State* L) { lua_pushboolean(L, m_director.directing()); return 1; });
    vm.registerFunction("camera", "shake", [this](lua_State* L) {
        const float strength = static_cast<float>(luaL_checknumber(L, 1)), seconds = static_cast<float>(luaL_optnumber(L, 2, 0.4));
        if (!m_app->reduceMotion()) m_director.shake(strength, seconds); // accessibility: reduced motion keeps the camera still
        return 0;
    });
}

} // namespace kke
