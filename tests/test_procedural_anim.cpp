// Procedural animation (kke/ProceduralAnim.h): gait tables and changes,
// footsteps, body sway, look-at, FABRIK, layering and the active ragdoll's
// states. The Jolt motor side is in test_jolt_ragdoll.cpp.
#include "kke/ProceduralAnim.h"

#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <algorithm>

namespace {

kke::ModelBone bone(const char* name, int parent, const glm::vec3& offset) {
    kke::ModelBone b;
    b.name = name;
    b.parent = parent;
    b.localRest = glm::translate(glm::mat4(1.0f), offset);
    return b;
}

// A quadruped with Quaternius bone names, facing +Z, feet on y = 0.
kke::ModelData animal() {
    kke::ModelData m;
    auto add = [&](const char* name, int parent, glm::vec3 offset) {
        m.bones.push_back(bone(name, parent, offset));
        return static_cast<int>(m.bones.size()) - 1;
    };
    const int hips = add("Hips", -1, { 0, 0.6f, -0.3f });
    const int torso = add("Torso", hips, { 0, 0.02f, 0.3f });
    const int shoulders = add("Shoulders", torso, { 0, 0.0f, 0.3f });
    const int neck = add("Neck", shoulders, { 0, 0.1f, 0.1f });
    add("Head", neck, { 0, 0.15f, 0.1f });
    const char* up[4] = { "FrontUpLeg.L", "FrontUpLeg.R", "BackUpLeg.L", "BackUpLeg.R" };
    const char* low[4] = { "FrontLowLeg.L", "FrontLowLeg.R", "BackLowLeg.L", "BackLowLeg.R" };
    const char* foot[4] = { "FrontFoot.L", "FrontFoot.R", "BackFoot.L", "BackFoot.R" };
    for (int i = 0; i < 4; ++i) {
        const float side = i % 2 == 0 ? 0.12f : -0.12f;
        const bool front = i < 2;
        const int parent = front ? shoulders : hips;
        const float hipY = front ? 0.62f : 0.6f;
        // Front knees bend forward a touch, hind hocks back, so IK knows which way.
        const int u = add(up[i], parent, { side, -0.02f, 0 });
        const int l = add(low[i], u, { 0, -hipY * 0.5f, front ? 0.18f : -0.18f });
        add(foot[i], l, { 0, -(hipY * 0.5f), front ? -0.18f : 0.18f });
    }
    return m;
}

kke::Pose rest(const kke::ModelData& m) { return kke::AnimationSet(m).restPose(); }
glm::vec3 posOf(const kke::ModelData& m, const kke::Pose& p, int b) { return glm::vec3(kke::poseToModel(m, p)[b][3]); }

auto flatGround(float y = 0.0f) {
    return [y](const glm::vec3& from, glm::vec3& hit, glm::vec3& normal) {
        hit = glm::vec3(from.x, y, from.z);
        normal = glm::vec3(0, 1, 0);
        return true;
    };
}

glm::mat4 bodyAt(const glm::vec3& p, float yawDegrees = 0.0f) {
    return glm::translate(glm::mat4(1.0f), p) * glm::mat4_cast(glm::angleAxis(glm::radians(yawDegrees), glm::vec3(0, 1, 0)));
}

} // namespace

// ------------------------------------------------------------- gait tables

TEST(ProceduralAnim, TrotMovesDiagonalPairsTogether) {
    const kke::GaitPattern trot = kke::gaitPattern(kke::Gait::Trot, 4);
    ASSERT_EQ(trot.phase.size(), 4u);
    EXPECT_FLOAT_EQ(trot.phase[0], trot.phase[3]); // FL with BR
    EXPECT_FLOAT_EQ(trot.phase[1], trot.phase[2]); // FR with BL
    EXPECT_NEAR(std::abs(trot.phase[0] - trot.phase[1]), 0.5f, 1e-6f);
    const kke::GaitPattern pace = kke::gaitPattern(kke::Gait::Pace, 4);
    EXPECT_FLOAT_EQ(pace.phase[0], pace.phase[2]); // FL with BL
    // A walk always has three feet down: duty 0.75, four distinct beats.
    const kke::GaitPattern walk = kke::gaitPattern(kke::Gait::Walk, 4);
    EXPECT_FLOAT_EQ(walk.duty, 0.75f);
    for (int i = 0; i < 4; ++i)
        for (int j = i + 1; j < 4; ++j) EXPECT_NE(walk.phase[i], walk.phase[j]);
}

