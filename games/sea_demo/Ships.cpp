// The ship classes and their art (Ships.h).

#include "Ships.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/SceneLoader.h"
#include "kke/SphereImpostors.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace kke_sea {

namespace {

constexpr const char* kPack = "POLYGON_Pirate_Pack";

// Speeds from the real ships, scaled down a little so a fight stays in
// view: a cutter makes ~8 kn, a schooner 11, a brig 10, a ship of the line
// 8 (and turns like a cathedral).
const std::vector<ShipClass> kClasses = {
    { "rowing", "Rowing boat", "Light and nimble: oars, no sails, one swivel gun a side", "SM_Veh_Boat_Rowing_01_Hull_Pirate", {}, {}, {},
      5.4f, 2.0f, 1.2f, 3.6f, 1.6f, 45.0f, true, 1, 1, 2.5f, 1.5f, 80.0f },
    { "launch", "Longboat", "Rowed, steady, two guns a side", "SM_Veh_Boat_Small_01_Hull_Pirate", {}, {}, {}, 8.5f, 2.9f, 1.6f, 4.6f, 1.2f, 32.0f,
      true, 2, 1, 3.5f, 2.7f, 160.0f },
    { "schooner", "Schooner", "Quick and agile under sail, four guns a side", "SM_Veh_Boat_Medium_01_Hull_Pirate", { "SM_Veh_Boat_Medium_01_Mast_Pirate" },
      { "SM_Veh_Boat_Medium_01_Sails_Pirate" }, { "SM_Veh_Boat_Medium_01_Rigging" }, 14.0f, 5.0f, 4.0f, 9.5f, 1.4f, 20.0f, false, 4, 1, 4.5f, 2.7f,
      380.0f },
    { "brig", "Brig", "The all-rounder: two masts, eight guns a side", "SM_Veh_Veh_Boat_Large_01_Hull_Pirate",
      { "SM_Veh_Boat_Large_Mast_01_Pirate", "SM_Veh_Boat_Large_Mast_02_Pirate" },
      { "SM_Veh_Boat_Large_Sails_01_Pirate", "SM_Veh_Boat_Large_Sails_02_Pirate", "SM_Veh_Boat_Large_Sails_03_Pirate",
        "SM_Veh_Boat_Large_Front_SailRigging_01_Pirate" },
      { "SM_Veh_Veh_Boat_Large_01_Rigging" }, 26.0f, 8.0f, 6.0f, 8.5f, 0.9f, 13.0f, false, 8, 1, 6.0f, 5.4f, 750.0f },
    { "warship", "Man-o'-war", "Slow and huge: three masts, two decks of guns", "SM_Veh_Boat_Warship_01_Hull_Pirate",
      { "SM_Veh_Boat_Warship_01_Mast_01_Pirate", "SM_Veh_Boat_Warship_01_Mast_02_Pirate", "SM_Veh_Boat_Warship_01_Mast_03_Pirate" },
      { "SM_Veh_Boat_Warship_01_Sails_01_Pirate", "SM_Veh_Boat_Warship_01_Sails_02_Pirate", "SM_Veh_Boat_Warship_01_Sails_03_Pirate",
        "SM_Veh_Boat_Warship_01_Sails_04_Pirate", "SM_Veh_Boat_Warship_01_Sails_05_Pirate" },
      { "SM_Veh_Boat_Warship_01_Rigging_Pirate" }, 34.0f, 10.0f, 9.0f, 7.0f, 0.55f, 8.5f, false, 7, 2, 8.0f, 11.0f, 1400.0f },
};

void appendBox(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx, const glm::vec3& mn, const glm::vec3& mx, const glm::vec3& color,
               float bowTaper = 0.0f) {
    // bowTaper > 0 pinches the +Z end into a bow.
    auto P = [&](float x, float y, float z) {
        glm::vec3 p(x, y, z);
        if (bowTaper > 0.0f && z > 0.0f && mx.z > 0.0f) {
            const float t = z / mx.z;
            p.x *= 1.0f - bowTaper * t;
        }
        return p;
    };
    const glm::vec3 c[8] = { P(mn.x, mn.y, mn.z), P(mx.x, mn.y, mn.z), P(mx.x, mx.y, mn.z), P(mn.x, mx.y, mn.z),
                             P(mn.x, mn.y, mx.z), P(mx.x, mn.y, mx.z), P(mx.x, mx.y, mx.z), P(mn.x, mx.y, mx.z) };
    const int f[6][4] = { { 0, 3, 2, 1 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 }, { 3, 7, 6, 2 }, { 0, 4, 7, 3 }, { 1, 2, 6, 5 } };
    for (const auto& q : f) {
        glm::vec3 n = glm::cross(c[q[1]] - c[q[0]], c[q[2]] - c[q[0]]);
        n = glm::length(n) > 1e-9f ? glm::normalize(n) : glm::vec3(0, 1, 0);
        const uint32_t base = static_cast<uint32_t>(v.size());
        for (int k = 0; k < 4; ++k) v.push_back(kke::Vertex{ c[q[k]], color, n, { 0, 0 } });
        idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    }
}

void bounds(const kke::ModelData& d, const glm::mat4& m, glm::vec3& lo, glm::vec3& hi) {
    lo = glm::vec3(1e9f);
    hi = glm::vec3(-1e9f);
    for (const kke::ModelMesh& mesh : d.meshes)
        for (const kke::ModelVertex& v : mesh.vertices) {
            const glm::vec3 p = glm::vec3(m * glm::vec4(v.position, 1.0f));
            lo = glm::min(lo, p);
            hi = glm::max(hi, p);
        }
}

} // namespace

