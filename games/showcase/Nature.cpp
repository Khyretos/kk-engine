// The nature park and the snow field (Kees, 2026-10-03: "a Synty nature
// park with swaying grass and trees, an axe for chopping, flowers to pick"
// and "a snow area with trails").
//
// The nature park (layout::kZones[5]): a forest round a meadow. Trees,
// plants and the grass sway in gusts of wind, and the grass bends away
// from your legs. The axe (on the chopping block at the park's gate, or
// the supply table's) fells a tree in four hits: it falls away from you,
// thuds down, and lies as logs you can carry, throw, or split with the
// axe into firewood for the bag. Flowers in the meadow are picked into the
// bag. With POLYGON Nature installed the trees, stumps and plants are
// Synty's (swayed by turning their instances); without it they're built
// from shapes. The grass blades are this file's own either way.
//
// The snow field (layout::kZones[6]), up on the mountain: a layer of snow
// a hand deep that keeps every footprint and tyre track (a grid of how far
// it's pressed, drawn in chunks that are rebuilt when pressed), with snow
// falling round the camera. Reset the world makes it fresh again.
//
// KKE_DEMO_NATURE=1 takes the axe, fells a tree, splits a log, picks
// flowers and walks a trail through the snow (checks, screenshots).

#include "ShowcaseModule.h"
#include "Geometry.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/PhysicsWorld.h"
#include "kke/SceneLoader.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace kke_showcase {

using namespace layout;

namespace {

constexpr glm::vec2 kPark(-230.0f, 40.0f);   // the zone's middle
constexpr float kParkR = 108.0f;
constexpr glm::vec2 kMeadow(-205.0f, 45.0f); // the clearing with the flowers
constexpr float kMeadowR = 26.0f;
const glm::vec3 kChopBlock(-146.0f, 0.0f, 17.0f); // at the park's gate (y: on the ground)
constexpr int kHitsToFell = 4;
constexpr float kChopHitAt = 0.42f, kChopEnd = 0.7f; // s into a swing
constexpr float kGrassRadius = 20.0f;        // m round you that has blades
constexpr float kGrassCell = 0.5f;
const glm::vec3 kWindDir = glm::normalize(glm::vec3(1.0f, 0.0f, 0.45f));
const glm::vec3 kLogColor(0.42f, 0.29f, 0.17f);
// The snow field.
constexpr glm::vec2 kSnow(-300.0f, -280.0f);
constexpr float kSnowR = 56.0f;               // m: snow out to here (it thins out over the last 8 m)
constexpr float kSnowDepth = 0.22f;           // m of fresh snow
constexpr float kSnowCell = 0.25f;            // fine enough for a footprint
constexpr int kSnowCells = 464;               // per side (116 m)
constexpr int kSnowSide = kSnowCells + 1;     // vertices per side
constexpr int kSnowChunk = 58;                // cells per chunk side
constexpr int kSnowChunks = kSnowCells / kSnowChunk;
const glm::vec2 kSnowOrigin = kSnow - glm::vec2(kSnowCell * kSnowCells * 0.5f);

// The kinds of tree: Synty's name, and the shape-built stand-in's style
// (0 broadleaf, 1 pine, 2 birch), height and trunk radius.
struct TreeKind { const char* synty; int style; float height, trunk; };
const TreeKind kTreeKinds[] = {
    { "SM_Tree_Round_01", 0, 9.0f, 0.3f },  { "SM_Tree_Round_03", 0, 8.0f, 0.28f }, { "SM_Tree_Large_01", 0, 12.0f, 0.42f },
    { "SM_Tree_Pine_01", 1, 11.0f, 0.3f },  { "SM_Tree_Pine_02", 1, 10.0f, 0.28f }, { "SM_Tree_Birch_01", 2, 10.0f, 0.2f },
    { "SM_Tree_Birch_02", 2, 9.0f, 0.18f },
};
const char* const kStumpNames[] = { "SM_Tree_Stump_01", "SM_Tree_Stump_02" };
const char* const kPlantNames[] = { "SM_Plant_Bush_02", "SM_Plant_Grass_02", "SM_Plant_Grass_03", "SM_Plant_Fern_01", "SM_Plant_Bush_01",
                                    "SM_Plant_Flowers_01", "SM_Plant_PurpleFlower_01", "SM_Plant_Mushrooms_01", "SM_Plant_Grass_04", "SM_Plant_Fern_02" };
const char* const kFlowerIds[] = { "flower_red", "flower_yellow", "flower_blue" };
const glm::vec3 kFlowerColors[] = { { 0.85f, 0.12f, 0.1f }, { 0.97f, 0.82f, 0.12f }, { 0.25f, 0.4f, 0.92f } };

uint32_t hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
float unit(uint32_t h) { return static_cast<float>(h >> 8) * (1.0f / 16777216.0f); }
struct Rng {
    uint32_t s;
    float next() { s = hash(s + 0x9e3779b9u); return unit(s); }
};

std::shared_ptr<const kke::SoundBuffer> finishSound(kke::SoundBuffer& b) {
    float peak = 1e-6f;
    for (float s : b.samples) peak = std::max(peak, std::abs(s));
    for (float& s : b.samples) s *= 0.9f / peak;
    return std::make_shared<const kke::SoundBuffer>(std::move(b));
}

// An axe biting wood: a hard knock and a short woody ring.
std::shared_ptr<const kke::SoundBuffer> synthChop(uint32_t seed) {
    kke::SoundBuffer b;
    const int rate = b.sampleRate;
    const int n = rate * 2 / 5;
    b.samples.resize(static_cast<size_t>(n));
    float lp = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(rate);
        const float w = unit(seed = hash(seed + 1u)) * 2.0f - 1.0f;
        lp += 0.3f * (w - lp);
        const float knock = lp * std::exp(-t / 0.012f);
        const float ring = (std::sin(6.2831853f * 320.0f * t) + 0.6f * std::sin(6.2831853f * 510.0f * t)) * std::exp(-t / 0.06f);
        const float thud = std::sin(6.2831853f * 120.0f * t) * std::exp(-t / 0.04f);
        b.samples[static_cast<size_t>(i)] = knock * 1.2f + ring * 0.35f + thud * 0.6f;
    }
    return finishSound(b);
}

// A tree going down: creaking, cracking, then the crash and a rumble.
std::shared_ptr<const kke::SoundBuffer> synthFall(uint32_t seed) {
    kke::SoundBuffer b;
    const int rate = b.sampleRate;
    const int n = rate * 2;
    b.samples.resize(static_cast<size_t>(n));
    float lp = 0.0f, lp2 = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(rate);
        const float w = unit(seed = hash(seed + 1u)) * 2.0f - 1.0f;
        lp += 0.2f * (w - lp);
        lp2 += 0.01f * (w - lp2);
        const float crackle = (unit(hash(static_cast<uint32_t>(i / 90) * 2654435761u)) > 0.8f ? lp : 0.0f) * std::exp(-t / 0.3f);
        const float crash = lp * std::exp(-t / 0.18f) + lp2 * 8.0f * std::exp(-t / 0.7f);
        const float boom = std::sin(6.2831853f * 55.0f * t) * std::exp(-t / 0.25f);
        b.samples[static_cast<size_t>(i)] = crackle * 0.8f + crash + boom * 0.7f;
    }
    return finishSound(b);
}

