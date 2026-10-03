// Crumpling bodies on FEMFX (the build's KKE_ENABLE_FEMFX): each car's body
// is also a soft, plastic solid in AMD FEMFX (a box of tetrahedra the size
// of the car, sheet steel's yield). Jolt still drives the car; a hit is
// replayed into the solid as a shove where it landed, the way it was
// pushed, and FEMFX works out how the metal gives: the dent spreads,
// the panel buckles round it, a hard enough one folds the corner in, and
// it stays (plastic). The Synty body is skinned to the solid
// (kke::embedPoints, PhysicsModule::deformEmbedded), so the car on screen
// is the car FEMFX crumpled.
//
// The solids sit on FEMFX's ground far from the track, one per car, each
// held still while it moves: every step whatever moves it as a whole (the
// shove's push) is taken out and only the change of shape is left, the
// car's own frame. They are awake only for a moment after a hit (FEMFX
// then puts them to sleep), so a pack of cars costs nothing until they
// touch.
//
// The solid's cells are ~60 cm, too coarse for the crease right where a
// bumper hit, so dent() also presses that in by hand (Damage.cpp) and
// it's laid back on top of every shape read from here. Without FEMFX
// (Android, a build without it) the hand-pressed dents are all there is.

#include "RacingModule.h"

#if KKE_ENABLE_FEMFX
#include "kke/Log.h"
#include "kke/Material.h"
#include "kke/VoxelTets.h"
#include "kke/modules/PhysicsModule.h"

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cmath>
#endif