const std::vector<ShipClass>& shipClasses() { return kClasses; }

void ShipArtLibrary::load(kke::Application& app, kke::ModelModule* models) {
    m_models = models;
    m_art.clear();
    m_art.resize(kClasses.size());
    auto log = kke::log::get("SeaDemo");
    const char* base = SDL_GetBasePath();
    std::vector<std::string> searched;
    const std::string folder = kke::findAssetFolder("assets/synty", { "KKE_ASSETS_DIR", "KKE_SYNTY_DIR" }, base ? base : "", &searched);
    if (!folder.empty()) {
        kke::CatalogScanOptions scan;
        scan.onlyPacks = { kPack };
        m_catalog = kke::AssetCatalog::scan(folder, scan);
    }
    m_packFound = m_models && m_catalog.find(kClasses[0].hull) != nullptr;
    for (size_t i = 0; i < kClasses.size(); ++i) {
        buildBoxes(app, static_cast<int>(i), kClasses[i]);
        if (m_packFound) loadClass(static_cast<int>(i), kClasses[i]);
    }
    size_t loaded = 0;
    for (const ShipArt& a : m_art) loaded += a.loaded ? 1u : 0u;
    if (!m_packFound)
        m_status = "POLYGON Pirate Pack isn't installed: the ships are built from boxes. Put the extracted pack in assets/synty/ or set KKE_ASSETS_DIR "
                   "(README.md \"Ships\").";
    else
        m_status = "Ships from POLYGON Pirate Pack (" + std::to_string(loaded) + " of " + std::to_string(kClasses.size()) + " classes).";
    log->info("{}", m_status);
    if (!m_used.empty()) {
        std::string list;
        for (const std::string& u : m_used) list += (list.empty() ? "" : ", ") + u;
        log->info("pack assets used: {}", list);
    }
}