// A soft rustle (picking a flower).
std::shared_ptr<const kke::SoundBuffer> synthRustle(uint32_t seed) {
    kke::SoundBuffer b;
    const int rate = b.sampleRate;
    const int n = rate / 4;
    b.samples.resize(static_cast<size_t>(n));
    float hp = 0.0f, prev = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(rate);
        const float w = unit(seed = hash(seed + 1u)) * 2.0f - 1.0f;
        hp = 0.7f * (hp + w - prev);
        prev = w;
        const float env = std::sin(glm::pi<float>() * t / 0.25f);
        b.samples[static_cast<size_t>(i)] = hp * env * env;
    }
    return finishSound(b);
}

// A cone standing on its base (local Y up from `m`'s origin), both sides.
void appendCone(const glm::mat4& m, float radius, float height, const glm::vec3& color, std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx) {
    constexpr int kSides = 10;
    const glm::mat3 nm = glm::mat3(m);
    const glm::vec3 tip(m * glm::vec4(0.0f, height, 0.0f, 1.0f));
    for (int s = 0; s < kSides; ++s) {
        const float a0 = glm::two_pi<float>() * static_cast<float>(s) / kSides, a1 = glm::two_pi<float>() * static_cast<float>(s + 1) / kSides;
        const glm::vec3 p0(std::cos(a0) * radius, 0.0f, std::sin(a0) * radius), p1(std::cos(a1) * radius, 0.0f, std::sin(a1) * radius);
        const glm::vec3 mid = glm::normalize(glm::vec3((p0.x + p1.x) * 0.5f, radius / height * radius, (p0.z + p1.z) * 0.5f));
        const glm::vec3 n = glm::normalize(nm * mid);
        const uint32_t base = static_cast<uint32_t>(v.size());
        v.push_back({ glm::vec3(m * glm::vec4(p0, 1.0f)), color * 0.85f, n, glm::vec2(0.0f) });
        v.push_back({ glm::vec3(m * glm::vec4(p1, 1.0f)), color * 0.85f, n, glm::vec2(0.0f) });
        v.push_back({ tip, color * 1.1f, n, glm::vec2(0.0f) });
        idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 1 });
    }
}

// A blade of grass (or a stem): a thin triangle from `base` to `tip`, both sides.
void appendBlade(const glm::vec3& base, const glm::vec3& tip, const glm::vec3& side, const glm::vec3& root, const glm::vec3& top, std::vector<kke::Vertex>& v,
                 std::vector<uint32_t>& idx) {
    const uint32_t b = static_cast<uint32_t>(v.size());
    const glm::vec3 n(0.0f, 1.0f, 0.0f); // lit like the ground it grows from
    v.push_back({ base - side, root, n, glm::vec2(0.0f) });
    v.push_back({ base + side, root, n, glm::vec2(0.0f) });
    v.push_back({ tip, top, n, glm::vec2(0.0f) });
    idx.insert(idx.end(), { b, b + 1, b + 2, b, b + 2, b + 1 });
}

} // namespace

// ------------------------------------------------------------ the park

float ShowcaseModule::windGust(const glm::vec3& at) const {
    const float t = m_windTime;
    return 0.55f + 0.45f * std::sin(t * 0.7f + at.x * 0.045f) * std::sin(t * 0.31f + at.z * 0.037f + 1.3f);
}

// Leaning with the wind and rocking in it, about `base`.
glm::mat4 ShowcaseModule::swayOf(const glm::vec3& base, float phase, float amount) const {
    const float gust = windGust(base);
    const float angle = amount * (0.6f * gust + 0.5f * (0.4f + gust) * std::sin(m_windTime * 1.3f + phase));
    const glm::vec3 axis = glm::normalize(glm::cross(glm::vec3(0, 1, 0), kWindDir));
    return glm::translate(glm::mat4(1.0f), base) * glm::rotate(glm::mat4(1.0f), angle, axis) * glm::translate(glm::mat4(1.0f), -base);
}

// What never moves: the chopping block at the park's gate.
void ShowcaseModule::buildNature(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx) {
    const float g = groundHeight(kChopBlock.x, kChopBlock.z);
    appendCylinder(glm::translate(glm::mat4(1.0f), glm::vec3(kChopBlock.x, g + 0.3f, kChopBlock.z)), 0.38f, 0.3f, glm::vec3(0.5f, 0.36f, 0.22f), v, idx);
    appendCylinder(glm::translate(glm::mat4(1.0f), glm::vec3(kChopBlock.x, g + 0.605f, kChopBlock.z)), 0.36f, 0.005f, glm::vec3(0.78f, 0.62f, 0.42f), v, idx);
    kke::RigidWorld::BodyDesc d;
    d.shape = kke::RigidWorld::Shape::ConvexHull;
    d.motion = kke::RigidWorld::Motion::Static;
    d.points = cylinderPoints(0.38f, 0.3f);
    d.position = glm::vec3(kChopBlock.x, g + 0.3f, kChopBlock.z);
    d.material = 2;
    m_rigid->world().add(d);
    // A woodpile beside it.
    for (int k = 0; k < 5; ++k)
        appendCylinder(glm::translate(glm::mat4(1.0f), glm::vec3(kChopBlock.x + 1.4f, g + 0.17f + (k > 2 ? 0.3f : 0.0f), kChopBlock.z - 0.6f + 0.33f * static_cast<float>(k % 3) + (k > 2 ? 0.16f : 0.0f))) *
                           glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), glm::vec3(1, 0, 0)),
                       0.16f, 0.5f, kLogColor, v, idx);
}

void ShowcaseModule::clearNature() {
    kke::RigidWorld& w = m_rigid->world();
    for (Tree& t : m_trees) {
        if (t.body != kke::RigidWorld::kNoBody) w.remove(t.body);
        if (t.instance) m_models->remove(t.instance);
        if (t.stump) m_models->remove(t.stump);
    }
    m_trees.clear();
    for (Plant& p : m_plants)
        if (p.instance) m_models->remove(p.instance);
    m_plants.clear();
    m_flowers.clear();
    m_chop = -1.0f;
    m_chopTree = -1;
}

