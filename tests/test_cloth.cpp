#include "kke/Cloth.h"
#include "kke/RigidWorld.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace {

kke::RigidWorld::Settings single() {
    kke::RigidWorld::Settings s;
    s.threads = 0;
    return s;
}

void ground(kke::RigidWorld& w) {
    kke::RigidWorld::BodyDesc g;
    g.motion = kke::RigidWorld::Motion::Static;
    g.halfExtents = glm::vec3(20.0f, 0.5f, 20.0f);
    g.position = glm::vec3(0.0f, -0.5f, 0.0f);
    w.add(g);
}

float lowest(const std::vector<glm::vec3>& p) {
    float y = FLT_MAX;
    for (const glm::vec3& v : p) y = std::min(y, v.y);
    return y;
}

void run(kke::RigidWorld& w, float seconds) {
    for (int i = 0; i < int(seconds * 60.0f); ++i) w.step(1.0f / 60.0f);
}

} // namespace

TEST(Cloth, FabricPresetsDiffer) {
    const kke::Fabric silk = kke::clothFabric("silk"), denim = kke::clothFabric("denim"), wool = kke::clothFabric("wool");
    EXPECT_LT(silk.density, denim.density);
    EXPECT_GT(silk.bend, denim.bend); // silk drapes, denim holds its shape
    EXPECT_GT(wool.thickness, silk.thickness); // wool is fluffy
    EXPECT_EQ(kke::clothFabric("no such fabric").name, "cotton");
    for (const std::string& n : kke::clothFabricNames()) EXPECT_EQ(kke::clothFabric(n).name, n);
}

TEST(Cloth, GridAndNormals) {
    kke::ClothMesh m = kke::clothGrid(glm::vec3(0, 1, 0), 1.0f, 1.0f, 5, 4, glm::vec3(1, 0, 0), glm::vec3(0, 0, 1));
    ASSERT_EQ(m.positions.size(), 20u);
    EXPECT_EQ(m.indices.size(), size_t(4 * 3 * 6));
    std::vector<glm::vec3> n;
    kke::clothNormals(m.positions, m.indices, n);
    for (const glm::vec3& v : n) EXPECT_NEAR(std::fabs(v.y), 1.0f, 1e-4f); // lying flat
    kke::ClothMesh net = kke::clothNet(glm::vec3(0), 1.0f, 1.0f, 3, 3);
    EXPECT_EQ(net.lines.size(), size_t(12 * 2));
}

TEST(Cloth, HangsFromItsPinsWithoutStretching) {
    kke::RigidWorld w(single());
    kke::ClothDesc d;
    d.mesh = kke::clothGrid(glm::vec3(0, 2, 0), 1.0f, 1.0f, 12, 12, glm::vec3(1, 0, 0), glm::vec3(0, 0, 1)); // flat, will swing down
    d.fabric = kke::clothFabric("cotton");
    for (int x = 0; x < d.mesh.columns; ++x) d.pinned.push_back(kke::clothGridIndex(d.mesh, x, 0));
    auto id = w.addCloth(d);
    ASSERT_NE(id, 0u);
    run(w, 3.0f);
    std::vector<glm::vec3> p;
    ASSERT_TRUE(w.clothPositions(id, p));
    // Pins stayed.
    for (uint32_t i : d.pinned) EXPECT_NEAR(glm::length(p[i] - d.mesh.positions[i]), 0.0f, 1e-3f);
    // It swung down to hang, and no thread is much longer than woven.
    EXPECT_LT(lowest(p), 1.2f);
    const float bottomToTop = glm::length(p[kke::clothGridIndex(d.mesh, 6, 11)] - p[kke::clothGridIndex(d.mesh, 6, 0)]);
    EXPECT_LT(bottomToTop, 1.0f * d.fabric.maxStretch + 0.01f);
}