void ShipArtLibrary::loadClass(int index, const ShipClass& c) {
    auto log = kke::log::get("SeaDemo");
    ShipArt& art = m_art[static_cast<size_t>(index)];
    // Load every part raw: the pack mixes units per file only in the
    // loader's eyes (fixUnitMismatch would scale a tiny rigging file up and
    // not its hull); the parts themselves share one origin and one unit.
    auto loadRaw = [&](const char* name, kke::ModelData& out) {
        const kke::CatalogAsset* asset = m_catalog.find(name, { kPack });
        if (!asset) return false;
        try {
            kke::ModelLoadOptions opts = kke::packLoadOptions(m_catalog, *asset);
            opts.fixUnitMismatch = false;
            opts.loadAnimations = false;
            out = kke::loadModel(asset->path, opts);
        } catch (const std::exception& e) {
            log->warn("{} didn't load ({}): that part is left out", name, e.what());
            return false;
        }
        return !out.meshes.empty();
    };
    kke::ModelData hull;
    if (!loadRaw(c.hull, hull)) return;
    glm::vec3 lo, hi;
    bounds(hull, glm::mat4(1.0f), lo, hi);
    const float rawLength = hi.z - lo.z;
    const float scale = rawLength < 2.0f ? 100.0f : 1.0f; // centimetre files hold metres / 100
    // The bowsprit sticks out at the bow: the longer end.
    const bool bowAtMinusZ = -lo.z > hi.z * 1.15f;
    glm::mat4 toShip = glm::scale(glm::mat4(1.0f), glm::vec3(scale));
    if (bowAtMinusZ) toShip = glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(0, 1, 0)) * toShip;
    art.modelToShip = toShip;
    art.modelScale = scale;
    bounds(hull, toShip, lo, hi);
    const float length = hi.z - lo.z;
    // The rail amidships: the highest hull point near the middle.
    float rail = lo.y, beam = 0.0f;
    const float zMid = (lo.z + hi.z) * 0.5f;
    for (const kke::ModelMesh& m : hull.meshes)
        for (const kke::ModelVertex& v : m.vertices) {
            const glm::vec3 p = glm::vec3(toShip * glm::vec4(v.position, 1.0f));
            if (std::abs(p.z - zMid) < length * 0.08f) {
                rail = std::max(rail, p.y);
                beam = std::max(beam, std::abs(p.x));
            }
        }
    art.deckY = rail - std::min(1.0f, (rail - lo.y) * 0.2f);
    // The hull's real length: below the deck (the bowsprit is above it).
    float hz0 = 1e9f, hz1 = -1e9f;
    for (const kke::ModelMesh& m : hull.meshes)
        for (const kke::ModelVertex& v : m.vertices) {
            const glm::vec3 p = glm::vec3(toShip * glm::vec4(v.position, 1.0f));
            if (p.y < art.deckY - 0.3f) {
                hz0 = std::min(hz0, p.z);
                hz1 = std::max(hz1, p.z);
            }
        }
    if (hz0 > hz1) {
        hz0 = lo.z;
        hz1 = hi.z;
    }
    art.hullCenter = glm::vec3(0.0f, (lo.y + art.deckY) * 0.5f, (hz0 + hz1) * 0.5f);
    art.hullHalf = glm::vec3(std::max(beam * 0.92f, 0.5f), std::max((art.deckY - lo.y) * 0.5f, 0.3f), std::max((hz1 - hz0) * 0.46f, 1.0f));
    // The hull as made, for dents.
    for (const kke::ModelMesh& m : hull.meshes) {
        std::vector<glm::vec3> pos, nrm;
        pos.reserve(m.vertices.size());
        nrm.reserve(m.vertices.size());
        for (const kke::ModelVertex& v : m.vertices) {
            pos.push_back(v.position);
            nrm.push_back(v.normal);
        }
        art.hullPositions.push_back(std::move(pos));
        art.hullNormals.push_back(std::move(nrm));
        art.hullIndices.push_back(m.indices);
    }
    auto addPart = [&](kke::ModelData&& d, ShipArt::Role role, const char* name) {
        ShipArt::Part p;
        p.role = role;
        bounds(d, toShip, p.lo, p.hi);
        p.model = m_models->add(std::move(d), std::string("sea:") + name);
        if (!p.model) return;
        art.parts.push_back(p);
        m_used.push_back(name);
    };
    addPart(std::move(hull), ShipArt::Role::Hull, c.hull);
    for (const char* name : c.masts) {
        kke::ModelData d;
        if (!loadRaw(name, d)) continue;
        // The mast stands where its top is (the yards and booms make the
        // bounds lopsided).
        glm::vec3 top(0.0f, -1e9f, 0.0f), mlo, mhi;
        for (const kke::ModelMesh& m : d.meshes)
            for (const kke::ModelVertex& v : m.vertices) {
                const glm::vec3 p = glm::vec3(toShip * glm::vec4(v.position, 1.0f));
                if (p.y > top.y) top = p;
            }
        bounds(d, toShip, mlo, mhi);
        ShipArt::Mast mast;
        mast.base = glm::vec3(top.x, art.deckY, top.z);
        mast.height = std::max(top.y - art.deckY, 2.0f);
        mast.halfWidth = std::max(mhi.x, -mlo.x);
        art.masts.push_back(mast);
        addPart(std::move(d), ShipArt::Role::Mast, name);
        art.parts.back().mast = static_cast<int>(art.masts.size()) - 1;
    }
    for (const char* name : c.sails) {
        kke::ModelData d;
        if (!loadRaw(name, d)) continue;
        addPart(std::move(d), ShipArt::Role::Sail, name);
        ShipArt::Part& p = art.parts.back();
        const float z = (p.lo.z + p.hi.z) * 0.5f;
        float best = 1e9f;
        for (size_t m = 0; m < art.masts.size(); ++m) {
            const float d2 = std::abs(art.masts[m].base.z - z);
            if (d2 < best) {
                best = d2;
                p.mast = static_cast<int>(m);
            }
        }
    }
    for (const char* name : c.rigging) {
        kke::ModelData d;
        if (loadRaw(name, d)) addPart(std::move(d), ShipArt::Role::Rigging, name);
    }
    // Guns: evenly along the middle 60% of the hull, a little under the
    // rail (upper deck) and, on a two-decker, a deck lower.
    art.guns.clear();
    const float gunLength = (hz1 - hz0) * 0.62f;
    for (int deck = 0; deck < c.gunDecks; ++deck) {
        const float y = art.deckY - 0.35f - static_cast<float>(deck) * 2.1f;
        for (int g = 0; g < c.gunsPerSide; ++g) {
            const float t = c.gunsPerSide == 1 ? 0.5f : static_cast<float>(g) / static_cast<float>(c.gunsPerSide - 1);
            art.guns.push_back(glm::vec3(art.hullHalf.x / 0.92f + 0.15f, y, art.hullCenter.z - gunLength * 0.5f + gunLength * t));
        }
    }
    art.loaded = true;
    log->info("{}: {:.1f} m long, {:.1f} m beam, deck {:.1f} m, {} masts, {} parts (scale {})", c.name, length, beam * 2.0f, art.deckY,
              art.masts.size(), art.parts.size(), scale);
}