namespace racing {

#if KKE_ENABLE_FEMFX

namespace {

constexpr float kSpacing = 20.0f;                        // m between the solids
const glm::vec3 kShellYard(3000.0f, 0.0f, 3000.0f);      // where they are: nowhere near anything, on FEMFX's ground

// Sheet steel on a frame, lumped into a solid: stiff enough to hold its
// shape at speed, a low yield so a hit leaves a dent, creep so the dent
// stays. Tuned with KKE_RACE_CRASH=1 (README.md "Crumpling").
kke::Material bodyMaterial() {
    kke::Material m;
    m.density = 900.0f;          // a car is mostly air: 1300 kg over ~8 m^3 of box is ~160, but FEMFX wants some mass to push against
    m.stiffness = 2.5e7f;
    m.poissonsRatio = 0.3f;
    m.fractureStressThreshold = 1.0e9f; // it folds, it doesn't break
    m.plasticYieldThreshold = 1500.0f;
    m.plasticCreep = 0.6f;
    return m;
}

} // namespace

struct RacingModule::Shell {
    kke::PhysicsModule::ObjectHandle handle = kke::PhysicsModule::kInvalidHandle;
    glm::vec3 anchor{0.0f};       // where its rest centre is
    glm::vec3 center{0.0f};       // the car's bounds' centre, car space (the solid's origin)
    kke::TetEmbedding embedding;  // every body vertex, all parts in a row
    std::vector<glm::vec3> restNormals;
    std::vector<size_t> partSizes;
    std::vector<glm::vec3> positions, normals; // scratch
    bool reading = false;         // awake: read its shape back each step
};

bool RacingModule::crumpleOn() const { return m_femfx != nullptr && m_crumple; }

void RacingModule::makeShell(Car& c, int slot) {
    c.shell.reset();
    if (!crumpleOn() || !c.art) return;
    const CarArt& art = *c.art;
    auto shell = std::make_shared<Shell>();
    shell->center = (art.boundsMin + art.boundsMax) * 0.5f;
    const glm::vec3 size = glm::max(art.boundsMax - art.boundsMin, glm::vec3(0.4f));
    // About 60 cm cells: 3 across, 2 up, 8 along a 4.5 m car (288 tets).
    // Finer folds cost more than they show on a low-poly body.
    const int nx = std::clamp(static_cast<int>(std::lround(size.x / 0.6f)), 2, 4);
    const int ny = 2;
    const int nz = std::clamp(static_cast<int>(std::lround(size.z / 0.6f)), 4, 9);
    const kke::TetMeshData mesh = kke::PhysicsModule::buildGridBox(nx, ny, nz, size.x, size.y, size.z);
    std::vector<glm::vec3> points;
    for (size_t p = 0; p < art.positions.size(); ++p) {
        shell->partSizes.push_back(art.positions[p].size());
        for (const glm::vec3& v : art.positions[p]) points.push_back(v - shell->center);
        shell->restNormals.insert(shell->restNormals.end(), art.normals[p].begin(), art.normals[p].end());
    }
    shell->embedding = kke::embedPoints(mesh, points);
    shell->anchor = kShellYard + glm::vec3(kSpacing * static_cast<float>(slot), size.y * 0.5f + 0.01f, 0.0f);
    kke::PhysicsModule::TetSpawnOptions o;
    o.plastic = true;
    o.drawOnlyCracks = true; // the Synty body is what's drawn
    shell->handle = m_femfx->spawnTetMeshWithOptions(mesh, shell->anchor, bodyMaterial(), o);
    if (shell->handle == kke::PhysicsModule::kInvalidHandle) return;
    c.shell = std::move(shell);
}

void RacingModule::dropShell(Car& c) {
    if (c.shell && m_femfx) m_femfx->removeObject(c.shell->handle);
    c.shell.reset();
}

void RacingModule::resetShell(Car& c) {
    if (!c.shell || !m_femfx) return;
    m_femfx->resetObject(c.shell->handle, c.shell->anchor, glm::vec3(0.0f));
    c.shell->reading = false;
}

// The hit as a shove: the vertices near it moved the way it pushed, the
// harder the faster, fading over a radius that grows with it. FEMFX then
// decides how far the metal gives and where it folds.
bool RacingModule::crumple(Car& c, const glm::vec3& localPoint, const glm::vec3& localDir, float depth) {
    if (!c.shell || !m_femfx) return false;
    Shell& s = *c.shell;
    const glm::vec3 at = s.anchor + (localPoint - s.center);
    const float radius = 0.5f + depth * 3.0f;
    const float shove = m_crumpleShove * depth; // m/s at the point of the hit
    m_femfx->changeVertexVelocities(s.handle, [&](const glm::vec3& p, const glm::vec3& v) {
        const float d = glm::length(p - at);
        if (d >= radius) return v;
        const float f = 1.0f - d / radius;
        return v + localDir * (shove * f * f);
    });
    s.reading = true;
    return true;
}

void RacingModule::updateShells() {
    if (!crumpleOn()) return;
    for (Car& c : m_cars) {
        if (!c.shell) continue;
        Shell& s = *c.shell;
        if (m_femfx->isObjectAsleep(s.handle)) {
            s.reading = false;
            continue;
        }
        // Hold it still: its mean motion and spin out (the rest is its
        // change of shape), back to its anchor if it drifted.
        glm::vec3 mid(0.0f), vel(0.0f);
        if (!m_femfx->objectMotion(s.handle, mid, vel)) continue;
        glm::vec3 momentum(0.0f);
        glm::mat3 inertia(0.0f);
        m_femfx->changeVertexVelocities(s.handle, [&](const glm::vec3& p, const glm::vec3& v) {
            const glm::vec3 r = p - mid;
            momentum += glm::cross(r, v - vel);
            inertia += glm::mat3(glm::dot(r, r)) - glm::outerProduct(r, r);
            return v;
        });
        const glm::vec3 spin = std::fabs(glm::determinant(inertia)) > 1e-6f ? glm::inverse(inertia) * momentum : glm::vec3(0.0f);
        m_femfx->changeVertexVelocities(s.handle, [&](const glm::vec3& p, const glm::vec3& v) { return v - vel - glm::cross(spin, p - mid); });
        if (glm::length(mid - s.anchor) > 0.02f) m_femfx->translateObject(s.handle, s.anchor - mid);
        if (!s.reading) continue; // settling on its spawn: nothing to show
        // The shape now, into the car's space for the Synty body.
        if (!m_femfx->deformEmbedded(s.handle, s.embedding, s.restNormals, s.positions, s.normals)) continue;
        if (c.dented.size() != s.partSizes.size()) {
            c.dented.assign(s.partSizes.size(), {});
            c.dentedNormals.assign(s.partSizes.size(), {});
        }
        size_t k = 0;
        for (size_t p = 0; p < s.partSizes.size(); ++p) {
            c.dented[p].resize(s.partSizes[p]);
            c.dentedNormals[p].resize(s.partSizes[p]);
            for (size_t i = 0; i < s.partSizes[p] && k < s.positions.size(); ++i, ++k) {
                c.dented[p][i] = s.positions[k] - s.anchor + s.center;
                c.dentedNormals[p][i] = s.normals[k];
            }
        }
        addPressedDents(c); // the creases on top of FEMFX's crumple (Damage.cpp)
        c.dentsChanged = true;
    }
}

std::string RacingModule::crumpleReport() const {
    if (!crumpleOn()) return {};
    int awake = 0;
    float deepest = 0.0f;
    for (const Car& c : m_cars) {
        if (!c.shell) continue;
        if (c.shell->reading) ++awake;
        for (size_t p = 0; p < c.dented.size() && p < c.art->positions.size(); ++p)
            for (size_t i = 0; i < c.dented[p].size() && i < c.art->positions[p].size(); ++i)
                deepest = std::max(deepest, glm::length(c.dented[p][i] - c.art->positions[p][i]));
    }
    return fmt::format(", FEMFX {:.2f} ms a step, {} bodies moving, deepest dent {:.2f} m", m_femfx->lastStepMsAvg(), awake, deepest);
}

#else

std::string RacingModule::crumpleReport() const { return {}; }
bool RacingModule::crumpleOn() const { return false; }
void RacingModule::makeShell(Car&, int) {}
void RacingModule::dropShell(Car&) {}
void RacingModule::resetShell(Car&) {}
bool RacingModule::crumple(Car&, const glm::vec3&, const glm::vec3&, float) { return false; }
void RacingModule::updateShells() {}

#endif

} // namespace racing