TEST(Cloth, RestsOnABoxNotInIt) {
    kke::RigidWorld w(single());
    ground(w);
    kke::RigidWorld::BodyDesc box;
    box.motion = kke::RigidWorld::Motion::Static;
    box.halfExtents = glm::vec3(0.3f);
    box.position = glm::vec3(0.0f, 0.3f, 0.0f);
    w.add(box);
    kke::ClothDesc d;
    d.mesh = kke::clothGrid(glm::vec3(0, 1.2f, 0), 1.2f, 1.2f, 20, 20, glm::vec3(1, 0, 0), glm::vec3(0, 0, 1));
    d.fabric = kke::clothFabric("cotton");
    auto id = w.addCloth(d);
    run(w, 4.0f);
    std::vector<glm::vec3> p;
    w.clothPositions(id, p);
    int onTop = 0;
    for (const glm::vec3& v : p) {
        const bool overBox = std::fabs(v.x) < 0.28f && std::fabs(v.z) < 0.28f;
        if (!overBox) continue;
        ++onTop;
        EXPECT_GT(v.y, 0.6f); // top of the box, never inside
    }
    EXPECT_GT(onTop, 10);
    for (const glm::vec3& v : p) EXPECT_GT(v.y, 0.0f); // and nothing through the floor
}

TEST(Cloth, FullProtectionKeepsClothOnTopOfCloth) {
    // A sheet lies on the floor; a second is dropped onto it. Jolt alone
    // has no cloth-vs-cloth collision: both would sink to the floor and
    // be drawn through each other. Full protection keeps them apart.
    for (auto level : { kke::ClothProtection::Full, kke::ClothProtection::Basic }) {
        kke::RigidWorld w(single());
        ground(w);
        kke::ClothDesc bottom;
        bottom.mesh = kke::clothGrid(glm::vec3(0, 0.02f, 0), 1.0f, 1.0f, 16, 16, glm::vec3(1, 0, 0), glm::vec3(0, 0, 1));
        bottom.fabric = kke::clothFabric("cotton");
        bottom.protection = level;
        kke::ClothDesc top = bottom;
        top.mesh = kke::clothGrid(glm::vec3(0.03f, 0.5f, 0.02f), 0.8f, 0.8f, 13, 13, glm::vec3(1, 0, 0), glm::vec3(0, 0, 1));
        auto a = w.addCloth(bottom);
        run(w, 0.5f);
        auto b = w.addCloth(top);
        run(w, 3.0f);
        std::vector<glm::vec3> pa, pb;
        w.clothPositions(a, pa);
        w.clothPositions(b, pb);
        // For every top vertex over the bottom sheet: how far above the
        // bottom sheet's nearest vertex it is.
        float worst = FLT_MAX;
        for (const glm::vec3& v : pb) {
            float best = FLT_MAX, dy = 0.0f;
            for (const glm::vec3& u : pa) {
                const float dxz = glm::length(glm::vec2(v.x - u.x, v.z - u.z));
                if (dxz < best) { best = dxz; dy = v.y - u.y; }
            }
            worst = std::min(worst, dy);
        }
        if (level == kke::ClothProtection::Full) {
            EXPECT_GT(worst, 0.5f * bottom.fabric.thickness) << "the top sheet sank into the bottom one";
            EXPECT_GT(w.clothStats(b).selfContacts + w.clothStats(a).selfContacts + 1u, 0u);
        } else {
            EXPECT_LT(worst, 0.5f * bottom.fabric.thickness) << "Basic has no cloth-vs-cloth: the sheets end up level";
        }
    }
}

TEST(Cloth, SilkFloatsDownSlowerThanDenim) {
    kke::RigidWorld w(single());
    auto drop = [&](const char* fabric, float x) {
        kke::ClothDesc d;
        d.mesh = kke::clothGrid(glm::vec3(x, 5.0f, 0), 1.0f, 1.0f, 10, 10, glm::vec3(1, 0, 0), glm::vec3(0, 0, 1));
        d.fabric = kke::clothFabric(fabric);
        return w.addCloth(d);
    };
    auto silk = drop("silk", -2.0f), denim = drop("denim", 2.0f);
    run(w, 0.8f);
    std::vector<glm::vec3> ps, pd;
    w.clothPositions(silk, ps);
    w.clothPositions(denim, pd);
    auto mean = [](const std::vector<glm::vec3>& p) { float y = 0; for (auto& v : p) y += v.y; return y / float(p.size()); };
    EXPECT_GT(mean(ps), mean(pd) + 0.3f);
}