// Trees, plants and flowers where they started; the snow fresh.
void ShowcaseModule::spawnNature() {
    clearNature();
    if (!m_sndChop) {
        m_sndChop = synthChop(0x51a7e5u);
        m_sndFall = synthFall(0x7a11u);
        m_sndPick = synthRustle(0xf10e7u);
    }
    // Synty's trees, once: all of them or none (half would look like a bug).
    if (!m_natureTried) {
        m_natureTried = true;
        const char* off = std::getenv("KKE_NATURE_ART");
        if (!(off && *off == '0')) {
            scanCatalog();
            const std::vector<std::string> packs = { "POLYGON_Nature" };
            auto load = [&](const char* asset) -> kke::ModelModule::ModelId {
                const kke::CatalogAsset* a = m_catalog.find(asset, packs);
                return a ? m_models->load(a->path, kke::packLoadOptions(m_catalog, *a)) : 0;
            };
            bool all = true;
            for (const TreeKind& k : kTreeKinds) {
                m_treeModels.push_back(load(k.synty));
                all = all && m_treeModels.back();
            }
            for (const char* s : kStumpNames) {
                m_stumpModels.push_back(load(s));
                all = all && m_stumpModels.back();
            }
            for (const char* p : kPlantNames) {
                m_plantModels.push_back(load(p));
                all = all && m_plantModels.back();
            }
            m_natureSynty = all;
            kke::log::get(name())->info("nature: {}", all ? "POLYGON Nature trees and plants" : "POLYGON Nature isn't installed: trees built from shapes");
        }
    }
    kke::RigidWorld& w = m_rigid->world();
    Rng rng{ 0x5eed1234u };
    // The forest: round the meadow, off the roads, not too close together.
    for (int tries = 0; tries < 1200 && m_trees.size() < 85; ++tries) {
        const float a = rng.next() * glm::two_pi<float>(), r = 18.0f + std::sqrt(rng.next()) * (kParkR - 22.0f);
        const glm::vec2 p = kPark + glm::vec2(std::cos(a), std::sin(a)) * r;
        if (glm::length(p - kMeadow) < kMeadowR + 4.0f || roadDistance(p.x, p.y) < 4.0f || glm::length(p - glm::vec2(kChopBlock.x, kChopBlock.z)) < 8.0f) continue;
        bool crowded = false;
        for (const Tree& t : m_trees) crowded = crowded || glm::length(glm::vec2(t.base.x, t.base.z) - p) < 5.0f;
        if (crowded) continue;
        Tree t;
        t.kind = static_cast<int>(rng.next() * static_cast<float>(std::size(kTreeKinds))) % static_cast<int>(std::size(kTreeKinds));
        t.base = glm::vec3(p.x, groundHeight(p.x, p.y) - 0.05f, p.y);
        t.yaw = rng.next() * 360.0f;
        t.scale = 0.8f + 0.4f * rng.next();
        t.phase = rng.next() * 6.28f;
        const TreeKind& k = kTreeKinds[t.kind];
        t.height = k.height * t.scale;
        t.trunk = k.trunk * t.scale;
        if (m_natureSynty) {
            if (const kke::ModelData* d = m_models->model(m_treeModels[static_cast<size_t>(t.kind)])) {
                float top = 0.0f;
                for (const kke::ModelMesh& mesh : d->meshes)
                    for (const kke::ModelVertex& mv : mesh.vertices) top = std::max(top, mv.position.y);
                if (top > 1.0f) t.height = top * t.scale;
            }
            t.instance = m_models->spawn(m_treeModels[static_cast<size_t>(t.kind)]);
        }
        // The trunk: a static capsule you can't walk through (the crown has none).
        kke::RigidWorld::BodyDesc d;
        d.shape = kke::RigidWorld::Shape::Capsule;
        d.motion = kke::RigidWorld::Motion::Static;
        d.radius = t.trunk;
        d.halfHeight = 1.6f;
        d.position = t.base + glm::vec3(0.0f, 1.6f + t.trunk, 0.0f);
        d.material = 2;
        t.body = w.add(d);
        m_trees.push_back(t);
    }
    // Synty undergrowth all over: grass clumps, ferns, bushes, flower
    // patches, mushrooms (they sway too, near you).
    if (m_natureSynty)
        for (int k = 0; k < 520; ++k) {
            const float a = rng.next() * glm::two_pi<float>(), r = std::sqrt(rng.next()) * (kParkR - 4.0f);
            const glm::vec2 p = kPark + glm::vec2(std::cos(a), std::sin(a)) * r;
            if (roadDistance(p.x, p.y) < 1.0f) continue;
            Plant pl;
            pl.at = glm::vec3(p.x, groundHeight(p.x, p.y), p.y);
            pl.yaw = rng.next() * 360.0f;
            pl.scale = 0.8f + 0.5f * rng.next();
            pl.phase = rng.next() * 6.28f;
            // Meadow: grass and flowers; forest: ferns, bushes, mushrooms too.
            const bool meadow = glm::length(p - kMeadow) < kMeadowR;
            const int kind = meadow ? (k % 3 == 0 ? 5 + (k % 2) : k % 3) : static_cast<int>(rng.next() * static_cast<float>(std::size(kPlantNames))) % static_cast<int>(std::size(kPlantNames));
            pl.instance = m_models->spawn(m_plantModels[static_cast<size_t>(kind)]);
            m_plants.push_back(pl);
        }
    // Flowers to pick in the meadow.
    for (int k = 0; k < 48; ++k) {
        const float a = rng.next() * glm::two_pi<float>(), r = std::sqrt(rng.next()) * (kMeadowR - 2.0f);
        const glm::vec2 p = kMeadow + glm::vec2(std::cos(a), std::sin(a)) * r;
        m_flowers.push_back({ glm::vec3(p.x, groundHeight(p.x, p.y), p.y), k % 3, rng.next() * 6.28f, false });
    }
    // Fresh snow.
    m_snowPress.assign(static_cast<size_t>(kSnowSide * kSnowSide), 0.0f);
    if (m_snowGround.empty()) {
        m_snowGround.resize(m_snowPress.size());
        for (int j = 0; j < kSnowSide; ++j)
            for (int i = 0; i < kSnowSide; ++i)
                m_snowGround[static_cast<size_t>(j * kSnowSide + i)] =
                    groundHeight(kSnowOrigin.x + kSnowCell * static_cast<float>(i), kSnowOrigin.y + kSnowCell * static_cast<float>(j));
    }
    m_snowDirty.assign(static_cast<size_t>(kSnowChunks * kSnowChunks), 1);
    m_felled = 0;
    kke::log::get(name())->info("nature: {} trees, {} plants, {} flowers; snow {} x {} cells", m_trees.size(), m_plants.size(), m_flowers.size(), kSnowCells, kSnowCells);
}

bool ShowcaseModule::axeInHand() const {
    const kke::InventoryItem* it = m_inv.equipped(kke::EquipSlot::RightHand);
    return it && it->id == "axe" && m_held.body == kke::RigidWorld::kNoBody && m_ride == Ride::None;
}

int ShowcaseModule::treeInReach(float reach) const {
    const glm::vec3 me = m_rigid->world().characterPosition(m_player);
    glm::vec3 f = m_loco->facing();
    f.y = 0.0f;
    f = glm::length(f) > 1e-3f ? glm::normalize(f) : glm::vec3(0, 0, -1);
    int best = -1;
    float bestD = reach;
    for (size_t k = 0; k < m_trees.size(); ++k) {
        const Tree& t = m_trees[k];
        if (t.state != TreeState::Standing) continue;
        glm::vec3 to = t.base - me;
        to.y = 0.0f;
        const float d = glm::length(to) - t.trunk;
        if (d > bestD || (glm::length(to) > 0.3f && glm::dot(glm::normalize(to), f) < 0.2f)) continue;
        bestD = d;
        best = static_cast<int>(k);
    }
    return best;
}

int ShowcaseModule::flowerInReach() const {
    if (m_flowers.empty() || m_ride != Ride::None) return -1;
    const glm::vec3 me = m_rigid->world().characterPosition(m_player);
    if (glm::length(glm::vec2(me.x, me.z) - kMeadow) > kMeadowR + 3.0f) return -1;
    glm::vec3 f = m_loco->facing();
    f.y = 0.0f;
    f = glm::length(f) > 1e-3f ? glm::normalize(f) : glm::vec3(0, 0, -1);
    int best = -1;
    float bestD = 1.3f;
    for (size_t k = 0; k < m_flowers.size(); ++k) {
        if (m_flowers[k].picked) continue;
        glm::vec3 to = m_flowers[k].at - me;
        to.y = 0.0f;
        const float d = glm::length(to);
        if (d > bestD || (d > 0.3f && glm::dot(to / d, f) < 0.2f)) continue;
        bestD = d;
        best = static_cast<int>(k);
    }
    return best;
}