TEST(ProceduralAnim, SixLegsRunOnAlternatingTripods) {
    const kke::GaitPattern t = kke::gaitPattern(kke::Gait::Tripod, 6);
    // L1, R2, L3 together; R1, L2, R3 together.
    EXPECT_FLOAT_EQ(t.phase[0], t.phase[3]);
    EXPECT_FLOAT_EQ(t.phase[0], t.phase[4]);
    EXPECT_FLOAT_EQ(t.phase[1], t.phase[2]);
    EXPECT_FLOAT_EQ(t.phase[1], t.phase[5]);
    EXPECT_NE(t.phase[0], t.phase[1]);
    // A wave lifts one leg at a time.
    const kke::GaitPattern w = kke::gaitPattern(kke::Gait::Wave, 8);
    EXPECT_EQ(w.gait, kke::Gait::Wave);
    EXPECT_NEAR(w.duty, 7.0f / 8.0f, 1e-6f);
}

TEST(ProceduralAnim, GaitChangesWithFroudeNumber) {
    // A 0.5 m dog and a 1.5 m horse both switch at the same Froude number,
    // so the horse is still walking at a speed the dog trots.
    EXPECT_EQ(kke::gaitForSpeed(1.0f, 0.5f, 4), kke::Gait::Walk);
    EXPECT_EQ(kke::gaitForSpeed(2.0f, 0.5f, 4), kke::Gait::Trot);
    EXPECT_EQ(kke::gaitForSpeed(2.0f, 1.5f, 4), kke::Gait::Walk);
    EXPECT_EQ(kke::gaitForSpeed(6.0f, 0.5f, 4), kke::Gait::Gallop);
    EXPECT_EQ(kke::gaitForSpeed(3.0f, 0.9f, 2), kke::Gait::Trot); // a person runs
    EXPECT_EQ(kke::gaitFromName("GALLOP"), kke::Gait::Gallop);
    EXPECT_EQ(kke::gaitFromName("nonsense"), kke::Gait::Auto);
}

// ------------------------------------------------------------- walking

TEST(ProceduralAnim, PlantedFeetStayPutWhileTheBodyMoves) {
    kke::ProceduralGait gait(kke::makeLegs(4, 0.8f, 0.3f, 0.5f));
    auto ground = flatGround();
    glm::vec3 p(0.0f);
    gait.reset(bodyAt(p), ground);
    const glm::vec3 v(0, 0, 1.2f);
    const float dt = 1.0f / 60.0f;
    std::vector<glm::vec3> last(4);
    std::vector<bool> wasPlanted(4, true);
    for (int i = 0; i < 4; ++i) last[i] = gait.foot(i).position;
    int landings = 0;
    for (int f = 0; f < 240; ++f) {
        p += v * dt;
        gait.update(bodyAt(p), v, 0.0f, ground, dt);
        for (int i = 0; i < 4; ++i) {
            const auto& foot = gait.foot(i);
            if (foot.planted && wasPlanted[i]) { EXPECT_LT(glm::length(foot.position - last[i]), 1e-5f) << "foot " << i << " slid"; }
            if (foot.landed) ++landings;
            EXPECT_GE(foot.position.y, -1e-4f);
            last[i] = foot.position;
            wasPlanted[i] = foot.planted;
        }
    }
    EXPECT_GT(landings, 8); // it actually walked
    // And kept up: every foot is near its hip, not left behind.
    for (int i = 0; i < 4; ++i) EXPECT_LT(glm::length(gait.foot(i).position - gait.foot(i).hip), 0.6f);
    EXPECT_EQ(gait.gait(), kke::Gait::Walk);
}

TEST(ProceduralAnim, StepsLandOnRaisedGroundAndTheBodyPitches) {
    kke::ProceduralGait gait(kke::makeLegs(4, 0.8f, 0.3f, 0.5f));
    // A ramp: 20 cm up per metre forward.
    auto ramp = [](const glm::vec3& from, glm::vec3& hit, glm::vec3& normal) {
        hit = glm::vec3(from.x, std::max(0.0f, from.z) * 0.2f, from.z);
        normal = glm::normalize(glm::vec3(0, 1, -0.2f));
        return true;
    };
    glm::vec3 p(0.0f);
    gait.reset(bodyAt(p), ramp);
    const glm::vec3 v(0, 0, 1.0f);
    for (int f = 0; f < 300; ++f) {
        p += v / 60.0f;
        p.y = std::max(0.0f, p.z) * 0.2f;
        gait.update(bodyAt(p), v, 0.0f, ramp, 1.0f / 60.0f);
    }
    for (int i = 0; i < 4; ++i) {
        const auto& f = gait.foot(i);
        if (!f.planted) continue;
        EXPECT_NEAR(f.position.y, std::max(0.0f, f.position.z) * 0.2f, 1e-3f);
    }
    // Nose up: the body's forward axis points up the slope (atan 0.2 = 11 deg).
    const glm::vec3 fwd = glm::normalize(glm::mat3(gait.bodyPose()) * glm::vec3(0, 0, 1));
    const float pitch = glm::degrees(std::asin(fwd.y));
    EXPECT_GT(pitch, 7.0f);
    EXPECT_LT(pitch, 14.0f);
}