void ShipArtLibrary::buildBoxes(kke::Application& app, int index, const ShipClass& c) {
    ShipArt& art = m_art[static_cast<size_t>(index)];
    const float L = c.length, B = c.beam, H = c.height;
    art.deckY = H * 0.62f;
    art.hullCenter = glm::vec3(0.0f, art.deckY * 0.5f, 0.0f);
    art.hullHalf = glm::vec3(B * 0.46f, art.deckY * 0.5f, L * 0.46f);
    {
        std::vector<kke::Vertex> v;
        std::vector<uint32_t> idx;
        const glm::vec3 hullColor(0.42f, 0.27f, 0.16f), deckColor(0.72f, 0.58f, 0.38f), stripe(0.12f, 0.1f, 0.09f);
        appendBox(v, idx, { -B * 0.5f, 0.0f, -L * 0.5f }, { B * 0.5f, art.deckY, L * 0.42f }, hullColor, 0.8f);
        appendBox(v, idx, { -B * 0.47f, art.deckY, -L * 0.48f }, { B * 0.47f, art.deckY + 0.08f, L * 0.4f }, deckColor, 0.8f);
        appendBox(v, idx, { -B * 0.51f, art.deckY - 0.7f, -L * 0.45f }, { B * 0.51f, art.deckY - 0.45f, L * 0.38f }, stripe, 0.8f);
        if (!c.oars) appendBox(v, idx, { -B * 0.45f, art.deckY, -L * 0.5f }, { B * 0.45f, art.deckY + H * 0.25f, -L * 0.32f }, hullColor);
        art.boxHull = std::make_unique<kke::DynamicMeshRenderer>(app);
        art.boxHull->upload(v, idx);
    }
    art.masts.clear();
    art.guns.clear();
    if (!c.oars) {
        const int masts = std::max(1, static_cast<int>(c.masts.size()));
        for (int m = 0; m < masts; ++m) {
            ShipArt::Mast mast;
            const float t = masts == 1 ? 0.5f : static_cast<float>(m) / static_cast<float>(masts - 1);
            mast.base = glm::vec3(0.0f, art.deckY, L * (0.25f - 0.5f * t));
            mast.height = H * 2.6f;
            mast.halfWidth = B * 0.9f;
            art.masts.push_back(mast);
        }
        std::vector<kke::Vertex> v;
        std::vector<uint32_t> idx;
        appendBox(v, idx, { -0.18f, 0.0f, -0.18f }, { 0.18f, 1.0f, 0.18f }, { 0.33f, 0.22f, 0.13f }); // unit-height mast
        appendBox(v, idx, { -0.5f, 0.92f, -0.08f }, { 0.5f, 0.95f, 0.08f }, { 0.33f, 0.22f, 0.13f });  // top yard
        appendBox(v, idx, { -0.6f, 0.55f, -0.08f }, { 0.6f, 0.58f, 0.08f }, { 0.33f, 0.22f, 0.13f });  // lower yard
        art.boxMast = std::make_unique<kke::DynamicMeshRenderer>(app);
        art.boxMast->upload(v, idx);
        v.clear();
        idx.clear();
        appendBox(v, idx, { -0.48f, 0.6f, -0.03f }, { 0.48f, 0.9f, 0.03f }, { 0.93f, 0.9f, 0.82f });
        appendBox(v, idx, { -0.58f, 0.2f, -0.03f }, { 0.58f, 0.53f, 0.03f }, { 0.93f, 0.9f, 0.82f });
        art.boxSail = std::make_unique<kke::DynamicMeshRenderer>(app);
        art.boxSail->upload(v, idx);
    }
    for (int deck = 0; deck < c.gunDecks; ++deck)
        for (int g = 0; g < c.gunsPerSide; ++g) {
            const float t = c.gunsPerSide == 1 ? 0.5f : static_cast<float>(g) / static_cast<float>(c.gunsPerSide - 1);
            art.guns.push_back(glm::vec3(B * 0.52f, art.deckY - 0.35f - static_cast<float>(deck) * 1.6f, -L * 0.3f + L * 0.6f * t));
        }
}