std::string ShowcaseModule::flowerName(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= m_flowers.size()) return {};
    const kke::ItemDef* def = m_items.find(kFlowerIds[m_flowers[static_cast<size_t>(index)].kind]);
    return def ? def->name : std::string("flower");
}

void ShowcaseModule::pickFlower(int index) {
    if (index < 0 || static_cast<size_t>(index) >= m_flowers.size()) return;
    Flower& fl = m_flowers[static_cast<size_t>(index)];
    const char* id = kFlowerIds[fl.kind];
    const kke::ItemDef* def = m_items.find(id);
    if (!def) return;
    if (m_inv.add(m_items, id, 1) > 0) {
        toast("No room in your bag for the " + def->name);
        return;
    }
    fl.picked = true;
    m_pickReach = 0.0f;
    m_pickAt = fl.at + glm::vec3(0.0f, 0.3f, 0.0f);
    m_invDirty = true;
    playSound(m_sndPick, fl.at, 0.5f, 25.0f);
    toast("Picked a " + def->name + " (" + std::to_string(m_inv.count(id)) + " in the bag)");
    kke::log::get(name())->info("nature: picked a {} ({} in the bag)", id, m_inv.count(id));
}

// A swing of the axe: at a tree in reach (four hits fell it), at a log
// (splits it into firewood), or at the air.
void ShowcaseModule::startChop() {
    if (m_chop >= 0.0f) return;
    const kke::RigidWorld& w = m_rigid->world();
    const glm::vec3 me = w.characterPosition(m_player);
    m_chop = 0.0f;
    m_chopTree = treeInReach(1.6f);
    if (m_chopTree >= 0) {
        const Tree& t = m_trees[static_cast<size_t>(m_chopTree)];
        glm::vec3 to = t.base - me;
        to.y = 0.0f;
        to = glm::length(to) > 1e-3f ? glm::normalize(to) : glm::vec3(0, 0, -1);
        m_loco->setFacing(to);
        m_chopAt = t.base + glm::vec3(0.0f, 1.0f, 0.0f) - to * t.trunk;
        return;
    }
    glm::vec3 f = m_loco->facing();
    f.y = 0.0f;
    f = glm::length(f) > 1e-3f ? glm::normalize(f) : glm::vec3(0, 0, -1);
    m_chopAt = me + f * 0.9f + glm::vec3(0.0f, 0.5f, 0.0f);
}

void ShowcaseModule::chopHit() {
    kke::RigidWorld& w = m_rigid->world();
    const glm::vec3 me = w.characterPosition(m_player);
    if (m_chopTree >= 0 && static_cast<size_t>(m_chopTree) < m_trees.size() && m_trees[static_cast<size_t>(m_chopTree)].state == TreeState::Standing) {
        Tree& t = m_trees[static_cast<size_t>(m_chopTree)];
        ++t.hits;
        t.shake = 1.0f;
        glm::vec3 out = me - t.base;
        out.y = 0.0f;
        out = glm::length(out) > 1e-3f ? glm::normalize(out) : glm::vec3(1, 0, 0);
        m_fxLib->play("wood_chips", { m_chopAt, out, glm::vec3(0.0f), 0.6f, t.base.y });
        playSound(m_sndChop, m_chopAt, 0.9f, 60.0f);
        kke::log::get(name())->info("nature: chop {} of {} at the tree at {:.1f} {:.1f}", t.hits, kHitsToFell, t.base.x, t.base.z);
        if (t.hits >= kHitsToFell) {
            // Down it goes, away from you.
            t.state = TreeState::Falling;
            t.fallDir = -out;
            t.fall = 0.02f;
            t.fallSpeed = 0.1f;
            if (t.body != kke::RigidWorld::kNoBody) w.remove(t.body);
            t.body = kke::RigidWorld::kNoBody;
            ++m_felled;
            playSound(m_sndFall, t.base + glm::vec3(0, 2, 0), 0.5f, 120.0f); // the creak and crack (the crash is in it)
            toast("Timber!");
        }
        return;
    }
    // A log in front: split it into firewood for the bag.
    for (size_t k = 0; k < m_props.size(); ++k) {
        const Prop& p = m_props[k];
        if (p.shape != PropShape::Barrel || p.color != kLogColor) continue;
        const glm::vec3 at = w.position(p.body);
        if (glm::length(at - m_chopAt) > 1.6f) continue;
        if (m_held.body == p.body) dropHeld();
        w.remove(p.body);
        m_props.erase(m_props.begin() + static_cast<std::ptrdiff_t>(k));
        dropItem("firewood", 2, at);
        dropItem("firewood", 2, at + glm::vec3(0.3f, 0.0f, 0.2f));
        m_fxLib->play("wood_chips", { at + glm::vec3(0, 0.2f, 0), glm::vec3(0, 1, 0), glm::vec3(0.0f), 0.8f, groundHeight(at.x, at.z) });
        playSound(m_sndChop, at, 1.0f, 60.0f);
        toast("Split into firewood");
        kke::log::get(name())->info("nature: split a log into firewood at {:.1f} {:.1f}", at.x, at.z);
        return;
    }
}

// The swing's arms: both hands on the handle, up over the right shoulder,
// then down onto where the blade lands. And a hand down to pick a flower.
void ShowcaseModule::chopHands(const kke::Pose& pose, const glm::mat4& toWorld) {
    if (!m_ik.arm(kke::CharacterIk::Right).valid()) return;
    using Side = kke::CharacterIk::Side;
    const std::vector<glm::mat4> bones = kke::poseToModel(m_rigData, pose);
    const kke::HumanArm& arm = m_ik.arm(Side::Right);
    const glm::vec3 sr = glm::vec3(toWorld * bones[static_cast<size_t>(arm.chain.upper)][3]);
    glm::vec3 f = m_loco->facing();
    f.y = 0.0f;
    f = glm::length(f) > 1e-3f ? glm::normalize(f) : glm::vec3(0, 0, -1);
    const glm::vec3 right = glm::normalize(glm::cross(f, glm::vec3(0, 1, 0))), up(0, 1, 0);
    if (m_chop >= 0.0f) {
        const glm::vec3 raised = sr + up * 0.45f - f * 0.1f + right * 0.05f;
        const glm::vec3 struck = m_chopAt - f * 0.55f + up * 0.05f; // the axe's length short of the cut
        float k;
        glm::vec3 hand;
        if (m_chop < 0.3f) {
            k = m_chop / 0.3f;
            hand = glm::mix(sr + f * 0.3f - up * 0.3f, raised, k * k * (3.0f - 2.0f * k));
        } else if (m_chop < kChopHitAt) {
            k = (m_chop - 0.3f) / (kChopHitAt - 0.3f);
            hand = glm::mix(raised, struck, k * k);
        } else {
            k = std::min(1.0f, (m_chop - kChopHitAt) / (kChopEnd - kChopHitAt));
            hand = glm::mix(struck, sr + f * 0.3f - up * 0.3f, k);
        }
        m_ik.hand(Side::Right, hand, hand - up * 0.3f + right * 0.35f);
        if (!m_inv.equipped(kke::EquipSlot::LeftHand)) {
            const glm::vec3 low = hand - glm::normalize(hand - sr + f * 0.2f) * 0.22f;
            m_ik.hand(Side::Left, low, low - up * 0.3f - right * 0.35f);
        }
        return;
    }
    if (m_pickReach >= 0.0f) {
        const float k = std::sin(glm::pi<float>() * std::min(1.0f, m_pickReach / 0.6f));
        const glm::vec3 hand = glm::mix(sr + f * 0.25f - up * 0.5f, m_pickAt, k);
        m_ik.hand(Side::Right, hand, hand + up * 0.3f + right * 0.3f);
    }
}

