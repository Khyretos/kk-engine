#include "kke/Application.h"
#include "kke/modules/GridModule.h"
#include "kke/modules/StatsModule.h"
#include "kke/modules/ParticleModule.h"
#include "CubeModule.h"

#include <iostream>

// This is the whole demo: create an Application, add the modules you want,
// run it. Nothing engine-specific happens here — everything that draws or
// simulates something is a Module (see kke/Module.h and the README's
// "Adding a module" section for how to write your own).
int main() {
    try {
        kke::Application app("Kreative Kompas Engine - Demo", 1280, 720);

        // Opaque geometry first, then transparent (grid, particles) — see
        // GridModule's comment on why draw order matters when depth
        // writes are disabled.
        app.addModule<kke_demo::CubeModule>();
        app.addModule<kke::GridModule>();
        app.addModule<kke::ParticleModule>(20000);
        app.addModule<kke::StatsModule>();

        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