TEST(ProceduralAnim, StandingStillSettlesAndStops) {
    kke::ProceduralGait gait(kke::makeLegs(6, 0.3f, 0.12f, 0.08f));
    auto ground = flatGround();
    glm::vec3 p(0.0f);
    gait.reset(bodyAt(p), ground);
    // Walk, stop, and give it a moment: every foot back under the body.
    for (int f = 0; f < 120; ++f) {
        p += glm::vec3(0.3f, 0, 0) / 60.0f;
        gait.update(bodyAt(p), glm::vec3(0.3f, 0, 0), 0.0f, ground, 1.0f / 60.0f);
    }
    for (int f = 0; f < 240; ++f) gait.update(bodyAt(p), glm::vec3(0.0f), 0.0f, ground, 1.0f / 60.0f);
    const float phase = gait.phase();
    for (int f = 0; f < 30; ++f) gait.update(bodyAt(p), glm::vec3(0.0f), 0.0f, ground, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(gait.phase(), phase); // no more stepping
    for (int i = 0; i < 6; ++i) {
        EXPECT_TRUE(gait.foot(i).planted);
        const glm::vec3 rest = glm::vec3(bodyAt(p) * glm::vec4(gait.leg(i).restFoot, 1.0f));
        EXPECT_LT(glm::length(gait.foot(i).position - rest), 0.12f * gait.leg(i).length() + 1e-3f);
    }
}

TEST(ProceduralAnim, AutoGaitGallopsWhenFast) {
    kke::ProceduralGait gait(kke::makeLegs(4, 0.8f, 0.3f, 0.5f));
    auto ground = flatGround();
    glm::vec3 p(0.0f);
    gait.reset(bodyAt(p), ground);
    const glm::vec3 v(0, 0, 6.0f);
    for (int f = 0; f < 120; ++f) {
        p += v / 60.0f;
        gait.update(bodyAt(p), v, 0.0f, ground, 1.0f / 60.0f);
    }
    EXPECT_EQ(gait.gait(), kke::Gait::Gallop);
    EXPECT_LT(gait.cycleSeconds(), 0.5f);
    for (int i = 0; i < 4; ++i) EXPECT_LT(glm::length(gait.foot(i).position - gait.foot(i).hip), 0.9f);
}

TEST(ProceduralAnim, LeansIntoTurns) {
    kke::ProceduralGait gait(kke::makeLegs(4, 0.8f, 0.3f, 0.5f));
    auto ground = flatGround();
    gait.reset(bodyAt(glm::vec3(0.0f)), ground);
    // Circling left at 3 m/s, 90 deg/s: the body's up tilts left (+X).
    float yaw = 0.0f;
    glm::vec3 p(0.0f);
    for (int f = 0; f < 120; ++f) {
        yaw += 90.0f / 60.0f;
        const glm::vec3 fwd(std::sin(glm::radians(yaw)), 0, std::cos(glm::radians(yaw)));
        p += fwd * 3.0f / 60.0f;
        gait.update(bodyAt(p, yaw), fwd * 3.0f, 90.0f, ground, 1.0f / 60.0f);
    }
    const glm::vec3 up = glm::mat3(glm::inverse(bodyAt(p, yaw)) * gait.bodyPose()) * glm::vec3(0, 1, 0);
    EXPECT_GT(up.x, 0.1f);
}

TEST(ProceduralAnim, KneeKeepsSegmentLengths) {
    const glm::vec3 hip(0, 1, 0), foot(0.1f, 0.2f, 0.3f);
    const glm::vec3 knee = kke::kneePosition(hip, foot, 0.5f, 0.45f, glm::vec3(0, 0, 1));
    EXPECT_NEAR(glm::length(knee - hip), 0.5f, 1e-4f);
    EXPECT_NEAR(glm::length(foot - knee), 0.45f, 1e-4f);
    EXPECT_GT(knee.z, 0.15f); // bent the way it was asked
}

// ------------------------------------------------------------- skeletons

TEST(ProceduralAnim, ApplyGaitPutsRigFeetOnThePlan) {
    const kke::ModelData m = animal();
    const auto chains = kke::quadrupedLegChains(m);
    for (const auto& c : chains) ASSERT_TRUE(c.valid());
    kke::ProceduralGait gait(kke::legsFromSkeleton(m, chains));
    ASSERT_EQ(gait.legCount(), 4);
    auto ground = flatGround();
    glm::vec3 p(0.0f);
    gait.reset(bodyAt(p), ground);
    for (int f = 0; f < 50; ++f) {
        p += glm::vec3(0, 0, 1.0f) / 60.0f;
        gait.update(bodyAt(p), glm::vec3(0, 0, 1.0f), 0.0f, ground, 1.0f / 60.0f);
    }
    kke::Pose pose = rest(m);
    kke::applyGait(m, pose, chains, 0, gait, bodyAt(p));
    for (int i = 0; i < 4; ++i) {
        const glm::vec3 want = gait.foot(i).position - p; // model space = world minus the body
        EXPECT_LT(glm::length(posOf(m, pose, chains[i].end) - want), 0.01f) << "leg " << i;
    }
}

TEST(ProceduralAnim, LegPlacerTiltsTheBodyOnASlope) {
    const kke::ModelData m = animal();
    kke::LegPlacer legs(kke::quadrupedLegChains(m), 0);
    ASSERT_TRUE(legs.valid());
    // Front feet on ground 10 cm higher than the hind feet.
    auto step = [](const glm::vec3& from, glm::vec3& hit, glm::vec3& normal) {
        hit = glm::vec3(from.x, from.z > 0.0f ? 0.05f : -0.05f, from.z);
        normal = glm::vec3(0, 1, 0);
        return true;
    };
    kke::Pose pose;
    for (int f = 0; f < 60; ++f) {
        pose = rest(m);
        legs.apply(m, pose, step, 1.0f / 60.0f);
    }
    EXPECT_GT(legs.pitchDegrees(), 3.0f);
    // Each foot keeps its animated height above the ground under it.
    const auto chains = kke::quadrupedLegChains(m);
    const kke::Pose still = rest(m);
    EXPECT_NEAR(posOf(m, pose, chains[0].end).y, posOf(m, still, chains[0].end).y + 0.05f, 0.01f);
    EXPECT_NEAR(posOf(m, pose, chains[3].end).y, posOf(m, still, chains[3].end).y - 0.05f, 0.01f);
}

TEST(ProceduralAnim, LookAtTurnsTheHeadWithinLimits) {
    const kke::ModelData m = animal();
    kke::LookAt look = kke::LookAt::quadruped(m);
    ASSERT_TRUE(look.valid());
    const int head = 4;
    // Something up and to the left (+X for a +Z-facing animal).
    const glm::vec3 target(3.0f, 1.5f, 1.5f);
    kke::Pose pose;
    for (int f = 0; f < 120; ++f) {
        pose = rest(m);
        look.apply(m, pose, &target, 1.0f / 60.0f);
    }
    EXPECT_GT(look.yawDegrees(), 40.0f);
    EXPECT_LE(look.yawDegrees(), look.settings().maxYaw + 1e-3f);
    const glm::vec3 fwd = glm::mat3(kke::poseToModel(m, pose)[head]) * glm::vec3(0, 0, 1);
    EXPECT_GT(fwd.x, 0.5f);
    // Directly behind: it gives up and comes back to forward.
    const glm::vec3 behind(0.0f, 0.8f, -5.0f);
    for (int f = 0; f < 240; ++f) {
        pose = rest(m);
        look.apply(m, pose, &behind, 1.0f / 60.0f);
    }
    EXPECT_NEAR(look.yawDegrees(), 0.0f, 0.5f);
}

TEST(ProceduralAnim, FabrikReachesAndKeepsLengths) {
    kke::ModelData m;
    m.bones.push_back(bone("Tail1", -1, { 0, 0, 0 }));
    for (int i = 2; i <= 5; ++i) {
        const std::string name = "Tail" + std::to_string(i);
        m.bones.push_back(bone(name.c_str(), i - 2, { 0, 0, -0.2f }));
    }
    const kke::IkChain tail = kke::findIkChain(m, "Tail1", "Tail5");
    ASSERT_EQ(tail.bones.size(), 5u);
    kke::Pose pose = rest(m);
    const glm::vec3 target(0.3f, 0.3f, -0.4f), pole(0.0f, 1.0f, 0.0f);
    kke::solveFabrik(m, pose, tail, target, &pole);
    EXPECT_LT(glm::length(posOf(m, pose, tail.bones.back()) - target), 0.01f);
    for (size_t i = 0; i + 1 < tail.bones.size(); ++i)
        EXPECT_NEAR(glm::length(posOf(m, pose, tail.bones[i + 1]) - posOf(m, pose, tail.bones[i])), 0.2f, 1e-3f);
    // Out of reach: straight toward it.
    kke::solveFabrik(m, pose, tail, glm::vec3(0, 5, 0));
    EXPECT_NEAR(posOf(m, pose, tail.bones.back()).y, 0.8f, 1e-3f);
}

TEST(ProceduralAnim, MaskedAndAdditiveLayers) {
    const kke::ModelData m = animal();
    const kke::BoneMask neck = kke::boneMask(m, { "Neck" });
    EXPECT_EQ(neck[3], 1.0f); // Neck
    EXPECT_EQ(neck[4], 1.0f); // Head, below it
    EXPECT_EQ(neck[0], 0.0f); // Hips
    kke::Pose base = rest(m), layer = rest(m);
    for (auto& b : layer) b.r = glm::angleAxis(0.5f, glm::vec3(1, 0, 0));
    kke::Pose out;
    kke::blendPosesMasked(base, layer, neck, 1.0f, out);
    EXPECT_NEAR(glm::angle(out[4].r), 0.5f, 1e-4f);
    EXPECT_NEAR(glm::angle(out[0].r), 0.0f, 1e-4f);
    // Additive: the change from the reference lands on top of the base.
    kke::Pose ref = rest(m), add = rest(m);
    add[0].t += glm::vec3(0, 0.1f, 0);
    add[0].r = glm::angleAxis(0.2f, glm::vec3(0, 1, 0));
    kke::addPose(layer, add, ref, {}, 0.5f, out);
    EXPECT_NEAR(out[0].t.y, ref[0].t.y + 0.05f, 1e-5f);
    EXPECT_NEAR(glm::angle(glm::inverse(layer[0].r) * out[0].r), 0.1f, 1e-4f);
}

// ------------------------------------------------------------- active ragdoll

namespace {

// Two bodies and a knee: enough for the state machine.
kke::RagdollDesc tinyRagdoll() {
    kke::RagdollDesc d;
    kke::RagdollBody pelvis, torso, leg;
    pelvis.name = "pelvis";
    pelvis.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0, 1.0f, 0));
    torso.name = "torso";
    torso.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0, 1.4f, 0));
    leg.name = "thigh_l";
    leg.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0.5f, 0));
    d.bodies = { pelvis, torso, leg };
    kke::RagdollJoint spine, hip;
    spine.name = "spine";
    spine.bodyA = 0;
    spine.bodyB = 1;
    hip.name = "hip_l";
    hip.bodyA = 0;
    hip.bodyB = 2;
    d.joints = { spine, hip };
    return d;
}