TEST(Cloth, WindBlowsAFlag) {
    kke::RigidWorld w(single());
    kke::ClothDesc d;
    d.mesh = kke::clothGrid(glm::vec3(0.5f, 2.0f, 0), 1.0f, 0.6f, 12, 8, glm::vec3(1, 0, 0), glm::vec3(0, -1, 0));
    d.fabric = kke::clothFabric("cotton");
    for (int y = 0; y < d.mesh.rows; ++y) d.pinned.push_back(kke::clothGridIndex(d.mesh, 0, y)); // the pole
    auto id = w.addCloth(d);
    w.setWind(glm::vec3(8.0f, 0.0f, 0.0f));
    run(w, 3.0f);
    std::vector<glm::vec3> p;
    w.clothPositions(id, p);
    // Flying out along the wind rather than hanging limp down the pole.
    const glm::vec3 tip = p[kke::clothGridIndex(d.mesh, 11, 4)];
    EXPECT_GT(tip.x, 0.6f);
}

TEST(Cloth, CharactersPushCurtainsAsideAndRaysIgnoreCloth) {
    kke::RigidWorld w(single());
    ground(w);
    kke::ClothDesc d;
    d.mesh = kke::clothGrid(glm::vec3(0, 1.1f, 0), 1.6f, 2.0f, 16, 20, glm::vec3(1, 0, 0), glm::vec3(0, -1, 0));
    d.fabric = kke::clothFabric("linen");
    for (int x = 0; x < d.mesh.columns; ++x) d.pinned.push_back(kke::clothGridIndex(d.mesh, x, 0));
    auto curtain = w.addCloth(d);
    kke::RigidWorld::CharacterDesc cd;
    cd.position = glm::vec3(0, 0, -1.0f);
    auto ch = w.addCharacter(cd);
    EXPECT_EQ(w.bodyCount(), 1u); // the ground: characters' cloth colliders and cloth aren't rigid bodies
    // Rays go through cloth and characters' cloth colliders to the floor.
    const auto hit = w.raycast(glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(0, -1, 0), 10.0f);
    ASSERT_TRUE(hit.hit);
    EXPECT_NEAR(hit.point.y, 0.0f, 0.01f);
    run(w, 1.0f);
    kke::RigidWorld::CharacterInput in;
    in.move = glm::vec3(0, 0, 1.2f);
    for (int i = 0; i < 60; ++i) {
        w.setCharacterInput(ch, in);
        w.step(1.0f / 60.0f);
    }
    // It walked through (a curtain doesn't stop a player) and pushed the cloth.
    EXPECT_GT(w.characterPosition(ch).z, -0.2f);
    std::vector<glm::vec3> p;
    w.clothPositions(curtain, p);
    float pushed = 0.0f;
    for (const glm::vec3& v : p) pushed = std::max(pushed, v.z);
    EXPECT_GT(pushed, 0.1f);
}

TEST(Cloth, RemoveAndReset) {
    kke::RigidWorld w(single());
    ground(w);
    kke::ClothDesc d;
    d.mesh = kke::clothGrid(glm::vec3(0, 1, 0), 1.0f, 1.0f, 8, 8, glm::vec3(1, 0, 0), glm::vec3(0, 0, 1));
    auto id = w.addCloth(d);
    EXPECT_EQ(w.clothCount(), 1u);
    run(w, 1.0f);
    w.resetCloth(id);
    std::vector<glm::vec3> p;
    w.clothPositions(id, p);
    EXPECT_NEAR(p[0].y, 1.0f, 1e-4f);
    w.removeCloth(id);
    EXPECT_EQ(w.clothCount(), 0u);
    EXPECT_FALSE(w.clothPositions(id, p));
}
