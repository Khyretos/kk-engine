#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/modules/CharacterCreatorModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/StatsModule.h"
#include "kke/modules/UiModule.h"

#include <iostream>

// The character creator on its own (kke::CharacterCreatorModule,
// docs/HUMANS.md): the base other games build their own screen from.
// Done prints the share code; a game would bake the character
// (CharacterCreatorModule::bakeSkinned) and send the code to the others.
int main() {
    try {
        kke::Application app("Kreative Kompas Engine - Character Creator", 1280, 720);
        // An even studio light shows a face's shape best (assets/moods).
        app.setMood("studio");
        app.debugUi().setVisible(false); // the creator is the whole screen
        app.addModule<kke::InputModule>("character_creator_input.json");
        app.addModule<kke::ModelModule>();
        app.addModule<kke::UiModule>();
        auto& orbit = app.addModule<kke::OrbitCameraModule>(/*distance=*/4.0f, /*pitch=*/-0.08f, /*yaw=*/3.14159265f,
                                                            glm::vec3(0.4f, 0.9f, 0.0f));
        orbit.setDistanceLimits(0.4f, 8.0f);
        orbit.setPadControls(true);
        auto& creator = app.addModule<kke::CharacterCreatorModule>();
        creator.onDone([&creator](const kke::HumanLook&) {
            kke::log::get("CharacterCreator")->info("share code: {}", creator.shareCode());
        });
        app.addModule<kke::StatsModule>();
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