std::vector<glm::mat4> transforms(const kke::RagdollDesc& d) {
    std::vector<glm::mat4> t;
    for (const auto& b : d.bodies) t.push_back(b.transform);
    return t;
}

} // namespace

TEST(ProceduralAnim, ActiveRagdollStaggersAndRecovers) {
    const kke::RagdollDesc d = tinyRagdoll();
    kke::ActiveRagdoll a(d);
    ASSERT_TRUE(a.valid());
    EXPECT_FALSE(a.physical());
    EXPECT_TRUE(a.drive().targets.empty());
    a.hit(1, glm::vec3(2.0f, 0, 0)); // a light shove on the torso
    EXPECT_EQ(a.state(), kke::ActiveRagdoll::State::Active);
    EXPECT_LT(a.jointStrength(0), 1.0f); // the spine weakened most
    EXPECT_LT(a.jointStrength(0), a.jointStrength(1));
    const kke::RagdollDrive drive = a.drive();
    EXPECT_EQ(drive.targets.size(), 3u);
    EXPECT_EQ(drive.assistBodies, std::vector<int>{ 0 });
    EXPECT_GT(drive.assist, 0.0f);
    // Still standing where the animation wants it: recovers, then hands back.
    const auto bodies = transforms(d);
    for (int f = 0; f < 240 && a.physical(); ++f) a.update(1.0f / 60.0f, bodies);
    EXPECT_EQ(a.state(), kke::ActiveRagdoll::State::Animated);
}

