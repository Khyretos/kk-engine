#include "kke/Application.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/DebugControlModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/SoundVisualizerModule.h"
#include "kke/modules/StatsModule.h"

#include "AudioDemoModule.h"

#include <iostream>

// Every case the audio engine handles, one station each: rooms, walls,
// doors, falling crates, footsteps, binaural, pings. See
// AudioDemoModule.h and docs/AUDIO.md "Audio demo".
int main() {
    try {
        kke::Application app("Kreative Kompas Engine - Audio Demo", 1280, 720);
        app.camera().farPlane = 200.0f;
        app.lighting().lights[0].direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
        app.lighting().lights[1].enabled = true;
        app.lighting().lights[1].isDirectional = true;
        app.lighting().lights[1].direction = glm::normalize(glm::vec3(0.6f, -0.3f, 0.5f));
        app.lighting().lights[1].color = glm::vec3(0.55f, 0.65f, 0.85f);
        app.lighting().lights[1].intensity = 0.35f;
        app.lighting().ambientColor = glm::vec3(0.28f);
        app.addModule<kke::OrbitCameraModule>(/*distance=*/9.0f, /*pitch=*/-0.55f, /*yaw=*/0.4f, glm::vec3(0.0f, 1.6f, 0.0f));
        app.addModule<kke::RigidBodyModule>();
        app.addModule<kke::ModelModule>();
        app.addModule<kke::AudioModule>();
        app.addModule<kke::SoundVisualizerModule>().settings.enabled = true;
        app.addModule<kke_audio_demo::AudioDemoModule>();
        app.addModule<kke::DebugControlModule>().setUiVisible(false);
        app.addModule<kke::StatsModule>();
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