kke::ModelModule::ModelId ShipArtLibrary::prop(const std::string& asset) {
    if (!m_packFound || !m_models) return 0;
    const kke::CatalogAsset* a = m_catalog.find(asset, { kPack });
    if (!a) return 0;
    kke::ModelData d;
    try {
        kke::ModelLoadOptions opts = kke::packLoadOptions(m_catalog, *a);
        opts.loadAnimations = false;
        d = kke::loadModel(a->path, opts);
    } catch (const std::exception& e) {
        kke::log::get("SeaDemo")->warn("{} didn't load ({}): left out", asset, e.what());
        return 0;
    }
    glm::vec3 lo, hi;
    bounds(d, glm::mat4(1.0f), lo, hi);
    const glm::vec3 size = hi - lo;
    // Same rule as the ships: a file that came out a hundredth of its size.
    const float scale = std::max({ size.x, size.y, size.z }) < 0.6f && asset.find("CannonBall") == std::string::npos ? 100.0f : 1.0f;
    const kke::ModelModule::ModelId id = m_models->add(std::move(d), "sea:" + asset);
    if (id) {
        m_propScales.push_back({ id, scale });
        m_used.push_back(asset);
    }
    return id;
}

float ShipArtLibrary::propScale(kke::ModelModule::ModelId id) const {
    for (const auto& p : m_propScales)
        if (p.first == id) return p.second;
    return 1.0f;
}

} // namespace kke_sea