TEST(ProceduralAnim, ActiveRagdollFallsWhenTippedAndGetsUp) {
    const kke::RagdollDesc d = tinyRagdoll();
    kke::ActiveRagdoll a(d);
    a.hit(1, glm::vec3(1.0f, 0, 0));
    // The torso ends up beside the pelvis instead of above it: fallen.
    auto bodies = transforms(d);
    bodies[1] = glm::translate(glm::mat4(1.0f), glm::vec3(0.4f, 1.0f, 0));
    a.update(1.0f / 60.0f, bodies);
    EXPECT_EQ(a.state(), kke::ActiveRagdoll::State::Fallen);
    EXPECT_EQ(a.jointStrength(0), 0.0f);
    EXPECT_EQ(a.drive().assist, 0.0f);
    for (int f = 0; f < 200; ++f) a.update(1.0f / 60.0f, bodies);
    EXPECT_NE(a.state(), kke::ActiveRagdoll::State::Fallen);
    for (int f = 0; f < 60; ++f) a.update(1.0f / 60.0f, bodies);
    EXPECT_EQ(a.state(), kke::ActiveRagdoll::State::Animated);
    EXPECT_FLOAT_EQ(a.getUpBlend(), 1.0f);
}

TEST(ProceduralAnim, BigHitKnocksDown) {
    const kke::RagdollDesc d = tinyRagdoll();
    kke::ActiveRagdoll a(d);
    a.hit(1, glm::vec3(0, 0, 9.0f)); // balance gone
    a.update(1.0f / 60.0f, transforms(d));
    EXPECT_EQ(a.state(), kke::ActiveRagdoll::State::Fallen);
}

