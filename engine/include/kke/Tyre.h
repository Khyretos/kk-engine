#pragma once

#include <cstdint>

namespace kke {

// A tyre's life beyond its friction curve: how hot it is, how worn, how
// much air is in it, how load and the ground change its grip, and what's
// left of it after a crash. RigidWorld runs one per vehicle wheel (the
// grip it gives goes into the wheel's friction every step) and reports it
// in VehicleWheelState; docs/VEHICLES.md "Tyres".
//
// What the sims do, and what's here (docs/VEHICLES.md has the sources):
//   - Heat in two layers, as rFactor 2 and Assetto Corsa model it: the
//     tread's surface heats fast from sliding (the friction's power, force
//     x slip speed) and cools in the air, faster with speed; the carcass
//     and the air inside warm slowly from the rubber flexing as it rolls
//     (more when it's soft) and from the surface. Grip peaks in a
//     temperature window: cold tyres slide, overheated ones go greasy.
//   - Pressure follows the air's temperature (the gas law), and a
//     puncture lets it out. A soft tyre flexes more (heat, drag), a flat
//     one runs on its sidewalls, and past that the rim meets the road.
//   - Wear from sliding, much faster when overheated; a worn-out tyre
//     blows.
//   - Load sensitivity (BeamNG's noLoadCoef / fullLoadCoef, every sim's
//     rule): twice the load gives less than twice the grip, so weight
//     moving across the car in a corner costs grip (balance, not rails).
//   - The ground: each surface its own grip, rolling drag and shape of
//     the friction curve. On loose gravel, dirt or snow the grip hardly
//     drops once the tyre slides (it digs in: rally cars slide on purpose).
// Every number costs a few multiplies a step per wheel.
struct TyreDesc {
    float optimalTemp = 80.0f;   // C: the grip window's middle (road tyre ~80, race slick ~95)
    float window = 55.0f;        // C either side before the grip is down by `tempLoss`
    float tempLoss = 0.15f;      // share of grip lost far outside the window
    float pressure = 2.2f;       // bar, cold
    float loadSensitivity = 0.15f; // grip at 2x the car's static load is (1 - this) of it per unit load
    float heat = 1.0f;           // x the heating (0 = no temperature model: grip as at optimalTemp)
    float wearRate = 1.0f;       // x the wear (0 = never wears)
    float rimRatio = 0.72f;      // rim radius / tyre radius (what's left when the tyre is gone)
};

// How much of the tyre is there.
enum class TyreCondition : uint8_t {
    Inflated, // as it should be
    Flat,     // punctured and down: on its sidewalls (lower, soft, draggy, wobbly)
    Rim,      // the tyre's gone: metal on the road (little grip, sparks)
    Detached, // the whole wheel is off: the car sits on its hub
};

// A kind of ground (RigidWorld::setGroundGrip by BodyDesc::material).
struct GroundGrip {
    float grip = 1.0f;          // x the tyre's peak grip (tarmac 1, gravel 0.62, snow 0.35, ice 0.12)
    float loose = 0.0f;         // 0..1: how much of the peak a sliding tyre keeps (0 tarmac: it falls off, 1 gravel: none lost)
    float rolling = 0.012f;     // rolling resistance coefficient (tarmac 0.012, gravel 0.04, mud 0.15)
    float heat = 1.0f;          // x the tyre's heating (loose ground slides without scrubbing much heat)
    // Named grounds (tuned from the numbers in docs/VEHICLES.md "Tyres").
    static GroundGrip tarmac() { return {}; }
    static GroundGrip concrete() { return { 0.95f, 0.0f, 0.011f, 1.0f }; }
    static GroundGrip gravel() { return { 0.62f, 0.8f, 0.04f, 0.35f }; }
    static GroundGrip dirt() { return { 0.68f, 0.7f, 0.05f, 0.45f }; }
    static GroundGrip grass() { return { 0.55f, 0.6f, 0.06f, 0.3f }; }
    static GroundGrip mud() { return { 0.45f, 0.9f, 0.15f, 0.2f }; }
    static GroundGrip snow() { return { 0.35f, 0.9f, 0.03f, 0.15f }; }
    static GroundGrip ice() { return { 0.12f, 0.5f, 0.01f, 0.05f }; }
};

// One tyre's state and its update.
class Tyre {
public:
    explicit Tyre(const TyreDesc& desc = {}, float ambient = 20.0f);
    void setDesc(const TyreDesc& d) { m_desc = d; }
    const TyreDesc& desc() const { return m_desc; }

    // What the wheel did this step (RigidWorld fills it from Jolt).
    struct Step {
        float dt = 0.0f;
        float load = 0.0f;            // N pressing it on the ground (0 in the air)
        float speed = 0.0f;           // m/s the car moves over the ground here
        float longitudinalForce = 0.0f, lateralForce = 0.0f; // N the ground gave
        float longitudinalSlipSpeed = 0.0f, lateralSlipSpeed = 0.0f; // m/s the tread slides over the ground
        float groundHeat = 1.0f;      // GroundGrip::heat under it
    };
    void update(const Step& s);

    // Grip x from temperature, pressure, wear and what's left of it
    // (not the ground or the load: those are per contact, see below).
    float grip() const;
    // Grip x for `load` N against `nominalLoad` (the car at rest): the
    // load-sensitivity rule, (load / nominal)^-k, kept within 0.7..1.3.
    float loadFactor(float load, float nominalLoad) const;
    // The tyre's radius now as a share of its full radius (flat and
    // on the rim are lower).
    float radiusScale() const;
    // Extra rolling resistance from being soft or gone (x the ground's).
    float dragScale() const;

    float surfaceTemp() const { return m_surface; } // C
    float coreTemp() const { return m_core; }       // C
    float pressure() const;                         // bar now
    float wear() const { return m_wear; }           // 0 new .. 1 gone (it blows)
    float slidePower() const { return m_slidePower; } // W the tread slid off in the last step (smoke, marks)
    TyreCondition condition() const { return m_condition; }

    // Damage and service.
    void puncture(float leak = 0.4f);  // starts losing air: `leak` of the cold pressure a second
    void setCondition(TyreCondition c);
    void setTemperature(float surface, float core); // tyre warmers, a pit stop's fresh set
    void replace();                    // a new tyre: cold (ambient), full, unworn, inflated
    void setAmbient(float c) { m_ambient = c; }

private:
    TyreDesc m_desc;
    float m_ambient = 20.0f;
    float m_surface = 20.0f, m_core = 20.0f;
    float m_wear = 0.0f;
    float m_air = 1.0f;     // share of the cold air mass left in it
    float m_leak = 0.0f;    // share a second
    float m_slidePower = 0.0f;
    TyreCondition m_condition = TyreCondition::Inflated;
};

} // namespace kke
