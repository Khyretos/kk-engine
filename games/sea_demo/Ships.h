#pragma once

// The ships (README.md "Ships"): five classes from Synty's POLYGON Pirate
// Pack, from a rowing boat to a man-o'-war, each with its own speed,
// turning, guns and toughness. With the pack missing every class is built
// from boxes at the same size, so the demo plays the same.

#include "kke/AssetCatalog.h"
#include "kke/Mesh.h"
#include "kke/modules/ModelModule.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace kke {
class Application;
class DynamicMeshRenderer;
}

namespace kke_sea {

// Ship space: +Z is the bow, +Y up, +X port (left), -X starboard.
struct ShipClass {
    const char* id;
    const char* name;
    const char* blurb;
    // Synty file names (POLYGON_Pirate_Pack/SourceFiles/FBX). Masts and
    // sails are matched to each other by where they stand.
    const char* hull;
    std::vector<const char*> masts, sails, rigging;
    // Box art (no pack) and the hull size the pack's art is scaled to.
    float length, beam, height;
    // Handling. Speeds in m/s (1 knot = 0.51 m/s).
    float topSpeed;      // full sail, wind from astern
    float acceleration;  // m/s^2 from a standstill at full sail
    float turnRate;      // degrees/s at speed, full rudder
    bool oars;           // rowed: the throttle is the oars, no sails, no wind
    // Guns and toughness.
    int gunsPerSide;
    int gunDecks;        // 1 or 2 rows of guns
    float reload;        // s per broadside
    float ballMass;      // kg: a 6-pounder is 2.7 kg, a 24-pounder 11 kg
    float health;
};

const std::vector<ShipClass>& shipClasses();

// One class's art, loaded once and shared by every ship of that class.
struct ShipArt {
    enum class Role { Hull, Mast, Sail, Rigging };
    struct Part {
        kke::ModelModule::ModelId model = 0;
        Role role = Role::Hull;
        int mast = -1;          // masts and sails: which mast
        glm::vec3 lo{0.0f}, hi{0.0f}; // ship-space bounds
    };
    struct Mast {
        glm::vec3 base{0.0f};   // ship space, at the deck
        float height = 10.0f;
        float halfWidth = 4.0f; // yards
    };
    bool loaded = false;        // the Synty art (false: boxes)
    std::vector<Part> parts;
    std::vector<Mast> masts;
    glm::mat4 modelToShip{1.0f};
    // The floating box: centre and half size in ship space.
    glm::vec3 hullCenter{0.0f}, hullHalf{1.0f};
    float deckY = 1.0f;          // the main deck, ship space
    std::vector<glm::vec3> guns; // muzzles on the port side (+X); starboard mirrors X
    // The hull as made (model space), for dents: one entry per mesh part.
    std::vector<std::vector<glm::vec3>> hullPositions, hullNormals;
    std::vector<std::vector<uint32_t>> hullIndices;
    float modelScale = 1.0f;     // model units -> metres
    // Box art.
    std::unique_ptr<kke::DynamicMeshRenderer> boxHull, boxMast, boxSail;
};

class ShipArtLibrary {
public:
    // Finds the pack (assets/synty or KKE_ASSETS_DIR); builds box art for
    // every class either way.
    void load(kke::Application& app, kke::ModelModule* models);
    const ShipArt& art(int shipClass) const { return m_art[static_cast<size_t>(shipClass)]; }
    const std::string& status() const { return m_status; }
    const std::vector<std::string>& used() const { return m_used; }
    // Extra props from the same pack (flags, islands, rocks, debris): 0 when
    // missing. Same scale rules as the ships.
    kke::ModelModule::ModelId prop(const std::string& asset);
    float propScale(kke::ModelModule::ModelId id) const;
    bool packFound() const { return m_packFound; }

private:
    void loadClass(int index, const ShipClass& c);
    void buildBoxes(kke::Application& app, int index, const ShipClass& c);

    kke::ModelModule* m_models = nullptr;
    kke::AssetCatalog m_catalog;
    bool m_packFound = false;
    std::vector<ShipArt> m_art;
    std::vector<std::pair<kke::ModelModule::ModelId, float>> m_propScales;
    std::string m_status;
    std::vector<std::string> m_used;
};

} // namespace kke_sea
