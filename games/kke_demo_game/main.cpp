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
#if KKE_ENABLE_FEMFX
#include "kke/modules/PhysicsModule.h"
#endif
#include "CubeModule.h"
#include "DestructionModule.h"
#include "NetworkModule.h"

#include <iostream>

// This is the whole demo: create an Application, add the modules you want,
// run it. Nothing engine-specific happens here — everything that draws or
// simulates something is a Module (see kke/Module.h and the README's
// "Adding a module" / "Cross-module communication" sections).
//
// DestructionModule and NetworkModule find each other through
// kke::INetworkReplicable (see kke/Capabilities.h) without either one
// including the other's header — comment out the NetworkModule line and
// DestructionModule still works exactly the same, just with nothing
// asking it for its replicated state.
int main() {
    try {
        kke::Application app("Kreative Kompas Engine - Demo", 1280, 720);

        // Best-effort hardware check against this game's own
        // developer-declared requirements (game.json's "requirements"
        // section) — see README "Estimated hardware requirements" for
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
                             "(see README 'Estimated hardware requirements').", manifest.title);
            } else if (check.warnings.empty()) {
                logger->info("Hardware check: this device meets '{}' recommended requirements.", manifest.title);
            }
        } catch (const std::exception& e) {
            kke::log::get("HardwareCheck")->warn("skipping hardware check: {}", e.what());
        }

        // Opaque geometry first, then transparent (grid, particles) — see
        // GridModule's comment on why draw order matters when depth
        // writes are disabled.
        app.addModule<kke_demo::CubeModule>();
        app.addModule<kke::GridModule>();
        app.addModule<kke::OrbitCameraModule>();
        app.addModule<kke::ParticleModule>(20000);
        app.addModule<kke_demo::DestructionModule>(16, 1234);
        app.addModule<kke_demo::NetworkModule>();
        app.addModule<kke::UiModule>();
        app.addModule<kke::MarketplaceUiModule>("marketplace");
        app.addModule<kke::DebugControlModule>();
#if KKE_ENABLE_FEMFX
        app.addModule<kke::PhysicsModule>();
#endif
        app.addModule<kke::StatsModule>();

        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
