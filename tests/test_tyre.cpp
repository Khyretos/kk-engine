// kke::Tyre (heat, wear, air, load) and the tyres of a RigidWorld vehicle
// (the ground's grip, flats, rolling resistance). docs/VEHICLES.md "Tyres".

#include "kke/RigidWorld.h"
#include "kke/Tyre.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace kke;

namespace {

// `seconds` of the same step.
void run(Tyre& t, Tyre::Step s, float seconds) {
    s.dt = 1.0f / 60.0f;
    for (int i = 0; i < int(seconds * 60.0f); ++i) t.update(s);
}

// A car cruising: 3.2 kN on the tyre, 30 m/s, hardly sliding.
Tyre::Step cruise() {
    Tyre::Step s;
    s.load = 3200.0f;
    s.speed = 30.0f;
    s.lateralForce = 1500.0f;
    s.lateralSlipSpeed = 0.3f;
    return s;
}

// A burnout: the tyre spinning 25 m/s over the road, the car still.
Tyre::Step burnout() {
    Tyre::Step s;
    s.load = 4000.0f;
    s.speed = 0.0f;
    s.longitudinalForce = 4800.0f;
    s.longitudinalSlipSpeed = 25.0f;
    return s;
}

} // namespace

TEST(Tyre, ColdTyresGripLessThanWarmOnes) {
    Tyre t;
    const float cold = t.grip();
    t.setTemperature(85.0f, 70.0f);
    const float warm = t.grip();
    EXPECT_LT(cold, warm);
    EXPECT_GT(cold, 0.8f); // still drivable cold
    t.setTemperature(190.0f, 120.0f);
    EXPECT_LT(t.grip(), warm); // cooked
}

TEST(Tyre, CruisingWarmsSlowlySlidingHeatsFast) {
    Tyre a, b;
    run(a, cruise(), 10.0f);
    run(b, burnout(), 10.0f);
    EXPECT_GT(a.coreTemp(), 20.0f);
    EXPECT_LT(a.surfaceTemp(), 40.0f);
    // Ten seconds of burnout: smoking hot (real ones pass 150 C).
    EXPECT_GT(b.surfaceTemp(), 150.0f);
    EXPECT_GT(b.surfaceTemp(), b.coreTemp());
    EXPECT_GT(b.slidePower(), 50000.0f);
}

TEST(Tyre, ItCoolsWhenItStopsSliding) {
    Tyre t;
    run(t, burnout(), 6.0f);
    const float hot = t.surfaceTemp();
    run(t, cruise(), 20.0f);
    EXPECT_LT(t.surfaceTemp(), hot - 50.0f);
}

TEST(Tyre, PressureRisesWithHeat) {
    Tyre t;
    EXPECT_NEAR(t.pressure(), 2.2f, 1e-3f);
    t.setTemperature(80.0f, 80.0f);
    // The gas law: 2.2 x 353 / 293.
    EXPECT_NEAR(t.pressure(), 2.2f * 353.15f / 293.15f, 1e-2f);
}

TEST(Tyre, TwiceTheLoadIsLessThanTwiceTheGrip) {
    Tyre t;
    EXPECT_NEAR(t.loadFactor(3000.0f, 3000.0f), 1.0f, 1e-4f);
    EXPECT_NEAR(t.loadFactor(6000.0f, 3000.0f), 0.85f, 1e-3f);
    EXPECT_GT(t.loadFactor(1500.0f, 3000.0f), 1.0f); // a light tyre grips a little more per newton
}

TEST(Tyre, APunctureGoesFlatThenShredsToTheRim) {
    Tyre t;
    t.puncture(0.5f);
    run(t, cruise(), 2.0f);
    EXPECT_EQ(t.condition(), TyreCondition::Flat);
    EXPECT_EQ(t.pressure(), 0.0f);
    EXPECT_LT(t.radiusScale(), 0.9f);
    EXPECT_GT(t.dragScale(), 3.0f);
    // 1.5 km flat and it's off the rim.
    run(t, cruise(), 60.0f);
    EXPECT_EQ(t.condition(), TyreCondition::Rim);
    EXPECT_NEAR(t.radiusScale(), t.desc().rimRatio, 1e-4f);
    EXPECT_LT(t.grip(), 0.5f);
}

TEST(Tyre, ALongBurnoutBlowsIt) {
    Tyre t;
    run(t, burnout(), 90.0f);
    EXPECT_NE(t.condition(), TyreCondition::Inflated);
}

TEST(Tyre, ANewSetIsColdFullAndUnworn) {
    Tyre t;
    run(t, burnout(), 20.0f);
    t.setCondition(TyreCondition::Rim);
    t.replace();
    EXPECT_EQ(t.condition(), TyreCondition::Inflated);
    EXPECT_EQ(t.wear(), 0.0f);
    EXPECT_NEAR(t.surfaceTemp(), 20.0f, 1e-4f);
    EXPECT_NEAR(t.pressure(), 2.2f, 1e-3f);
}

// ---- on a vehicle