// The park, every frame: wind, falling trees, Synty swaying, the axe, snow.
void ShowcaseModule::updateNature(float dt) {
    m_windTime += dt;
    kke::RigidWorld& w = m_rigid->world();
    const glm::vec3 cam = m_app->camera().position;
    // The swing.
    if (m_chop >= 0.0f) {
        const float before = m_chop;
        m_chop += dt;
        if (before < kChopHitAt && m_chop >= kChopHitAt) chopHit();
        if (m_chop >= kChopEnd) m_chop = -1.0f;
    }
    if (m_pickReach >= 0.0f && (m_pickReach += dt) > 0.6f) m_pickReach = -1.0f;
    for (Tree& t : m_trees) {
        t.shake = std::max(0.0f, t.shake - dt * 2.5f);
        if (t.state == TreeState::Falling) {
            // A rod tipping over its foot: slow at first, then fast.
            t.fallSpeed += (1.5f * 9.81f / std::max(t.height, 2.0f)) * std::sin(t.fall) * dt + 0.05f * dt;
            t.fall += t.fallSpeed * dt;
            if (t.fall >= 1.5f) {
                t.fall = 1.5f;
                t.state = TreeState::Down;
                t.downTime = 0.0f;
                const glm::vec3 crown = t.base + t.fallDir * (t.height * 0.65f);
                for (float s : { 0.3f, 0.6f, 0.85f }) {
                    const glm::vec3 p = t.base + t.fallDir * (t.height * s);
                    m_fxLib->play("impact_dirt", { glm::vec3(p.x, groundHeight(p.x, p.z) + 0.1f, p.z), glm::vec3(0, 1, 0), glm::vec3(0.0f), 1.2f, groundHeight(p.x, p.z) });
                }
                kke::physicsBlast(m_app->findCapability<kke::IPhysicsWorld>(), crown, 3.5f, 4.0f); // it knocks what it lands on
                const float near = glm::length(crown - w.characterPosition(m_player));
                if (near < 25.0f) m_shake = std::max(m_shake, 0.6f * (1.0f - near / 25.0f));
                kke::log::get(name())->info("nature: a tree came down at {:.1f} {:.1f}, {:.1f} m tall", t.base.x, t.base.z, t.height);
            }
        } else if (t.state == TreeState::Down && (t.downTime += dt) > 1.2f) {
            // It lies as logs now: to carry, throw, or split with the axe.
            t.state = TreeState::Gone;
            if (t.instance) m_models->remove(t.instance);
            t.instance = 0;
            const int logs = std::clamp(static_cast<int>(t.height / 3.0f), 2, 4);
            const glm::quat lie(glm::vec3(0, 1, 0), t.fallDir); // a barrel's axis is its Y
            for (int k = 0; k < logs; ++k) {
                const glm::vec3 at = t.base + t.fallDir * (1.2f + 2.2f * static_cast<float>(k));
                const float g = groundHeight(at.x, at.z);
                spawnProp(PropShape::Barrel, glm::vec3(std::max(0.12f, t.trunk * (1.0f - 0.15f * static_cast<float>(k))), 0.95f, 0.0f), 650.0f, kLogColor, 2,
                          glm::vec3(at.x, g + 0.35f, at.z));
                if (!m_props.empty()) w.setTransform(m_props.back().body, glm::vec3(at.x, g + 0.35f, at.z), lie);
            }
            // The stump stays, and still stops you.
            if (m_natureSynty && !m_stumpModels.empty())
                t.stump = m_models->spawn(m_stumpModels[static_cast<size_t>(t.kind) % m_stumpModels.size()],
                                          glm::translate(glm::mat4(1.0f), t.base) * glm::rotate(glm::mat4(1.0f), glm::radians(t.yaw), glm::vec3(0, 1, 0)) *
                                              glm::scale(glm::mat4(1.0f), glm::vec3(t.scale)));
            kke::RigidWorld::BodyDesc d;
            d.shape = kke::RigidWorld::Shape::ConvexHull;
            d.motion = kke::RigidWorld::Motion::Static;
            d.points = cylinderPoints(t.trunk * 1.15f, 0.25f);
            d.position = t.base + glm::vec3(0.0f, 0.25f, 0.0f);
            d.material = 2;
            t.body = w.add(d);
            kke::log::get(name())->info("nature: the tree lies as {} logs", logs);
        }
        // Synty trees: their instance placed, swaying (only near enough to see it).
        if (t.instance && glm::length(t.base - cam) < 260.0f) {
            glm::mat4 m = glm::translate(glm::mat4(1.0f), t.base);
            if (t.state == TreeState::Falling || t.state == TreeState::Down)
                m = m * glm::rotate(glm::mat4(1.0f), t.fall, glm::normalize(glm::cross(glm::vec3(0, 1, 0), t.fallDir)));
            m = m * glm::rotate(glm::mat4(1.0f), glm::radians(t.yaw), glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(t.scale));
            const float rock = t.shake * 0.03f * std::sin(m_windTime * 30.0f);
            if (t.state == TreeState::Standing)
                m = swayOf(t.base, t.phase, 0.018f) * glm::translate(glm::mat4(1.0f), t.base) *
                    glm::rotate(glm::mat4(1.0f), rock, glm::normalize(glm::cross(glm::vec3(0, 1, 0), kWindDir + glm::vec3(0.01f, 0, 0)))) *
                    glm::translate(glm::mat4(1.0f), -t.base) * m;
            m_models->setTransform(t.instance, m);
        }
    }
    for (Plant& p : m_plants)
        if (p.instance && glm::length(p.at - cam) < 60.0f)
            m_models->setTransform(p.instance, swayOf(p.at, p.phase, 0.07f) * glm::translate(glm::mat4(1.0f), p.at) *
                                                   glm::rotate(glm::mat4(1.0f), glm::radians(p.yaw), glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(p.scale)));

    // Leaves drifting down in the forest; snow falling on the snow field.
    const bool inPark = glm::length(glm::vec2(cam.x, cam.z) - kPark) < kParkR;
    const bool onSnow = glm::length(glm::vec2(cam.x, cam.z) - kSnow) < kSnowR + 25.0f;
    if (inPark && !m_leafEmitter) m_leafEmitter = m_fxLib->start("leaves", { cam + glm::vec3(0, 5, 0) });
    if (!inPark && m_leafEmitter) {
        m_fxLib->stop(m_leafEmitter);
        m_leafEmitter = 0;
    }
    if (m_leafEmitter) m_fxLib->move(m_leafEmitter, { cam + glm::vec3(0, 5, 0), glm::vec3(0, 1, 0), glm::vec3(0.0f), 1.0f, groundHeight(cam.x, cam.z) });
    if (onSnow && !m_snowEmitter) m_snowEmitter = m_fxLib->start("snow", { cam + glm::vec3(0, 6, 0) });
    if (!onSnow && m_snowEmitter) {
        m_fxLib->stop(m_snowEmitter);
        m_snowEmitter = 0;
    }
    if (m_snowEmitter) m_fxLib->move(m_snowEmitter, { cam + glm::vec3(0, 6, 0), glm::vec3(0, 1, 0), glm::vec3(0.0f), 1.0f, groundHeight(cam.x, cam.z) + kSnowDepth });

    // Footprints: one foot then the other, every stride, while on the ground in the snow.
    const glm::vec3 feet = w.characterPosition(m_player);
    if (m_ride == Ride::None && inSnow(feet) && m_loco->state() == kke::Locomotion::State::Ground) {
        m_stride += glm::length(glm::vec2(feet.x - m_lastSnowFeet.x, feet.z - m_lastSnowFeet.z));
        if (m_stride > 0.62f) {
            m_stride = 0.0f;
            m_leftFoot = !m_leftFoot;
            const glm::vec3 f = m_loco->facing();
            const glm::vec3 side = glm::normalize(glm::cross(glm::vec3(f.x, 0.0f, f.z) + glm::vec3(1e-4f, 0, 0), glm::vec3(0, 1, 0)));
            const glm::vec3 at = feet + side * (m_leftFoot ? -0.12f : 0.12f);
            stampSnow({ at.x, at.z }, { 0.075f, 0.15f }, std::atan2(f.x, f.z), 0.85f);
        }
    }
    m_lastSnowFeet = feet;
    // Tyre tracks.
    for (const Car& c : m_cars)
        for (const kke::VehicleWheelState& ws : c.state.wheels)
            if (ws.contact && inSnow(ws.contactPoint)) {
                const glm::vec3 f = w.rotation(w.vehicleBody(c.id)) * glm::vec3(0, 0, 1);
                stampSnow({ ws.contactPoint.x, ws.contactPoint.z }, { 0.12f, 0.22f }, std::atan2(f.x, f.z), 0.75f);
            }
    rebuildSnow();
}

