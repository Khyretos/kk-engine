#pragma once

#include "kke/Module.h"
#include "kke/RigidWorld.h"

#include <memory>

namespace kke {

// The engine module around kke::RigidWorld (Jolt): steps it on the fixed
// tick, shows its cost in a panel. Games reach the world with
// app.getModule<RigidBodyModule>()->world(). Rendering stays with the game
// (bodies usually drive ModelModule instances); debugDraw draws every
// body's bounds through DebugDrawModule when it's present.
class RigidBodyModule : public Module {
public:
    explicit RigidBodyModule(const RigidWorld::Settings& settings = RigidWorld::Settings{});
    const char* name() const override { return "RigidBodies"; }
    void init(Application& app) override;
    void fixedUpdate(const FixedUpdateContext& ctx) override;
    void renderUi() override;

    RigidWorld& world() { return *m_world; }
    bool paused = false;

private:
    RigidWorld::Settings m_settings;
    std::unique_ptr<RigidWorld> m_world;
    double m_msAvg = 0.0, m_msMax = 0.0;
};

} // namespace kke
