#include "kke/Application.h"
#include "kke/GameManifest.h"
#include "kke/HardwareCheck.h"
#include "kke/Log.h"
#include "kke/modules/GridModule.h"
#include "kke/modules/StatsModule.h"
#include "kke/modules/ParticleModule.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/UiModule.h"
#include "kke/modules/MarketplaceUiModule.h"
#include "kke/modules/DebugControlModule.h"
#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/MaterialGridModule.h"
#if KKE_ENABLE_FEMFX
#include "kke/modules/PhysicsModule.h"
#endif
#include "CubeModule.h"
#include "DestructionModule.h"
#include "NetworkModule.h"

#include <iostream>

// This is the whole demo: create an Application, add the modules you want,
// run it. Nothing engine-specific happens here — everything that draws or
// simulates something is a Module (see kke/Module.h and docs/HISTORY.md's
// "Adding a module" / "Cross-module communication" sections).
//
// DestructionModule and NetworkModule find each other through
// kke::INetworkReplicable (see kke/Capabilities.h) without either one
// including the other's header — comment out the NetworkModule line and
// DestructionModule still works exactly the same, just with nothing
// asking it for its replicated state.
namespace {

// The lighting rows (what LightingControlsModule's own RmlUi document
// does, but in the panel, so a controller reaches them too).
void addLightingRows(kke::Application& app, kke::DemoPanelModule& panel) {
    auto& s = panel.section("Lighting");
    kke::Lighting& l = app.lighting();
    static float ambient = 0.15f;
    l.ambientColor = glm::vec3(ambient);
    l.lights[1].enabled = false;
    s.slider("Ambient", &ambient, 0.0f, 1.0f, "%.2f", [&l] { l.ambientColor = glm::vec3(ambient); });
    s.slider("Key light", &l.lights[0].intensity, 0.0f, 3.0f);
    s.slider("Fill light", &l.lights[1].intensity, 0.0f, 2.0f, "%.2f", [&l] { l.lights[1].enabled = l.lights[1].intensity > 0.0f; });
    s.button("Warm key + cool fill", [&l] {
        ambient = 0.15f;
        l.ambientColor = glm::vec3(ambient);
        l.lights[0].color = glm::vec3(1.0f, 0.85f, 0.65f);
        l.lights[0].intensity = 1.0f;
        l.lights[1].enabled = true;
        l.lights[1].isDirectional = true;
        l.lights[1].direction = glm::normalize(glm::vec3(0.6f, -0.3f, 0.5f));
        l.lights[1].color = glm::vec3(0.55f, 0.65f, 0.85f);
        l.lights[1].intensity = 0.4f;
    });
    s.button("Dramatic (low ambient)", [&l] {
        ambient = 0.03f;
        l.ambientColor = glm::vec3(0.03f, 0.03f, 0.05f);
        l.lights[0].intensity = 1.6f;
        l.lights[1].enabled = false;
        l.lights[1].intensity = 0.0f;
    });
    s.button("Flat (bright ambient)", [&l] {
        ambient = 0.6f;
        l.ambientColor = glm::vec3(ambient);
        l.lights[0].intensity = 0.5f;
        l.lights[1].enabled = false;
        l.lights[1].intensity = 0.0f;
    });
    s.button("Reset lighting", [&l] {
        const bool sky = l.sky.lightsScene;
        l = kke::Lighting{};
        l.sky.lightsScene = sky;
        l.lights[1].enabled = false;
        l.lights[1].intensity = 0.0f;
        ambient = l.ambientColor.x;
    });
}

} // namespace

int main() {
    try {
        kke::Application app("Kreative Kompas Engine - Demo", 1600, 900);
        // A calm neutral backdrop for a test bench (assets/moods/studio.yaml).
        app.setMood("studio");
        // The panel's Lighting rows set a flat ambient with a slider and
        // presets: keep that in charge of the fill.
        app.lighting().sky.lightsScene = false;

        // Best-effort hardware check against this game's own
        // developer-declared requirements (game.json's "requirements"
        // section) — see docs/HISTORY.md "Estimated hardware requirements" for
        // why this is developer-declared rather than automatically
        // inferred, and why it only ever warns, never blocks. Reuses the
        // same game.json copy already placed at marketplace/kke_demo_game/
        // for MarketplaceUiModule, rather than a second copy of the file.
        try {
            kke::GameManifest manifest = kke::loadGameManifest("marketplace/kke_demo_game");
            kke::HardwareCheckResult check = kke::checkHardwareRequirements(manifest, app.device());
            auto logger = kke::log::get("HardwareCheck");
            for (const auto& warning : check.warnings) {
                logger->warn(warning);
            }
            if (!check.meetsMinimum) {
                logger->warn("This device is below '{}' minimum stated requirements. "
                             "The game will still run — this is informational, not a block "
                             "(see docs/HISTORY.md 'Estimated hardware requirements').", manifest.title);
            } else if (check.warnings.empty()) {
                logger->info("Hardware check: this device meets '{}' recommended requirements.", manifest.title);
            }
        } catch (const std::exception& e) {
            kke::log::get("HardwareCheck")->warn("skipping hardware check: {}", e.what());
        }

        // Opaque geometry first, then transparent (grid, particles) — see
        // GridModule's comment on why draw order matters when depth
        // writes are disabled.
        app.addModule<kke::InputModule>("kke_basics_input.json");
        app.addModule<kke_demo::CubeModule>();
        app.addModule<kke::GridModule>();
        app.addModule<kke::OrbitCameraModule>().setPadControls(true); // right stick turns, d-pad zooms
        app.addModule<kke::ParticleModule>(20000);
        app.addModule<kke_demo::DestructionModule>(16, 1234);
        app.addModule<kke_demo::NetworkModule>();
        app.addModule<kke::UiModule>();
        app.addModule<kke::MarketplaceUiModule>("marketplace");
        // Every setting in one RmlUi panel on the right (the modules add
        // their own sections: Cube, Destruction, Network); the mouse, F3
        // or a controller's View reach it, Esc too (with Quit).
        auto& panel = app.addModule<kke::DemoPanelModule>("Basics", kke::DemoPanelModule::Side::Right);
        addLightingRows(app, panel);
        app.addModule<kke::DebugControlModule>();
#if KKE_ENABLE_FEMFX
        auto& physics = app.addModule<kke::PhysicsModule>();
        // The material picker (MaterialGridModule's presets) as panel rows.
        auto& m = panel.section("Material");
        static int material = 0;
        std::vector<std::string> names;
        for (int i = 0; i < kke::MaterialGridModule::presetCount(); ++i) names.push_back(kke::MaterialGridModule::presetLabel(i));
        m.choice("Material", &material, names, [&physics] { physics.selectedMaterial() = kke::MaterialGridModule::presetMaterial(material); });
        m.button("Spawn tetrahedron", [&physics] { physics.spawnTetrahedronHere(); });
#endif
        app.addModule<kke::StatsModule>();

        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