namespace {

constexpr uint32_t kTarmac = 1, kGravel = 2;

RigidWorld::Settings single() {
    RigidWorld::Settings s;
    s.threads = 0;
    return s;
}

void ground(RigidWorld& w, uint32_t material) {
    RigidWorld::BodyDesc g;
    g.motion = RigidWorld::Motion::Static;
    g.halfExtents = { 400.0f, 0.5f, 400.0f };
    g.position = { 0.0f, -0.5f, 0.0f };
    g.material = material;
    w.add(g);
}

VehicleDesc car() {
    VehicleDesc d;
    d.halfExtents = { 0.9f, 0.4f, 2.1f };
    d.position = { 0.0f, 0.9f, 0.0f };
    for (int i = 0; i < 4; ++i) {
        VehicleWheelDesc wd;
        const bool front = i < 2;
        wd.position = { i % 2 == 0 ? 0.8f : -0.8f, -0.1f, front ? 1.3f : -1.3f };
        wd.maxSteerDegrees = front ? 30.0f : 0.0f;
        wd.maxHandBrakeTorque = front ? 0.0f : 4000.0f;
        d.wheels.push_back(wd);
    }
    return d;
}

// Full throttle for `seconds` from a standstill: the speed it reaches.
float launch(RigidWorld& w, RigidWorld::VehicleId v, float seconds) {
    VehicleInput in;
    in.throttle = 1.0f;
    for (int i = 0; i < int(seconds * 60.0f); ++i) {
        w.setVehicleInput(v, in);
        w.step(1.0f / 60.0f);
    }
    VehicleState s;
    w.vehicleState(v, s);
    return s.speed;
}

} // namespace

TEST(VehicleTyres, TheyCarryTheCarAndReportIt) {
    RigidWorld w(single());
    ground(w, kTarmac);
    const RigidWorld::VehicleId v = w.addVehicle(car());
    launch(w, v, 0.0f);
    for (int i = 0; i < 90; ++i) w.step(1.0f / 60.0f); // settle
    VehicleState s;
    ASSERT_TRUE(w.vehicleState(v, s));
    float load = 0.0f;
    for (const VehicleWheelState& wh : s.wheels) {
        EXPECT_TRUE(wh.contact);
        EXPECT_EQ(wh.groundMaterial, kTarmac);
        EXPECT_EQ(wh.condition, TyreCondition::Inflated);
        EXPECT_GT(wh.pressure, 2.0f);
        load += wh.load;
    }
    EXPECT_NEAR(load, 1300.0f * 9.81f, 1300.0f * 9.81f * 0.1f); // the tyres hold the car up
}

TEST(VehicleTyres, GravelLaunchesSlowerThanTarmac) {
    float tarmac = 0.0f, gravel = 0.0f;
    for (int pass = 0; pass < 2; ++pass) {
        RigidWorld w(single());
        ground(w, pass == 0 ? kTarmac : kGravel);
        w.setGroundGrip(kGravel, GroundGrip::gravel());
        VehicleDesc d = car();
        d.maxTorque = 1400.0f; // enough to spin the tyres: traction decides
        const RigidWorld::VehicleId v = w.addVehicle(d);
        for (int i = 0; i < 60; ++i) w.step(1.0f / 60.0f);
        (pass == 0 ? tarmac : gravel) = launch(w, v, 3.0f);
    }
    EXPECT_GT(tarmac, 5.0f);
    EXPECT_LT(gravel, tarmac * 0.95f);
    EXPECT_GT(gravel, tarmac * 0.4f); // slower, not stuck
}

TEST(VehicleTyres, AFlatDropsThatCornerAndDrags) {
    RigidWorld w(single());
    ground(w, kTarmac);
    const RigidWorld::VehicleId v = w.addVehicle(car());
    for (int i = 0; i < 180; ++i) w.step(1.0f / 60.0f);
    VehicleState before;
    w.vehicleState(v, before);
    w.setVehicleTyre(v, 0, TyreCondition::Flat);
    for (int i = 0; i < 180; ++i) w.step(1.0f / 60.0f);
    VehicleState after;
    w.vehicleState(v, after);
    EXPECT_LT(after.wheels[0].radius, before.wheels[0].radius - 0.03f);
    // The wheel's centre is lower: the car sits down on that corner.
    EXPECT_LT(after.wheels[0].transform[3].y, before.wheels[0].transform[3].y - 0.03f);
    EXPECT_EQ(after.wheels[0].pressure, 0.0f);
    EXPECT_LT(after.wheels[0].grip, after.wheels[1].grip);
}

TEST(VehicleTyres, SpinningTheWheelsHeatsThem) {
    RigidWorld w(single());
    ground(w, kTarmac);
    VehicleDesc d = car();
    for (int i = 0; i < 2; ++i) d.wheels[static_cast<size_t>(i)].maxBrakeTorque = 6000.0f;
    d.maxTorque = 900.0f;
    const RigidWorld::VehicleId v = w.addVehicle(d);
    for (int i = 0; i < 60; ++i) w.step(1.0f / 60.0f);
    // A burnout: the fronts held by the brakes, the rears spun.
    VehicleInput in;
    in.throttle = 1.0f;
    in.brake = 1.0f;
    for (int i = 0; i < 300; ++i) {
        w.setVehicleInput(v, in);
        w.step(1.0f / 60.0f);
    }
    VehicleState s;
    w.vehicleState(v, s);
    EXPECT_GT(s.wheels[2].surfaceTemp, s.wheels[0].surfaceTemp + 30.0f);
    EXPECT_GT(s.wheels[2].wear, 0.0f);
}