bool ShowcaseModule::inSnow(const glm::vec3& p) const { return glm::length(glm::vec2(p.x, p.z) - kSnow) < kSnowR - 1.0f; }

// Presses an oval (half sizes across, along; turned to `yaw`) into the snow.
void ShowcaseModule::stampSnow(const glm::vec2& at, const glm::vec2& half, float yaw, float depth) {
    if (m_snowPress.empty()) return;
    const float c = std::cos(yaw), s = std::sin(yaw);
    const float reach = std::max(half.x, half.y) + kSnowCell;
    const int i0 = std::max(0, static_cast<int>((at.x - reach - kSnowOrigin.x) / kSnowCell)), i1 = std::min(kSnowSide - 1, static_cast<int>((at.x + reach - kSnowOrigin.x) / kSnowCell) + 1);
    const int j0 = std::max(0, static_cast<int>((at.y - reach - kSnowOrigin.y) / kSnowCell)), j1 = std::min(kSnowSide - 1, static_cast<int>((at.y + reach - kSnowOrigin.y) / kSnowCell) + 1);
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i) {
            const glm::vec2 d = kSnowOrigin + glm::vec2(static_cast<float>(i), static_cast<float>(j)) * kSnowCell - at;
            // Into the oval's own axes (x across, y along the way it faces).
            const float across = d.x * c - d.y * s, along = d.x * s + d.y * c;
            const float r = std::sqrt((across * across) / ((half.x + 0.08f) * (half.x + 0.08f)) + (along * along) / ((half.y + 0.08f) * (half.y + 0.08f)));
            if (r >= 1.0f) continue;
            float& p = m_snowPress[static_cast<size_t>(j * kSnowSide + i)];
            const float want = depth * std::min(1.0f, (1.0f - r) * 2.5f);
            if (want <= p) continue;
            p = want;
            // The chunks this vertex belongs to (a vertex on a seam is in two).
            for (int cj = std::max(0, (j - 1) / kSnowChunk); cj <= std::min(kSnowChunks - 1, j / kSnowChunk); ++cj)
                for (int ci = std::max(0, (i - 1) / kSnowChunk); ci <= std::min(kSnowChunks - 1, i / kSnowChunk); ++ci)
                    m_snowDirty[static_cast<size_t>(cj * kSnowChunks + ci)] = 1;
        }
}

// The snow's chunks that something pressed (a few a frame).
void ShowcaseModule::rebuildSnow() {
    if (m_snowPress.empty() || !m_natureStatic) return;
    if (m_snowChunks.empty()) {
        for (int k = 0; k < kSnowChunks * kSnowChunks; ++k) m_snowChunks.push_back(std::make_unique<kke::DynamicMeshRenderer>(*m_app));
        m_snowIndices.assign(m_snowChunks.size(), 0);
    }
    auto height = [this](int i, int j) {
        i = std::clamp(i, 0, kSnowSide - 1);
        j = std::clamp(j, 0, kSnowSide - 1);
        const size_t n = static_cast<size_t>(j * kSnowSide + i);
        const glm::vec2 p = kSnowOrigin + glm::vec2(static_cast<float>(i), static_cast<float>(j)) * kSnowCell;
        const float r = glm::length(p - kSnow);
        const float thick = kSnowDepth * std::clamp((kSnowR - r) / 8.0f, 0.0f, 1.0f);
        return m_snowGround[n] + 0.03f + thick * (1.0f - 0.92f * m_snowPress[n]);
    };
    static std::vector<kke::Vertex> v;
    static std::vector<uint32_t> idx;
    int budget = 6;
    for (int cj = 0; cj < kSnowChunks && budget > 0; ++cj)
        for (int ci = 0; ci < kSnowChunks && budget > 0; ++ci) {
            const size_t c = static_cast<size_t>(cj * kSnowChunks + ci);
            if (!m_snowDirty[c]) continue;
            m_snowDirty[c] = 0;
            --budget;
            v.clear();
            idx.clear();
            const int bi = ci * kSnowChunk, bj = cj * kSnowChunk;
            for (int j = 0; j <= kSnowChunk; ++j)
                for (int i = 0; i <= kSnowChunk; ++i) {
                    const int gi = bi + i, gj = bj + j;
                    const float h = height(gi, gj);
                    const glm::vec3 n = glm::normalize(glm::vec3(height(gi - 1, gj) - height(gi + 1, gj), 2.0f * kSnowCell, height(gi, gj - 1) - height(gi, gj + 1)));
                    const float press = m_snowPress[static_cast<size_t>(gj * kSnowSide + gi)];
                    const glm::vec3 color = glm::mix(glm::vec3(0.95f, 0.96f, 0.99f), glm::vec3(0.66f, 0.72f, 0.82f), press);
                    v.push_back({ glm::vec3(kSnowOrigin.x + kSnowCell * static_cast<float>(gi), h, kSnowOrigin.y + kSnowCell * static_cast<float>(gj)), color, n, glm::vec2(0.0f) });
                }
            const int row = kSnowChunk + 1;
            for (int j = 0; j < kSnowChunk; ++j)
                for (int i = 0; i < kSnowChunk; ++i) {
                    const glm::vec2 mid = kSnowOrigin + glm::vec2(static_cast<float>(bi + i) + 0.5f, static_cast<float>(bj + j) + 0.5f) * kSnowCell;
                    if (glm::length(mid - kSnow) > kSnowR) continue; // only where there's snow
                    const uint32_t a = static_cast<uint32_t>(j * row + i), b = a + 1, cc = a + static_cast<uint32_t>(row), d = cc + 1;
                    idx.insert(idx.end(), { a, cc, b, b, cc, d });
                }
            m_snowIndices[c] = idx.size();
            if (!idx.empty()) m_snowChunks[c]->upload(v, idx);
        }
}

