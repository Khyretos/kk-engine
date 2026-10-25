#include "kke/ParticleFluid.h"
#include "kke/TaskPool.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace {

// A 6x6x6 block of particles at lattice spacing, dropped into a 0.8 m box.
kke::ParticleFluid makeBlock() {
    kke::ParticleFluid::Params p;
    p.radius = 0.04f;
    p.boundsMin = glm::vec3(-0.4f, -1.0f, -0.4f);
    p.boundsMax = glm::vec3(0.4f, 10.0f, 0.4f);
    kke::ParticleFluid f(p, 1000);
    for (int x = 0; x < 6; ++x)
        for (int y = 0; y < 6; ++y)
            for (int z = 0; z < 6; ++z) f.add(glm::vec3(x - 2.5f, y + 2.0f, z - 2.5f) * 0.08f, glm::vec3(0.0f), 20.0f);
    return f;
}

} // namespace

TEST(ParticleFluid, SettlesInsideContainerWithoutExploding) {
    kke::ParticleFluid f = makeBlock();
    for (int i = 0; i < 180; ++i) f.step(1.0f / 60.0f); // 3 s
    float maxSpeed = 0.0f, minY = 1e9f;
    for (size_t i = 0; i < f.size(); ++i) {
        maxSpeed = std::max(maxSpeed, glm::length(f.velocities()[i]));
        minY = std::min(minY, f.positions()[i].y);
        ASSERT_TRUE(std::isfinite(f.positions()[i].x));
    }
    EXPECT_GE(minY, f.params().radius - 1e-4f);  // never through the ground
    EXPECT_LT(maxSpeed, 1.0f);                    // came to rest-ish
    // Incompressibility: nobody squeezed far past rest density.
    float maxRho = *std::max_element(f.densities().begin(), f.densities().end());
    EXPECT_LT(maxRho, f.params().restDensity * 1.3f);
}

TEST(ParticleFluid, CollidersKeepParticlesOut) {
    kke::ParticleFluid f = makeBlock();
    const glm::vec3 c(0.0f, 0.2f, 0.0f);
    const float R = 0.2f;
    f.addCollider([&](const glm::vec3& p, glm::vec3& n) {
        glm::vec3 d = p - c;
        float len = glm::length(d);
        n = len > 1e-6f ? d / len : glm::vec3(0, 1, 0);
        return len - R;
    });
    for (int i = 0; i < 120; ++i) f.step(1.0f / 60.0f);
    for (const glm::vec3& p : f.positions()) EXPECT_GE(glm::length(p - c), R + f.params().radius - 0.01f);
}

TEST(ParticleFluid, CoolsTowardAmbientAndFreezes) {
    // Two identical falling particles; only one of them can solidify.
    auto run = [](float solidify, float& temperature) {
        kke::ParticleFluid::Params p;
        p.coolingRate = 2.0f;
        p.ambientTemperature = 20.0f;
        p.solidifyTemperature = solidify;
        kke::ParticleFluid f(p, 10);
        f.add(glm::vec3(0, 100, 0), glm::vec3(1, 0, 0), 1200.0f); // high up: falling the whole time
        for (int i = 0; i < 60; ++i) f.step(1.0f / 60.0f);
        temperature = f.temperatures()[0];
        float y0 = f.positions()[0].y;
        for (int i = 0; i < 30; ++i) f.step(1.0f / 60.0f);
        return y0 - f.positions()[0].y;
    };
    float tSolid = 0.0f, tFree = 0.0f;
    float fellSolid = run(500.0f, tSolid);
    float fellFree = run(-1e9f, tFree);
    EXPECT_LT(tSolid, 500.0f);                  // cooled toward ambient
    EXPECT_GT(fellSolid, 0.0f);                 // solid, but not pinned in mid-air
    EXPECT_LT(fellSolid, fellFree * 0.5f);      // heavily damped compared to liquid
}

TEST(ParticleFluid, CapacityIsABudget) {
    kke::ParticleFluid f(kke::ParticleFluid::Params{}, 3);
    EXPECT_TRUE(f.add(glm::vec3(0), glm::vec3(0), 0));
    EXPECT_TRUE(f.add(glm::vec3(1), glm::vec3(0), 0));
    EXPECT_TRUE(f.add(glm::vec3(2), glm::vec3(0), 0));
    EXPECT_FALSE(f.add(glm::vec3(3), glm::vec3(0), 0));
    f.removeIf([&](size_t i) { return f.positions()[i].x > 0.5f; });
    EXPECT_EQ(f.size(), 1u);
}

namespace {

// A 1,331-particle column falling into a box: above the 1,024 particles
// from which a neighbour search set on the fluid is used.
kke::ParticleFluid makeColumn() {
    kke::ParticleFluid::Params p;
    p.radius = 0.04f;
    p.boundsMin = glm::vec3(-0.5f, -1.0f, -0.5f);
    p.boundsMax = glm::vec3(0.5f, 10.0f, 0.5f);
    kke::ParticleFluid f(p, 2000);
    for (int x = 0; x < 11; ++x)
        for (int y = 0; y < 11; ++y)
            for (int z = 0; z < 11; ++z) f.add(glm::vec3(x - 5.0f, y + 1.0f, z - 5.0f) * 0.08f, glm::vec3(0.0f), 20.0f + x * 50.0f);
    return f;
}

// Everything a caller can read, bit for bit.
bool sameState(const kke::ParticleFluid& a, const kke::ParticleFluid& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::memcmp(&a.positions()[i], &b.positions()[i], sizeof(glm::vec3)) != 0) return false;
        if (std::memcmp(&a.velocities()[i], &b.velocities()[i], sizeof(glm::vec3)) != 0) return false;
        if (a.temperatures()[i] != b.temperatures()[i]) return false;
        if (a.densities()[i] != b.densities()[i]) return false;
        if (a.materials()[i] != b.materials()[i]) return false;
    }
    return true;
}