TEST(ProceduralAnim, TargetsFromPoseInvertTheSkin) {
    kke::ModelData m;
    m.bones.push_back(bone("Pelvis", -1, { 0, 1, 0 }));
    kke::RagdollDesc d;
    kke::RagdollBody b;
    b.name = "pelvis";
    b.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0, 1.1f, 0));
    b.bones = { "Pelvis" };
    d.bodies = { b };
    const std::vector<glm::mat4> boneWorld = { glm::translate(glm::mat4(1.0f), glm::vec3(0, 1, 0)) };
    const kke::RagdollSkinBinding binding = kke::bindSkeletonToRagdoll(m, boneWorld, d);
    const glm::mat4 moved = glm::translate(glm::mat4(1.0f), glm::vec3(2, 1, 0)) * glm::mat4_cast(glm::angleAxis(0.5f, glm::vec3(0, 1, 0)));
    const auto targets = kke::ragdollTargetsFromPose(binding, { moved }, 1);
    // Skinning from the target gives the bone back.
    const auto skinned = kke::poseFromRagdoll(m, binding, targets, glm::mat4(1.0f));
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) EXPECT_NEAR(skinned[0][c][r], moved[c][r], 1e-4f);
}

TEST(ProceduralAnim, PoseFromModelUndoesPoseToModel) {
    kke::ModelData model;
    model.bones.resize(3);
    model.bones[1].parent = 0;
    model.bones[2].parent = 1;
    kke::Pose pose(3);
    pose[0].t = glm::vec3(1, 2, 3);
    pose[0].r = glm::angleAxis(0.4f, glm::normalize(glm::vec3(1, 1, 0)));
    pose[1].t = glm::vec3(0, 0.5f, 0);
    pose[1].r = glm::angleAxis(-1.2f, glm::vec3(0, 0, 1));
    pose[1].s = glm::vec3(1.5f);
    pose[2].t = glm::vec3(0.2f, 0.4f, 0);
    pose[2].r = glm::angleAxis(2.5f, glm::normalize(glm::vec3(0, 1, 1)));
    const kke::Pose back = kke::poseFromModel(model, kke::poseToModel(model, pose));
    ASSERT_EQ(back.size(), 3u);
    for (size_t b = 0; b < 3; ++b) {
        EXPECT_LT(glm::length(back[b].t - pose[b].t), 1e-4f) << b;
        EXPECT_LT(glm::length(back[b].s - pose[b].s), 1e-4f) << b;
        EXPECT_GT(std::abs(glm::dot(back[b].r, pose[b].r)), 0.9999f) << b;
    }
}
