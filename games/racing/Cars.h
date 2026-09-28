#pragma once

#include "kke/AssetCatalog.h"
#include "kke/ModelAsset.h"
#include "kke/Vehicle.h"
#include "kke/modules/ModelModule.h"

#include <glm/glm.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace racing {

// The cars: six kinds from Synty's POLYGON Street Racer pack, each with
// its own engine, gearbox, weight and grip (kke::VehicleDesc), four body
// kits (the pack's presets) and every paint job the pack has.
struct CarType {
    const char* id;          // "muscle"
    const char* name;        // "Stock car"
    const char* synty;       // the pack's name for it: SK_Veh_Preset_<synty>_0N
    float mass;              // kg
    float torque;            // Nm
    float maxRpm;
    int drive;               // 0 rear, 1 front, 2 all wheels
    float grip;              // tyres: sideways grip
    float steer;             // degrees of front-wheel lock
    glm::vec3 blockSize;     // the stand-in block car (no art): width, height, length
};
const std::vector<CarType>& carTypes();
int carTypeIndex(const std::string& id); // -1 unknown

struct Paint {
    const char* name;
    const char* texture;     // PolygonStreetRacer_Veh_Tex_<texture>.png
    glm::vec3 color;         // for the menu swatch and the stand-in block car
};
const std::vector<Paint>& paints();
constexpr int kKits = 4;

// What a car is made of on screen and in Jolt: the body (one mesh part per
// material, wheels taken out), a left and a right wheel (centred on the
// wheel, in the car's axes), where the four wheels sit, and the chassis
// hull. From the pack when it's there, otherwise a block car built here.
struct CarArt {
    bool synty = false;
    kke::ModelModule::ModelId body = 0, wheel[2] = {}; // wheel[0] left, [1] right
    std::vector<std::vector<glm::vec3>> positions, normals; // the body's parts, as made (for dents)
    std::vector<std::vector<uint32_t>> indices;
    // The wheels' parts as made (centred on the wheel, car axes): the tyre
    // squashes on the road and bulges, goes flat, shreds off the rim.
    std::vector<std::vector<glm::vec3>> wheelPositions[2], wheelNormals[2];
    glm::vec3 wheelCenter[4]{};          // fl, fr, rl, rr (car space)
    float wheelRadius = 0.36f, wheelWidth = 0.3f;
    glm::vec3 boundsMin{-1.0f, 0.0f, -2.5f}, boundsMax{1.0f, 1.4f, 2.5f};
    std::vector<glm::vec3> hull;         // Jolt chassis
};

// Loads the pack's cars once each (type, kit, paint) and hands them out;
// no pack: block cars.
class CarGarage {
public:
    CarGarage(kke::ModelModule* models, const kke::AssetCatalog* catalog);
    const CarArt& art(int type, int kit, int paint);
    bool hasPack() const { return m_packRoot.size() > 0; }
    // The pack's assets this game used (docs/SCENES.md lists them).
    const std::vector<std::string>& used() const { return m_used; }

private:
    CarArt loadSynty(int type, int kit, int paint);
    CarArt makeBlock(int type, int paint);
    void finish(CarArt& art, const kke::ModelData& body); // parts for dents, bounds, hull
    static void keepWheel(CarArt& art, int side, const kke::ModelData& wheel); // parts for the tyre's squash
    kke::ModelModule* m_models;
    const kke::AssetCatalog* m_catalog;
    std::string m_packRoot, m_textures;
    std::map<std::string, CarArt> m_cache;
    std::vector<std::string> m_used;
};

// A car's Jolt description from its type and art.
kke::VehicleDesc vehicleDesc(const CarType& type, const CarArt& art, bool manualGearbox, bool drift);

} // namespace racing
