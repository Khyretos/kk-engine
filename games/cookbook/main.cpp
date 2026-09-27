// The cookbook game (docs/cookbook/): the starter template's setup plus
// every common camera (CookbookPlayer) and animation recipes on a real
// skeleton (Mannequin). Its scripts/ show the Lua side: switching cameras,
// a scripted camera path and screen shake. The docs quote this code, and
// CI builds and runs it, so the recipes can't quietly go stale.
#include "kke/Application.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/ScriptModule.h"
#include "kke/modules/SettingsModule.h"
#include "kke/modules/StatsModule.h"
#include "kke/modules/UiModule.h"

#include "CookbookPlayer.h"
#include "Mannequin.h"

#include <filesystem>
#include <iostream>

int main() {
    try {
        kke::Application app("KKE Cookbook", 1280, 720);
        kke::Lighting& light = app.lighting();
        light.lights[0].enabled = true;
        light.lights[0].direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
        light.lights[0].intensity = 2.0f;
        light.lights[1].enabled = true;
        light.lights[1].direction = glm::normalize(glm::vec3(0.6f, -0.3f, 0.5f));
        light.lights[1].color = glm::vec3(0.55f, 0.65f, 0.85f);
        light.lights[1].intensity = 0.3f;
        light.ambientColor = glm::vec3(0.2f);
        light.toneMapper = kke::ToneMapper::AgX;

        app.addModule<kke::SettingsModule>("settings.json");
        app.addModule<kke::InputModule>("input.json");
        app.addModule<kke::RigidBodyModule>();
        app.addModule<kke::ModelModule>();
        app.addModule<kke::UiModule>();
        app.addModule<kke::AudioModule>().setUiVisible(false);
        std::error_code ec;
        const bool fromSource = std::filesystem::is_directory(GAME_SCRIPTS_SOURCE, ec);
        app.addModule<kke::ScriptModule>(fromSource ? GAME_SCRIPTS_SOURCE : GAME_SCRIPTS_INSTALLED);
        app.addModule<cookbook::CookbookPlayer>();
        app.addModule<cookbook::Mannequin>();
        app.addModule<kke::StatsModule>();
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
