#include "kke/MpmWater.h"
#include "kke/TaskPool.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {

// A small tank: 0.6 x 0.8 x 0.3 m, 5 cm cells.
kke::MpmWater::Params tank() {
    kke::MpmWater::Params p;
    p.boundsMin = glm::vec3(0.0f);
    p.boundsMax = glm::vec3(0.6f, 0.8f, 0.3f);
    p.cellSize = 0.05f;
    return p;
}

void run(kke::MpmWater& w, float seconds, float dt = 1.0f / 60.0f) {
    for (int i = 0; i < int(std::lround(seconds / dt)); ++i) w.step(dt);
}

// Height of the water's surface: the mean of the top particle in each
// column of the tank's floor plan.
float surfaceHeight(const kke::MpmWater& w, float cell) {
    const glm::vec3 lo = w.params().boundsMin, hi = w.params().boundsMax;
    const int nx = int((hi.x - lo.x) / cell), nz = int((hi.z - lo.z) / cell);
    std::vector<float> top(size_t(nx * nz), -1.0f);
    for (const glm::vec3& x : w.positions()) {
        const int i = std::clamp(int((x.x - lo.x) / cell), 0, nx - 1), k = std::clamp(int((x.z - lo.z) / cell), 0, nz - 1);
        top[size_t(k * nx + i)] = std::max(top[size_t(k * nx + i)], x.y);
    }
    float sum = 0.0f;
    int count = 0;
    for (float t : top)
        if (t >= 0.0f) {
            sum += t;
            ++count;
        }
    return count ? sum / float(count) : 0.0f;
}

bool sameState(const kke::MpmWater& a, const kke::MpmWater& b) {
    return a.size() == b.size() && std::memcmp(a.positions().data(), b.positions().data(), a.size() * sizeof(glm::vec3)) == 0 &&
           std::memcmp(a.velocities().data(), b.velocities().data(), a.size() * sizeof(glm::vec3)) == 0 &&
           std::memcmp(a.volumes().data(), b.volumes().data(), a.size() * sizeof(float)) == 0;
}

// A body the test moves itself (no rigid-body engine): gravity plus what
// the water did, standing on the tank's floor.
void moveBody(kke::MpmWater::Body& b, const glm::vec3& gravity, float dt) {
    b.velocity += b.deltaVelocity + gravity * dt;
    b.position += b.velocity * dt;
    if (b.position.y < b.halfExtents.y) {
        b.position.y = b.halfExtents.y;
        b.velocity.y = std::max(b.velocity.y, 0.0f);
        b.velocity.x *= 0.9f;
        b.velocity.z *= 0.9f;
    }
}

} // namespace

TEST(MpmWater, ADamBreakSettlesKeepingItsWaterAndVolume) {
    kke::MpmWater w(tank());
    const size_t n = w.fill(glm::vec3(0.0f), glm::vec3(0.3f, 0.4f, 0.3f));
    EXPECT_EQ(n, size_t(12 * 16 * 12));
    run(w, 7.0f);
    ASSERT_EQ(w.size(), n);
    for (const glm::vec3& x : w.positions()) {
        ASSERT_TRUE(std::isfinite(x.x) && std::isfinite(x.y) && std::isfinite(x.z));
        ASSERT_GE(x.x, 0.0f);
        ASSERT_LE(x.x, 0.6f);
        ASSERT_GE(x.y, 0.0f);
        ASSERT_LE(x.z, 0.3f);
    }
    // Calming down (APIC keeps the slosh going a while; nothing flies
    // off), spread over the whole floor: 0.3 x 0.4 m of water over 0.6 m
    // is 0.2 m deep (a little less: weakly compressible, and the top
    // particles sit a quarter cell under the surface).
    float meanSpeed = 0.0f;
    for (const glm::vec3& v : w.velocities()) meanSpeed += glm::length(v);
    meanSpeed /= float(w.size());
    EXPECT_LT(meanSpeed, 0.15f);
    EXPECT_LT(w.stats().maxSpeed, 0.5f);
    const float h = surfaceHeight(w, 0.05f);
    EXPECT_GT(h, 0.16f);
    EXPECT_LT(h, 0.22f);
    // Squeezed by its own weight, a few percent at most.
    float mean = 0.0f;
    for (float j : w.volumes()) mean += j;
    mean /= float(w.size());
    EXPECT_GT(mean, 0.95f);
    EXPECT_LT(mean, 1.02f);
}