// Everything in the park that's drawn from shapes, every frame: trees
// (when there's no Synty), stumps, logs-to-be, flowers; and the grass.
void ShowcaseModule::batchNature() {
    static std::vector<kke::Vertex> v, gv;
    static std::vector<uint32_t> idx, gi;
    v.clear();
    idx.clear();
    gv.clear();
    gi.clear();
    const glm::vec3 cam = m_app->camera().position;
    const glm::vec3 feet = m_rigid->world().characterPosition(m_player);
    const bool near = glm::length(glm::vec2(cam.x, cam.z) - kPark) < kParkR + 120.0f;
    if (near) {
        for (const Tree& t : m_trees) {
            const glm::vec3 bark = kTreeKinds[t.kind].style == 2 ? glm::vec3(0.85f, 0.84f, 0.8f) : glm::vec3(0.4f, 0.28f, 0.17f);
            if (t.state == TreeState::Gone) {
                if (!t.stump) {
                    appendCylinder(glm::translate(glm::mat4(1.0f), t.base + glm::vec3(0.0f, 0.25f, 0.0f)), t.trunk * 1.1f, 0.25f, bark, v, idx);
                    appendCylinder(glm::translate(glm::mat4(1.0f), t.base + glm::vec3(0.0f, 0.505f, 0.0f)), t.trunk, 0.005f, glm::vec3(0.8f, 0.65f, 0.45f), v, idx);
                }
                continue;
            }
            if (t.instance) continue;
            glm::mat4 m = glm::translate(glm::mat4(1.0f), t.base);
            if (t.state != TreeState::Standing) m = m * glm::rotate(glm::mat4(1.0f), t.fall, glm::normalize(glm::cross(glm::vec3(0, 1, 0), t.fallDir)));
            else m = swayOf(t.base, t.phase, 0.018f + t.shake * 0.02f * std::sin(m_windTime * 30.0f)) * m;
            m = m * glm::rotate(glm::mat4(1.0f), glm::radians(t.yaw), glm::vec3(0, 1, 0));
            const float h = t.height;
            const int style = kTreeKinds[t.kind].style;
            const float trunkTop = style == 1 ? h * 0.3f : h * 0.55f;
            appendCylinder(m * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, trunkTop * 0.5f, 0.0f)), t.trunk, trunkTop * 0.5f, bark, v, idx);
            if (style == 1) {
                const glm::vec3 green(0.13f, 0.32f, 0.16f);
                for (int k = 0; k < 3; ++k) {
                    const float y = h * (0.22f + 0.22f * static_cast<float>(k)), r = h * (0.24f - 0.06f * static_cast<float>(k));
                    appendCone(m * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, y, 0.0f)), r, h * 0.38f, green * (1.0f + 0.12f * static_cast<float>(k)), v, idx);
                }
            } else {
                const glm::vec3 green = style == 2 ? glm::vec3(0.45f, 0.62f, 0.22f) : glm::vec3(0.24f, 0.45f, 0.16f);
                const float r = h * (style == 2 ? 0.2f : 0.27f);
                appendSphere(m * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, h * 0.68f, 0.0f)), r, green, v, idx);
                appendSphere(m * glm::translate(glm::mat4(1.0f), glm::vec3(r * 0.6f, h * 0.58f, r * 0.2f)), r * 0.7f, green * 0.9f, v, idx);
                appendSphere(m * glm::translate(glm::mat4(1.0f), glm::vec3(-r * 0.5f, h * 0.6f, -r * 0.35f)), r * 0.65f, green * 1.1f, v, idx);
            }
        }
        // Flowers: a stem and a head, nodding in the wind.
        for (const Flower& f : m_flowers) {
            if (f.picked || glm::length(f.at - cam) > 80.0f) continue;
            const float sway = 0.06f * windGust(f.at) * (1.0f + std::sin(m_windTime * 2.6f + f.phase));
            const glm::vec3 top = f.at + glm::vec3(0.0f, 0.32f, 0.0f) + kWindDir * sway;
            appendBlade(f.at, top, glm::vec3(0.012f, 0.0f, 0.0f), glm::vec3(0.2f, 0.4f, 0.12f), glm::vec3(0.3f, 0.55f, 0.18f), v, idx);
            appendBlade(f.at, top, glm::vec3(0.0f, 0.0f, 0.012f), glm::vec3(0.2f, 0.4f, 0.12f), glm::vec3(0.3f, 0.55f, 0.18f), v, idx);
            appendBox(glm::translate(glm::mat4(1.0f), top), glm::vec3(0.06f, 0.015f, 0.06f), kFlowerColors[f.kind], v, idx);
            appendBox(glm::translate(glm::mat4(1.0f), top) * glm::rotate(glm::mat4(1.0f), glm::quarter_pi<float>(), glm::vec3(0, 1, 0)),
                      glm::vec3(0.06f, 0.014f, 0.06f), kFlowerColors[f.kind], v, idx);
            appendBox(glm::translate(glm::mat4(1.0f), top + glm::vec3(0, 0.016f, 0)), glm::vec3(0.02f, 0.006f, 0.02f), glm::vec3(0.95f, 0.8f, 0.2f), v, idx);
        }
        // Grass: a blade or two every half metre round you, leaning with
        // the wind and pushed aside by your legs.
        const glm::vec2 c(feet.x, feet.z);
        if (glm::length(c - kPark) < kParkR + kGrassRadius) {
            const int r = static_cast<int>(kGrassRadius / kGrassCell);
            const int ci = static_cast<int>(std::floor(c.x / kGrassCell)), cj = static_cast<int>(std::floor(c.y / kGrassCell));
            for (int j = -r; j <= r; ++j)
                for (int i = -r; i <= r; ++i) {
                    if (i * i + j * j > r * r) continue;
                    const uint32_t cell = hash(static_cast<uint32_t>(ci + i) * 73856093u ^ static_cast<uint32_t>(cj + j) * 19349663u);
                    const glm::vec2 p = glm::vec2(static_cast<float>(ci + i) + unit(cell), static_cast<float>(cj + j) + unit(hash(cell))) * kGrassCell;
                    const float fromPark = glm::length(p - kPark);
                    if (fromPark > kParkR) continue;
                    const bool meadow = glm::length(p - kMeadow) < kMeadowR;
                    if (!meadow && unit(hash(cell + 7u)) < 0.35f) continue; // thinner under the trees
                    if (std::abs(p.x - kChopBlock.x) < 30.0f && std::abs(p.y - kChopBlock.z) < 30.0f && roadDistance(p.x, p.y) < 0.3f) continue;
                    const int blades = meadow ? 2 : 1;
                    for (int b = 0; b < blades; ++b) {
                        const uint32_t hb = hash(cell + 31u * static_cast<uint32_t>(b + 1));
                        const glm::vec2 q = p + glm::vec2(unit(hb) - 0.5f, unit(hash(hb)) - 0.5f) * 0.3f;
                        const glm::vec3 base(q.x, groundHeight(q.x, q.y), q.y);
                        const float h = (meadow ? 0.45f : 0.28f) + 0.3f * unit(hash(hb + 3u));
                        const float phase = unit(hash(hb + 5u)) * 6.28f;
                        glm::vec3 lean = kWindDir * (h * (0.15f + 0.3f * windGust(base) * (0.6f + 0.4f * std::sin(m_windTime * 2.2f + phase))));
                        // Away from your legs.
                        const glm::vec2 away(q.x - feet.x, q.y - feet.z);
                        const float d = glm::length(away);
                        float down = 0.0f;
                        if (d < 0.8f && d > 1e-3f && std::abs(base.y - feet.y) < 1.0f) {
                            const float k = 1.0f - d / 0.8f;
                            lean += glm::vec3(away.x / d, 0.0f, away.y / d) * (h * 0.9f * k);
                            down = h * 0.5f * k;
                        }
                        const glm::vec3 tip = base + glm::vec3(0.0f, h - down, 0.0f) + lean;
                        const float a = unit(hash(hb + 9u)) * 3.1416f;
                        const glm::vec3 side(std::cos(a) * 0.022f, 0.0f, std::sin(a) * 0.022f);
                        const float tone = 0.85f + 0.3f * unit(hash(hb + 11u));
                        const glm::vec3 root = (meadow ? glm::vec3(0.2f, 0.36f, 0.1f) : glm::vec3(0.15f, 0.28f, 0.09f)) * tone;
                        const glm::vec3 top = (meadow ? glm::vec3(0.55f, 0.66f, 0.22f) : glm::vec3(0.36f, 0.5f, 0.18f)) * tone;
                        appendBlade(base, tip, side, root, top, gv, gi);
                    }
                }
        }
    }
    m_natureBatchIndices = idx.size();
    m_grassBatchIndices = gi.size();
    if (!idx.empty()) m_natureBatch->upload(v, idx);
    if (!gi.empty()) m_grassBatch->upload(gv, gi);
}