// shaders/fluid_neighbours.comp, line for line on the CPU, as a search set
// on the fluid: pins the shader's algorithm, and the fluid's use of the
// lists it returns, to the CPU's own search.
struct ShaderCopy : kke::FluidNeighbourSearch {
    uint32_t needStride = 0; // answer only once stride is at least this (0: always)
    int calls = 0;
    bool search(const Query& q, uint32_t& stride, std::vector<uint32_t>& list, std::vector<uint32_t>& counts) override {
        ++calls;
        auto hashCell = [](glm::ivec3 c, uint32_t mask) {
            return ((uint32_t(c.x) * 73856093u) ^ (uint32_t(c.y) * 19349663u) ^ (uint32_t(c.z) * 83492791u)) & mask;
        };
        list.assign(q.count * stride, 0);
        counts.assign(q.count, 0);
        uint32_t most = 0;
        for (uint32_t s = 0; s < q.count; ++s) {
            const glm::vec3 p = q.positions[s];
            const glm::ivec3 c = glm::ivec3(glm::floor(p * q.invCellSize));
            const uint32_t mask = q.tableSize - 1;
            uint32_t seen[27];
            int nSeen = 0;
            uint32_t found = 0;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const uint32_t k = hashCell(c + glm::ivec3(dx, dy, dz), mask);
                        const uint32_t b0 = q.cellStart[k], b1 = q.cellStart[k + 1];
                        if (b0 == b1) continue;
                        bool dup = false;
                        for (int i = 0; i < nSeen; ++i)
                            if (seen[i] == k) dup = true;
                        if (dup) continue;
                        seen[nSeen++] = k;
                        for (uint32_t t = b0; t < b1; ++t) {
                            if (t == s) continue;
                            const glm::vec3 r = p - q.positions[t];
                            const float d2 = (r.x * r.x + r.y * r.y) + r.z * r.z;
                            if (d2 < q.radius2) {
                                if (found < stride) list[s * stride + found] = t;
                                ++found;
                            }
                        }
                    }
            counts[s] = found;
            most = std::max(most, found);
        }
        if (most > stride || stride < needStride) {
            stride = std::max(stride + stride / 2, std::max(most + 8, needStride));
            return false;
        }
        return true;
    }
};

struct Declines : kke::FluidNeighbourSearch {
    int calls = 0;
    bool search(const Query&, uint32_t&, std::vector<uint32_t>&, std::vector<uint32_t>&) override {
        ++calls;
        return false;
    }
};

} // namespace

TEST(ParticleFluid, SameResultOnAnyNumberOfThreads) {
    kke::ParticleFluid one = makeColumn(), four = makeColumn();
    kke::TaskPool pool(4);
    four.setTaskPool(&pool);
    for (int i = 0; i < 90; ++i) {
        one.step(1.0f / 60.0f);
        four.step(1.0f / 60.0f);
    }
    EXPECT_TRUE(sameState(one, four));
}

TEST(ParticleFluid, NeighbourSearchElsewhereGivesTheSameResult) {
    kke::ParticleFluid cpu = makeColumn(), elsewhere = makeColumn();
    auto search = std::make_shared<ShaderCopy>();
    elsewhere.setNeighbourSearch(search);
    for (int i = 0; i < 90; ++i) {
        cpu.step(1.0f / 60.0f);
        elsewhere.step(1.0f / 60.0f);
    }
    EXPECT_TRUE(sameState(cpu, elsewhere));
    EXPECT_GT(elsewhere.searchStats().elsewhere, 0u);
    EXPECT_GT(search->calls, 0);
}

TEST(ParticleFluid, NeighbourSearchGrowsItsListsWhenTheyAreTooShort) {
    kke::ParticleFluid cpu = makeColumn(), elsewhere = makeColumn();
    auto search = std::make_shared<ShaderCopy>();
    search->needStride = 100; // declines at the default 64, answers once grown
    elsewhere.setNeighbourSearch(search);
    for (int i = 0; i < 30; ++i) {
        cpu.step(1.0f / 60.0f);
        elsewhere.step(1.0f / 60.0f);
    }
    EXPECT_TRUE(sameState(cpu, elsewhere));
    EXPECT_GE(elsewhere.searchStats().declined, 1u);
    EXPECT_GT(elsewhere.searchStats().elsewhere, 0u);
}

TEST(ParticleFluid, ADecliningSearchBacksOffToTheCpu) {
    kke::ParticleFluid cpu = makeColumn(), elsewhere = makeColumn();
    auto search = std::make_shared<Declines>();
    elsewhere.setNeighbourSearch(search);
    for (int i = 0; i < 60; ++i) {
        cpu.step(1.0f / 60.0f);
        elsewhere.step(1.0f / 60.0f);
    }
    EXPECT_TRUE(sameState(cpu, elsewhere));
    EXPECT_EQ(elsewhere.searchStats().elsewhere, 0u);
    // Three tries, then one per timing round (every 64th substep): not 60.
    EXPECT_LE(search->calls, 4);
}

TEST(ParticleFluid, SmallLiquidsKeepTheSearchOnTheCpu) {
    kke::ParticleFluid f = makeBlock(); // 216 particles
    auto search = std::make_shared<ShaderCopy>();
    f.setNeighbourSearch(search);
    for (int i = 0; i < 10; ++i) f.step(1.0f / 60.0f);
    EXPECT_EQ(search->calls, 0);
}
