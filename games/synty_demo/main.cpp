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

#include <iostream>

// Synty assets in the engine: FBX loading, textured lit props, skinned
// characters with clips, procedural and hand posing, a bone view.
int main() {
    try {
        kke::Application app("Kreative Kompas Engine - Synty Demo", 1280, 720);
        app.camera().farPlane = 200.0f;
        app.lighting().lights[0].direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
        app.lighting().lights[1].enabled = true;
        app.lighting().lights[1].isDirectional = true;
        app.lighting().lights[1].direction = glm::normalize(glm::vec3(0.6f, -0.3f, 0.5f));
        app.lighting().lights[1].color = glm::vec3(0.55f, 0.65f, 0.85f);
        app.lighting().lights[1].intensity = 0.35f;
        app.lighting().ambientColor = glm::vec3(0.25f);

        app.addModule<kke::OrbitCameraModule>(/*distance=*/9.0f, /*pitch=*/-0.35f, /*yaw=*/2.85f, glm::vec3(0.0f, 0.9f, -1.5f));
        app.addModule<kke::ModelModule>();
#if KKE_ENABLE_FEMFX
        // Ragdolls (and anything else FEMFX: fracture, soft bodies). Real
        // units, no starting objects, and the level draws its own floor.
        auto& physics = app.addModule<kke::PhysicsModule>(/*renderScale=*/1.0f, /*initialObjectCount=*/0);
        physics.setDrawGround(false);
#endif
        app.addModule<kke_demo::SyntySceneModule>();
        app.addModule<kke::DebugControlModule>();
        app.addModule<kke::StatsModule>();
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