// KKE_DEMO_NATURE=1: the axe from the chopping block, a tree felled, a log
// split, three flowers picked, a walk through the snow. Logs each step.
void ShowcaseModule::updateNatureDemo(float dt, bool& chop) {
    m_demoNature += dt;
    const float t = m_demoNature;
    auto at = [&](float mark) { return t >= mark && t - dt < mark; };
    kke::RigidWorld& w = m_rigid->world();
    if (at(0.3f)) {
        travelTo(5);
        m_loco->teleport(kChopBlock + glm::vec3(0.0f, groundHeight(kChopBlock.x, kChopBlock.z) + 0.05f, 0.9f));
        m_loco->setFacing(glm::vec3(0, 0, -1));
        m_rig.yaw = 0.0f;
    }
    if (at(1.0f)) {
        const int item = itemInReach();
        if (item >= 0) takeItem(item);
        kke::log::get(name())->info("nature demo: {} in the right hand", axeInHand() ? "the axe" : "nothing");
    }
    if (at(1.5f)) {
        // To the nearest tree, a step from its trunk, facing it.
        const glm::vec3 me = w.characterPosition(m_player);
        int best = -1;
        float bestD = 1e9f;
        for (size_t k = 0; k < m_trees.size(); ++k)
            if (m_trees[k].state == TreeState::Standing && glm::length(m_trees[k].base - me) < bestD) {
                bestD = glm::length(m_trees[k].base - me);
                best = static_cast<int>(k);
            }
        if (best >= 0) {
            const Tree& tr = m_trees[static_cast<size_t>(best)];
            glm::vec3 from = me - tr.base;
            from.y = 0.0f;
            from = glm::normalize(from);
            const glm::vec3 stand = tr.base + from * (tr.trunk + 0.8f);
            m_loco->teleport(glm::vec3(stand.x, groundHeight(stand.x, stand.z) + 0.05f, stand.z));
            m_loco->setFacing(-from);
            m_rig.yaw = glm::degrees(std::atan2(-from.x, from.z)) + 30.0f;
        }
    }
    for (float mark : { 2.2f, 3.1f, 4.0f, 4.9f }) chop = chop || at(mark);
    if (at(10.0f)) {
        // To the first log and split it.
        for (const Prop& p : m_props)
            if (p.shape == PropShape::Barrel && p.color == kLogColor) {
                const glm::vec3 lp = w.position(p.body);
                const glm::vec3 me = w.characterPosition(m_player);
                glm::vec3 from = me - lp;
                from.y = 0.0f;
                from = glm::length(from) > 0.1f ? glm::normalize(from) : glm::vec3(1, 0, 0);
                const glm::vec3 stand = lp + from * 1.1f;
                m_loco->teleport(glm::vec3(stand.x, groundHeight(stand.x, stand.z) + 0.05f, stand.z));
                m_loco->setFacing(-from);
                break;
            }
    }
    chop = chop || at(10.6f);
    if (at(12.0f)) {
        int logs = 0;
        for (const Prop& p : m_props) logs += p.shape == PropShape::Barrel && p.color == kLogColor ? 1 : 0;
        kke::log::get(name())->info("nature demo: {} tree(s) felled, {} logs lying, {} piles of firewood on the ground", m_felled, logs,
                                    std::count_if(m_worldItems.begin(), m_worldItems.end(), [](const WorldItem& i) { return i.id == "firewood"; }));
    }
    // Three flowers.
    for (int k = 0; k < 3; ++k) {
        const float mark = 12.5f + 1.2f * static_cast<float>(k);
        if (at(mark) && static_cast<size_t>(k) < m_flowers.size()) {
            const Flower& f = m_flowers[static_cast<size_t>(k)];
            m_loco->teleport(f.at + glm::vec3(0.0f, 0.05f, 0.6f));
            m_loco->setFacing(glm::vec3(0, 0, -1));
            m_rig.yaw = 20.0f;
        }
        if (at(mark + 0.5f)) pickFlower(flowerInReach());
    }
    if (at(16.5f)) {
        travelTo(6);
        m_loco->teleport(glm::vec3(kSnow.x + 10.0f, groundHeight(kSnow.x + 10.0f, kSnow.y + 20.0f) + 0.05f, kSnow.y + 20.0f));
        m_loco->setFacing(glm::vec3(0, 0, -1));
        m_rig.yaw = 0.0f;
        m_rig.pitch = -15.0f;
    }
    if (t > 17.0f && t < 32.0f) m_moveInput = glm::vec2(std::sin(t * 0.6f) * 0.6f, 1.0f); // a winding walk
    if (at(32.0f)) {
        m_rig.yaw += 180.0f; // looking back along the trail
        m_rig.pitch = -30.0f;
    }
    if (at(33.0f)) {
        int pressed = 0;
        for (float p : m_snowPress) pressed += p > 0.4f ? 1 : 0;
        kke::log::get(name())->info("nature demo: {} snow cells pressed by the walk; flowers in the bag: {} {} {}", pressed, m_inv.count("flower_red"),
                                    m_inv.count("flower_yellow"), m_inv.count("flower_blue"));
    }
    if (at(36.0f)) {
        kke::log::get(name())->info("nature demo: done");
        m_demoNature = -1.0f;
    }
}

} // namespace kke_showcase
