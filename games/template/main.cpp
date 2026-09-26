// The starter game: copy this folder with tools/new_game to begin your own
// (docs/tutorials/getting-started.md). A game is a list of modules; the
// engine's modules do the heavy lifting, PlayerModule is yours, and the
// level and rules live in Lua (scripts/game.lua), which reloads while the
// game runs.
#include "kke/Application.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/ScriptModule.h"
#include "kke/modules/SettingsModule.h"
#include "kke/modules/StatsModule.h"
#include "kke/modules/UiModule.h"
#if KKE_ENABLE_FEMFX
#include "kke/modules/PhysicsBridgeModule.h"
#include "kke/modules/PhysicsModule.h"
#endif

#include "PlayerModule.h"

#include <filesystem>
#include <iostream>

int main() {
    try {
        kke::Application app("Starter Game", 1280, 720);
        // A sun, a little sky light from the side, and some ambient light.
        kke::Lighting& light = app.lighting();
        light.lights[0].enabled = true;
        light.lights[0].direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
        light.lights[0].intensity = 2.0f;
        light.lights[1].enabled = true;
        light.lights[1].direction = glm::normalize(glm::vec3(0.6f, -0.3f, 0.5f));
        light.lights[1].color = glm::vec3(0.55f, 0.65f, 0.85f);
        light.lights[1].intensity = 0.3f;
        light.ambientColor = glm::vec3(0.2f);

        app.addModule<kke::SettingsModule>("settings.json"); // graphics, audio, accessibility
        app.addModule<kke::InputModule>("input.json");       // rebindable controls
        app.addModule<kke::RigidBodyModule>();                // Jolt physics
        app.addModule<kke::ModelModule>();                    // 3D models (models.* in Lua)
        app.addModule<kke::UiModule>();                       // HUD and menus (ui.* in Lua)
        app.addModule<kke::AudioModule>().setUiVisible(false);
#if KKE_ENABLE_FEMFX
        // Things that really break (breakable.* in Lua), with the
        // "everything" build preset.
        app.addModule<kke::PhysicsModule>(/*renderScale=*/1.0f, /*initialObjectCount=*/0).setDrawGround(false);
        app.addModule<kke::PhysicsBridgeModule>();
#endif
        // scripts/*.lua, hot-reloaded: from this game's source folder while
        // you develop, else the copy next to the executable.
        std::error_code ec;
        const bool fromSource = std::filesystem::is_directory(GAME_SCRIPTS_SOURCE, ec);
        app.addModule<kke::ScriptModule>(fromSource ? GAME_SCRIPTS_SOURCE : GAME_SCRIPTS_INSTALLED);
        app.addModule<starter::PlayerModule>();
        app.addModule<kke::StatsModule>();
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
