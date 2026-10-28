#pragma once
// ScriptWorld: the world.* Lua API over kke::WorldStreamer (docs/SCENES.md "Streaming").
#include "kke/WorldStream.h"

#include <glm/glm.hpp>

#include <memory>
#include <set>
#include <string>

namespace kke {

class ScriptVM;

// A streamed world a script describes: the cells, and two functions that load
// and unload one (spawn its bodies, models and scenes, take them away).
// `load(cell)` answers true (or nothing) when the cell is in, "pending" when it
// will be (then world.done(name)), or false and a reason. The camera can be
// the focus (followCamera) or the script sets foci by hand.
class ScriptWorld {
public:
    ScriptWorld();
    ~ScriptWorld();
    void bind(ScriptVM& vm);
    // Advances the streamer and runs the hooks (CellLoaded, CellUnloaded, CellFailed).
    void update(float dt, ScriptVM& vm, const glm::vec3& cameraPosition);
    // The script that set the world up is gone: it is forgotten (without calling its functions).
    void release(const std::string& source);

    WorldStreamer& streamer() { return m_streamer; }
    bool active() const { return m_active; }

private:
    class Backend;
    WorldStreamer m_streamer;
    std::unique_ptr<Backend> m_backend;
    std::string m_owner;
    bool m_active = false, m_followCamera = false;
    ScriptVM* m_vm = nullptr;
};

} // namespace kke
