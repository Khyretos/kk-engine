#include "kke/Application.h"
#include "kke/modules/DebugControlModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/SettingsModule.h"
#include "kke/modules/StatsModule.h"

#include "SyntySceneModule.h"
#if KKE_ENABLE_FEMFX
#include "kke/modules/PhysicsModule.h"
#endif
#if KKE_ENABLE_JOLT
#include "kke/modules/RigidBodyModule.h"
#endif
#if KKE_ENABLE_FEMFX && KKE_ENABLE_JOLT
#include "kke/modules/PhysicsBridgeModule.h"
#endif

#include <iostream>
#include <vector>

// Synty assets in the engine: FBX loading, textured lit props, skinned
// characters with clips, procedural and hand posing, a bone view.
int main() {
    try {
        kke::Application app("Kreative Kompas Engine - Synty Demo", 1280, 720);
        // Warm, low sun over the Synty scenes (assets/moods/golden_hour.yaml).
        app.setMood("golden_hour");
        app.camera().farPlane = 200.0f;

        std::vector<kke::Module*> panels;
        panels.push_back(&app.addModule<kke::OrbitCameraModule>(/*distance=*/9.0f, /*pitch=*/-0.35f, /*yaw=*/2.85f, glm::vec3(0.0f, 0.9f, -1.5f)));
        app.addModule<kke::ModelModule>();
#if KKE_ENABLE_FEMFX
        // Ragdolls (and anything else FEMFX: fracture, soft bodies). Real
        // units, no starting objects, and the level draws its own floor.
        auto& physics = app.addModule<kke::PhysicsModule>(/*renderScale=*/1.0f, /*initialObjectCount=*/0);
        physics.setDrawGround(false);
        panels.push_back(&physics);
#endif
#if KKE_ENABLE_JOLT
        // Jolt ragdolls (joint limits, limbs that collide) win over FEMFX's
        // when both are present; the scene gives Jolt its floor and walls.
        panels.push_back(&app.addModule<kke::RigidBodyModule>());
#endif
#if KKE_ENABLE_FEMFX && KKE_ENABLE_JOLT
        // Jolt ragdolls and FEMFX glass meet (the G key).
        panels.push_back(&app.addModule<kke::PhysicsBridgeModule>());
#endif
        auto& scene = app.addModule<kke_demo::SyntySceneModule>();
        panels.push_back(&app.addModule<kke::DebugControlModule>());
        panels.push_back(&app.addModule<kke::StatsModule>());
        scene.setEnginePanels(panels);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
