#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/UiModule.h"
#include "kke/modules/MaterialGridModule.h"
#include "kke/modules/DebugControlModule.h"
#include "kke/modules/StatsModule.h"
#if KKE_ENABLE_FEMFX
#include "kke/modules/PhysicsModule.h"
#endif

#include <iostream>

// A dedicated physics demo — deliberately separate from kke_demo_game,
// not another module bolted onto that one. The reason is concrete, not
// stylistic: PhysicsModule's real-world simulation units (a 100-unit
// floor, gravity=9.88, objects falling from height 5) don't share a
// sensible camera with kke_demo_game's small-scale content (a unit
// cube at orbit distance 3.5) without a render-time scale hack shrinking
// everything down to nearly invisible. This demo instead gives the
// camera its own distance/angle suited to the real scale, and asks
// PhysicsModule to render at that real scale (renderScale=1.0) rather
// than fight the mismatch. See README "Physics: AMD FEMFX integration"
// and "Content pipeline: CGAL tetrahedralization" for how this
// simulation itself was built and verified — this file is just the
// demo-specific setup: camera placement and how many objects to drop.
//
// Requires KKE_ENABLE_FEMFX=ON (this whole file compiles to nothing
// useful otherwise — see the #if below). Not built unless FEMFX is
// enabled, matching how the physics parts of kke_demo_game are gated.
int main() {
#if !KKE_ENABLE_FEMFX
    std::cerr << "physics_demo requires -DKKE_ENABLE_FEMFX=ON -- this build was configured without it.\n";
    return 1;
#else
    try {
        kke::Application app("Kreative Kompas Engine - Physics Demo", 1280, 720);

        // Distance and pitch chosen for this demo's actual content
        // scale, not inherited from kke_demo_game's small-scale
        // defaults -- see this file's own top comment for why that
        // distinction matters. Pitch is negative (camera above the
        // target, looking down) so a viewer sees the floor and the
        // falling objects landing on it, not the underside of
        // anything. Target sits slightly above the floor, roughly
        // where the objects actually land.
        app.addModule<kke::OrbitCameraModule>(
            /*initialDistance=*/15.0f,
            /*initialPitch=*/-0.4f,
            /*initialYaw=*/-0.6f,
            /*initialTarget=*/glm::vec3(0.0f, 1.0f, 1.0f)
        );

        // renderScale=1.0: real physics units, matching the camera
        // above rather than the 0.02 shrink kke_demo_game needs.
        // initialObjectCount=6: enough to be visibly "several things
        // happening," spread out (see PhysicsModule::init()'s own
        // spawn loop) so they don't all land in one overlapping pile.
        app.addModule<kke::PhysicsModule>(/*renderScale=*/1.0f, /*initialObjectCount=*/6);

        // A real demonstration of multi-light support (see README
        // "Lighting"), not just a single hardcoded light left at its
        // default -- a cool-toned fill light from roughly the
        // opposite side of the default warm key light, low intensity,
        // so it softens shadowed faces without washing out the real
        // directional shading the key light provides.
        kke::Light& fillLight = app.lighting().lights[1];
        fillLight.enabled = true;
        fillLight.isDirectional = true;
        fillLight.direction = glm::normalize(glm::vec3(0.6f, -0.3f, 0.5f));
        fillLight.color = glm::vec3(0.55f, 0.65f, 0.85f); // cool blue-ish fill
        fillLight.intensity = 0.35f;

        app.addModule<kke::UiModule>();
        app.addModule<kke::MaterialGridModule>();
        app.addModule<kke::DebugControlModule>();
        app.addModule<kke::StatsModule>();

        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
#endif
}
