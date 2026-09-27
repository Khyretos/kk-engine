#include "kke/Application.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/DebugControlModule.h"
#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/SoundVisualizerModule.h"
#include "kke/modules/StatsModule.h"
#include "kke/modules/UiModule.h"

#include "AudioDemoModule.h"

#include <iostream>

// Every case the audio engine handles, one station each: rooms, walls,
// doors, falling crates, footsteps, binaural, pings. See
// AudioDemoModule.h and docs/AUDIO.md "Audio demo".
int main() {
    try {
        kke::Application app("Kreative Kompas Engine - Audio Demo", 1280, 720);
        // A clear day in an open field, so the sounds are the story (assets/moods/clear_day.yaml).
        app.setMood("clear_day");
        app.camera().farPlane = 200.0f;
        // Controls are actions (keyboard or controller, rebindable); the
        // demo's panel is RmlUi (kke::DemoPanelModule, on the right).
        app.addModule<kke::InputModule>("audio_demo_input.json");
        app.addModule<kke::UiModule>();
        app.addModule<kke::OrbitCameraModule>(/*distance=*/9.0f, /*pitch=*/-0.55f, /*yaw=*/0.4f, glm::vec3(0.0f, 1.6f, 0.0f))
            .setPadControls(true); // right stick turns you, d-pad zooms
        app.addModule<kke::RigidBodyModule>();
        app.addModule<kke::ModelModule>();
        app.addModule<kke::AudioModule>();
        app.addModule<kke::SoundVisualizerModule>().settings.enabled = true;
        app.addModule<kke_audio_demo::AudioDemoModule>();
        app.addModule<kke::DemoPanelModule>("Audio demo", kke::DemoPanelModule::Side::Right).setWidth(370.0f);
        app.addModule<kke::DebugControlModule>().setUiVisible(false);
        app.addModule<kke::StatsModule>();
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
