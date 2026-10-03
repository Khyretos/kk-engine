#include "kke/Application.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/DebugControlModule.h"
#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/StatsModule.h"
#include "kke/modules/UiModule.h"

#include "ParticlesDemoModule.h"

#include <iostream>

// Every effect in kke::ParticleLibrary, one at a time or all at once.
int main() {
    try {
        kke::Application app("Kreative Kompas Engine - Particles Demo", 1280, 720);
        // Dusk: fire, sparks and magic glow against a darker sky, and the
        // sun is still low enough to light smoke and confetti from the side.
        app.setMood("dusk");
        app.addModule<kke::InputModule>("particles_demo_input.json");
        app.addModule<kke::UiModule>();
        app.addModule<kke::AudioModule>().setUiVisible(false);
        app.addModule<kke::OrbitCameraModule>(/*distance=*/7.0f, /*pitch=*/-0.3f, /*yaw=*/0.5f, glm::vec3(0.0f, 1.2f, 0.0f))
            .setPadControls(true); // right stick turns, d-pad zooms
        app.addModule<kke_particles::ParticlesDemoModule>();
        app.addModule<kke::DemoPanelModule>("Particles");
        app.addModule<kke::DebugControlModule>().setUiVisible(false);
        app.addModule<kke::StatsModule>();
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