TEST(MpmWater, SameResultOnAnyNumberOfThreads) {
    auto make = [] {
        auto w = std::make_unique<kke::MpmWater>(tank());
        w->fill(glm::vec3(0.0f), glm::vec3(0.3f, 0.5f, 0.3f));
        kke::MpmWater::Body box;
        box.position = glm::vec3(0.45f, 0.3f, 0.15f);
        box.halfExtents = glm::vec3(0.06f);
        box.mass = 1.0f;
        w->bodies().push_back(box);
        return w;
    };
    auto one = make(), four = make();
    kke::TaskPool pool1(1), pool4(4);
    one->setTaskPool(&pool1);
    four->setTaskPool(&pool4);
    for (int i = 0; i < 40; ++i) {
        one->step(1.0f / 60.0f);
        four->step(1.0f / 60.0f);
        moveBody(one->bodies()[0], one->params().gravity, 1.0f / 60.0f);
        moveBody(four->bodies()[0], four->params().gravity, 1.0f / 60.0f);
    }
    EXPECT_TRUE(sameState(*one, *four));
    EXPECT_EQ(std::memcmp(&one->bodies()[0].position, &four->bodies()[0].position, sizeof(glm::vec3)), 0);
}

TEST(MpmWater, ALightBoxFloatsAndAHeavyOneSinks) {
    for (float density : { 400.0f, 3000.0f }) {
        kke::MpmWater w(tank());
        w.fill(glm::vec3(0.0f), glm::vec3(0.6f, 0.3f, 0.3f));
        kke::MpmWater::Body box;
        box.halfExtents = glm::vec3(0.08f);
        box.position = glm::vec3(0.3f, 0.45f, 0.15f); // dropped from above the water
        box.mass = density * 0.16f * 0.16f * 0.16f;
        w.bodies().push_back(box);
        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 300; ++i) {
            w.step(dt);
            moveBody(w.bodies()[0], w.params().gravity, dt);
        }
        const kke::MpmWater::Body& b = w.bodies()[0];
        const float level = surfaceHeight(w, 0.05f);
        if (density < 1000.0f) {
            // Floating: 40% of it under the surface (a little more or less
            // with the coarse grid), and nearly still.
            const float under = (level - (b.position.y - b.halfExtents.y)) / (2.0f * b.halfExtents.y);
            EXPECT_GT(under, 0.15f) << "level " << level << ", box at " << b.position.y;
            EXPECT_LT(under, 0.75f) << "level " << level << ", box at " << b.position.y;
            EXPECT_LT(std::fabs(b.velocity.y), 0.15f);
        } else {
            EXPECT_LT(b.position.y, b.halfExtents.y + 0.02f) << "a heavy box sinks to the floor";
        }
    }
}

TEST(MpmWater, AStreamPushesABodyAlong) {
    kke::MpmWater::Params p = tank();
    p.gravity = glm::vec3(0.0f);
    kke::MpmWater w(p);
    w.fill(glm::vec3(0.0f, 0.3f, 0.05f), glm::vec3(0.2f, 0.5f, 0.25f), glm::vec3(2.0f, 0.0f, 0.0f));
    kke::MpmWater::Body box;
    box.position = glm::vec3(0.4f, 0.4f, 0.15f);
    box.halfExtents = glm::vec3(0.05f);
    box.mass = 0.5f;
    w.bodies().push_back(box);
    float pushed = 0.0f;
    for (int i = 0; i < 20; ++i) {
        w.step(1.0f / 60.0f);
        pushed += w.bodies()[0].deltaVelocity.x;
    }
    EXPECT_GT(pushed, 0.3f);
    // A body the water can't move (mass 0) gets nothing, and stops it.
    kke::MpmWater wall(p);
    wall.fill(glm::vec3(0.0f, 0.3f, 0.05f), glm::vec3(0.2f, 0.5f, 0.25f), glm::vec3(2.0f, 0.0f, 0.0f));
    box.mass = 0.0f;
    box.halfExtents = glm::vec3(0.05f, 0.5f, 0.3f);
    wall.bodies().push_back(box);
    run(wall, 0.5f);
    EXPECT_EQ(wall.bodies()[0].deltaVelocity, glm::vec3(0.0f));
    for (const glm::vec3& x : wall.positions()) ASSERT_LT(x.x, 0.4f - 0.05f + 1e-4f);
}

TEST(MpmWater, RemoveIfAndClear) {
    kke::MpmWater w(tank());
    w.fill(glm::vec3(0.0f), glm::vec3(0.2f, 0.2f, 0.2f));
    const size_t n = w.size();
    w.removeIf([&](size_t i) { return w.positions()[i].x < 0.1f; });
    EXPECT_LT(w.size(), n);
    EXPECT_GT(w.size(), 0u);
    for (const glm::vec3& x : w.positions()) EXPECT_GE(x.x, 0.1f);
    w.step(1.0f / 60.0f);
    w.clear();
    EXPECT_EQ(w.size(), 0u);
    w.step(1.0f / 60.0f); // nothing to do, nothing breaks
}

TEST(MpmWater, ATankTooBigForTheGridBudgetIsRefused) {
    kke::MpmWater::Params p;
    p.boundsMin = glm::vec3(-50.0f);
    p.boundsMax = glm::vec3(50.0f);
    p.cellSize = 0.05f;
    EXPECT_THROW(kke::MpmWater w(p), std::invalid_argument);
}
