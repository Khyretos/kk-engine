#include "Cars.h"

#include "kke/Log.h"
#include "kke/SceneLoader.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace racing {

const std::vector<CarType>& carTypes() {
    // Tuned so every kind can win somewhere: the stock car and the exotic
    // are fastest on the oval, the light hatch and the sports car turn
    // best, the ute is heavy but hard to push around.
    static const std::vector<CarType> types = {
        { "muscle", "Stock car", "Muscle", 1500.0f, 640.0f, 7200.0f, 0, 1.05f, 32.0f, { 2.0f, 1.35f, 5.4f } },
        { "sports", "Sports", "Sports", 1250.0f, 500.0f, 7800.0f, 0, 1.12f, 36.0f, { 1.95f, 1.3f, 4.8f } },
        { "exotic", "Exotic", "Exotic", 1400.0f, 700.0f, 8200.0f, 2, 1.1f, 34.0f, { 2.2f, 1.25f, 5.1f } },
        { "sedan", "Sedan", "Sedan", 1450.0f, 470.0f, 6800.0f, 1, 1.02f, 34.0f, { 2.1f, 1.5f, 5.3f } },
        { "hatch", "Hatch", "Hatch", 1080.0f, 360.0f, 7500.0f, 1, 1.1f, 38.0f, { 1.95f, 1.6f, 4.3f } },
        { "ute", "Ute", "Ute", 1750.0f, 620.0f, 6500.0f, 0, 0.98f, 32.0f, { 1.9f, 1.7f, 5.1f } },
    };
    return types;
}

int carTypeIndex(const std::string& id) {
    const auto& t = carTypes();
    for (size_t i = 0; i < t.size(); ++i)
        if (id == t[i].id) return static_cast<int>(i);
    return -1;
}

const std::vector<Paint>& paints() {
    static const std::vector<Paint> list = {
        { "Race Red", "02_Race_Red", { 0.85f, 0.15f, 0.12f } },          { "Race Blue", "13_Race_Blue", { 0.2f, 0.45f, 0.9f } },
        { "Race Yellow", "07_Race_Yellow", { 0.95f, 0.8f, 0.15f } },     { "Race Green", "08_Race_Green", { 0.2f, 0.7f, 0.3f } },
        { "Race Purple", "01_Race_Purple", { 0.5f, 0.25f, 0.8f } },      { "Race Grey", "05_Race_Grey", { 0.5f, 0.5f, 0.52f } },
        { "Classic Black", "06_Classic_Black", { 0.08f, 0.08f, 0.09f } }, { "Flames", "11_Flames", { 0.9f, 0.4f, 0.1f } },
        { "Tiger", "14_Tiger", { 0.95f, 0.6f, 0.1f } },                  { "Patriot", "09_Patriot", { 0.2f, 0.3f, 0.7f } },
        { "Rip and Tear", "10_Rip_And_Tear", { 0.6f, 0.1f, 0.1f } },     { "Digital Square", "04_Digital_Square", { 0.3f, 0.8f, 0.9f } },
        { "Digital Triangle", "12_Digital_Triangle", { 0.7f, 0.3f, 0.8f } }, { "Paint Drip", "15_Paint_Drip", { 0.9f, 0.3f, 0.6f } },
        { "Pup", "16_Pup", { 0.95f, 0.75f, 0.55f } },                    { "Camo", "17_Camo", { 0.4f, 0.45f, 0.3f } },
        { "Taxi", "21_Taxi", { 0.95f, 0.8f, 0.2f } },                    { "Police", "19_Police_White", { 0.9f, 0.9f, 0.92f } },
        { "Port", "20_Port", { 0.9f, 0.5f, 0.1f } },                     { "Apoco Blue", "22_Apoco_Blue", { 0.25f, 0.55f, 0.85f } },
        { "Apoco Orange", "22_Apoco_Orange", { 0.95f, 0.5f, 0.15f } },   { "Rust", "24_Rust", { 0.55f, 0.3f, 0.15f } },
        { "Pearl Blue", "Pearl_01_Blue", { 0.35f, 0.55f, 0.95f } },      { "Pearl Red", "Pearl_03_Red", { 0.8f, 0.2f, 0.25f } },
    };
    return list;
}

namespace {

// A box whose faces are split into a grid of cells (so a dent bends it).
void addGridBox(kke::ModelMesh& m, const glm::vec3& center, const glm::vec3& half, int cells) {
    for (int axis = 0; axis < 3; ++axis)
        for (int sign = -1; sign <= 1; sign += 2) {
            glm::vec3 n(0.0f);
            n[axis] = static_cast<float>(sign);
            const glm::vec3 u = axis == 0 ? glm::vec3(0, 0, 1) : axis == 1 ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
            const glm::vec3 v = glm::cross(n, u);
            const uint32_t base = static_cast<uint32_t>(m.vertices.size());
            for (int j = 0; j <= cells; ++j)
                for (int i = 0; i <= cells; ++i) {
                    const float a = static_cast<float>(i) / static_cast<float>(cells) * 2.0f - 1.0f;
                    const float b = static_cast<float>(j) / static_cast<float>(cells) * 2.0f - 1.0f;
                    kke::ModelVertex vert;
                    vert.position = center + (n + u * a + v * b) * half;
                    vert.normal = n;
                    vert.uv = glm::vec2(static_cast<float>(i), static_cast<float>(j)) / static_cast<float>(cells);
                    m.vertices.push_back(vert);
                }
            const uint32_t row = static_cast<uint32_t>(cells + 1);
            for (int j = 0; j < cells; ++j)
                for (int i = 0; i < cells; ++i) {
                    const uint32_t a = base + static_cast<uint32_t>(j) * row + static_cast<uint32_t>(i);
                    // u x v = n: counter-clockwise seen from outside.
                    m.indices.insert(m.indices.end(), { a, a + 1, a + row + 1, a, a + row + 1, a + row });
                }
        }
}

void boundsOf(kke::ModelData& d) {
    glm::vec3 mn(1e9f), mx(-1e9f);
    for (const kke::ModelMesh& m : d.meshes)
        for (const kke::ModelVertex& v : m.vertices) {
            mn = glm::min(mn, v.position);
            mx = glm::max(mx, v.position);
        }
    d.boundsMin = mn;
    d.boundsMax = mx;
}

// A wheel: a tyre (a 16-sided cylinder along X) and a hub cap.
kke::ModelData blockWheel(float radius, float width, bool left) {
    kke::ModelData d;
    d.materials.push_back({ "tyre", glm::vec3(0.06f, 0.06f, 0.07f), 0.0f, 0.9f, "", "" });
    d.materials.push_back({ "hub", glm::vec3(0.7f, 0.72f, 0.75f), 0.8f, 0.3f, "", "" });
    kke::ModelMesh tyre, hub;
    tyre.material = 0;
    hub.material = 1;
    const int sides = 16;
    const float hw = width * 0.5f, outer = left ? 1.0f : -1.0f;
    for (int i = 0; i < sides; ++i) {
        const float a0 = glm::two_pi<float>() * static_cast<float>(i) / sides, a1 = glm::two_pi<float>() * static_cast<float>(i + 1) / sides;
        const glm::vec3 r0(0.0f, std::cos(a0), std::sin(a0)), r1(0.0f, std::cos(a1), std::sin(a1));
        const uint32_t b = static_cast<uint32_t>(tyre.vertices.size());
        auto vert = [](const glm::vec3& p, const glm::vec3& n) {
            kke::ModelVertex v;
            v.position = p;
            v.normal = n;
            return v;
        };
        tyre.vertices.push_back(vert(glm::vec3(-hw, 0, 0) + r0 * radius, r0));
        tyre.vertices.push_back(vert(glm::vec3(hw, 0, 0) + r0 * radius, r0));
        tyre.vertices.push_back(vert(glm::vec3(hw, 0, 0) + r1 * radius, r1));
        tyre.vertices.push_back(vert(glm::vec3(-hw, 0, 0) + r1 * radius, r1));
        tyre.indices.insert(tyre.indices.end(), { b, b + 2, b + 1, b, b + 3, b + 2 });
        // Both side walls, the outer one a lighter hub.
        for (int side = -1; side <= 1; side += 2) {
            kke::ModelMesh& m = static_cast<float>(side) == outer ? hub : tyre;
            const uint32_t c = static_cast<uint32_t>(m.vertices.size());
            const glm::vec3 n(static_cast<float>(side), 0.0f, 0.0f), x(static_cast<float>(side) * hw, 0.0f, 0.0f);
            m.vertices.push_back(vert(x, n));
            m.vertices.push_back(vert(x + r0 * radius, n));
            m.vertices.push_back(vert(x + r1 * radius, n));
            if (side > 0) m.indices.insert(m.indices.end(), { c, c + 1, c + 2 });
            else m.indices.insert(m.indices.end(), { c, c + 2, c + 1 });
        }
    }
    d.meshes.push_back(std::move(tyre));
    d.meshes.push_back(std::move(hub));
    boundsOf(d);
    return d;
}

} // namespace

CarGarage::CarGarage(kke::ModelModule* models, const kke::AssetCatalog* catalog) : m_models(models), m_catalog(catalog) {
    if (!catalog) return;
    for (const kke::CatalogPack& p : catalog->packs)
        if (p.name == "POLYGON_Street_Racer") m_packRoot = p.root;
    if (m_packRoot.empty()) return;
    // The paint jobs and the atlas.
    std::error_code ec;
    for (const auto& e : std::filesystem::recursive_directory_iterator(m_packRoot, ec)) {
        if (e.path().filename() == "PolygonStreetRacer_Veh_Tex_01_Race_Purple.png") m_textures = e.path().parent_path().parent_path().string();
        if (!m_textures.empty()) break;
    }
}

const CarArt& CarGarage::art(int type, int kit, int paint) {
    type = std::clamp(type, 0, static_cast<int>(carTypes().size()) - 1);
    kit = std::clamp(kit, 0, kKits - 1);
    paint = std::clamp(paint, 0, static_cast<int>(paints().size()) - 1);
    const std::string key = fmt::format("{}#{}#{}", type, hasPack() ? kit : 0, paint);
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return it->second;
    CarArt a = hasPack() ? loadSynty(type, kit, paint) : CarArt{};
    if (!a.body) a = makeBlock(type, paint);
    return m_cache.emplace(key, std::move(a)).first->second;
}

void CarGarage::keepWheel(CarArt& a, int side, const kke::ModelData& wheel) {
    a.wheelPositions[side].clear();
    a.wheelNormals[side].clear();
    for (const kke::ModelMesh& m : wheel.meshes) {
        std::vector<glm::vec3> p, n;
        for (const kke::ModelVertex& v : m.vertices) {
            p.push_back(v.position);
            n.push_back(v.normal);
        }
        a.wheelPositions[side].push_back(std::move(p));
        a.wheelNormals[side].push_back(std::move(n));
    }
}

void CarGarage::finish(CarArt& a, const kke::ModelData& body) {
    a.positions.clear();
    a.normals.clear();
    a.indices.clear();
    for (const kke::ModelMesh& m : body.meshes) {
        std::vector<glm::vec3> p, n;
        p.reserve(m.vertices.size());
        n.reserve(m.vertices.size());
        for (const kke::ModelVertex& v : m.vertices) {
            p.push_back(v.position);
            n.push_back(v.normal);
        }
        a.positions.push_back(std::move(p));
        a.normals.push_back(std::move(n));
        a.indices.push_back(m.indices);
    }
    a.boundsMin = body.boundsMin;
    a.boundsMax = body.boundsMax;
    // The chassis for Jolt: a car-shaped hull. The floor between the axles
    // (above the road by the wheels' clearance), the belt line out to the
    // body's width, the roof narrower and shorter.
    const glm::vec3 mn = a.boundsMin, mx = a.boundsMax;
    const float floor = std::max(mn.y + 0.22f, a.wheelRadius * 0.7f);
    const float belt = glm::mix(floor, mx.y, 0.55f);
    const float w = (mx.x - mn.x) * 0.5f - 0.03f, cx = (mx.x + mn.x) * 0.5f;
    const float len = mx.z - mn.z;
    a.hull = {
        { cx - w, floor, mn.z + 0.15f },         { cx + w, floor, mn.z + 0.15f },
        { cx - w, floor, mx.z - 0.15f },         { cx + w, floor, mx.z - 0.15f },
        { cx - w, belt, mn.z },                  { cx + w, belt, mn.z },
        { cx - w, belt, mx.z },                  { cx + w, belt, mx.z },
        { cx - w * 0.78f, mx.y - 0.05f, mn.z + len * 0.3f }, { cx + w * 0.78f, mx.y - 0.05f, mn.z + len * 0.3f },
        { cx - w * 0.78f, mx.y - 0.05f, mn.z + len * 0.62f }, { cx + w * 0.78f, mx.y - 0.05f, mn.z + len * 0.62f },
    };
}

CarArt CarGarage::loadSynty(int type, int kit, int paint) {
    CarArt a;
    const CarType& t = carTypes()[static_cast<size_t>(type)];
    const std::string asset = fmt::format("SK_Veh_Preset_{}_0{}", t.synty, kit + 1);
    const kke::CatalogAsset* found = m_catalog->find(asset, { "POLYGON_Street_Racer" });
    if (!found) {
        kke::log::get("Racing")->info("{} is not in the Street Racer pack here: a block car instead", asset);
        return a;
    }
    const std::string livery = m_textures + "/Vehicles/PolygonStreetRacer_Veh_Tex_" + paints()[static_cast<size_t>(paint)].texture + ".png";
    const std::string carbon = m_textures + "/Vehicles/PolygonStreetRacer_Texture_Carbon.png";
    kke::ModelLoadOptions opts = kke::packLoadOptions(*m_catalog, *found);
    opts.loadAnimations = false;
    kke::ModelData src;
    try {
        src = kke::loadModel(found->path, opts);
    } catch (const std::exception& e) {
        kke::log::get("Racing")->warn("{}: {}", asset, e.what());
        return a;
    }
    // The pack's own texture names don't lead to the paint (its FBX files
    // point at the plain atlas, the livery at a container's texture). A
    // paint job is a whole atlas (the body colour up top, badges, plates
    // and lights below), so the body and livery materials both get it,
    // carbon parts the carbon weave, glass stays a colour.
    for (kke::ModelMaterial& m : src.materials) {
        if (m.name.find("Glass") != std::string::npos) {
            m.baseColor = glm::vec3(0.06f, 0.08f, 0.1f);
            m.roughness = 0.1f;
            m.metallic = 0.0f;
            m.albedoTexture.clear();
            continue;
        }
        m.albedoTexture = m.name.find("Carbon") != std::string::npos ? carbon : livery;
        m.baseColor = glm::vec3(1.0f);
    }
    // Wheels out (the physics turns them), the rest merged per material.
    kke::ModelData body, wheels[2];
    body.sourcePath = wheels[0].sourcePath = wheels[1].sourcePath = src.sourcePath;
    body.materials = wheels[0].materials = wheels[1].materials = src.materials;
    kke::ModelData steering;
    steering.sourcePath = src.sourcePath;
    steering.materials = src.materials;
    std::vector<kke::ModelMesh> merged(src.materials.size());
    for (size_t m = 0; m < merged.size(); ++m) {
        merged[m].material = static_cast<uint32_t>(m);
        merged[m].name = src.materials[m].name;
    }
    const char* corners[4] = { "Wheel_fl", "Wheel_fr", "Wheel_rl", "Wheel_rr" };
    glm::vec3 wmin[4], wmax[4];
    bool have[4] = {};
    for (int c = 0; c < 4; ++c) {
        wmin[c] = glm::vec3(1e9f);
        wmax[c] = glm::vec3(-1e9f);
    }
    // Some presets number their wheels (SK_Veh_Sports_03_Wheel_39..42, not
    // _fl.._rr): those four go to corners by where they sit, front (+Z) and
    // left (+X) of their middle.
    auto numberedWheel = [](const std::string& n) {
        const size_t at = n.rfind("_Wheel_");
        return at != std::string::npos && n.find("Steering") == std::string::npos && at + 7 < n.size() &&
               std::all_of(n.begin() + static_cast<long>(at) + 7, n.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)) != 0; });
    };
    auto centre = [](const kke::ModelMesh& m) {
        glm::vec3 lo(1e9f), hi(-1e9f);
        for (const kke::ModelVertex& v : m.vertices) {
            lo = glm::min(lo, v.position);
            hi = glm::max(hi, v.position);
        }
        return (lo + hi) * 0.5f;
    };
    glm::vec3 numberedMid(0.0f);
    int numbered = 0;
    for (const kke::ModelMesh& m : src.meshes)
        if (numberedWheel(m.name) && !m.vertices.empty()) {
            numberedMid += centre(m);
            ++numbered;
        }
    if (numbered == 4) numberedMid /= 4.0f;
    for (const kke::ModelMesh& m : src.meshes) {
        int corner = -1;
        for (int c = 0; c < 4; ++c)
            if (m.name.find(corners[c]) != std::string::npos) corner = c;
        if (corner < 0 && numbered == 4 && numberedWheel(m.name) && !m.vertices.empty()) {
            const glm::vec3 at = centre(m);
            corner = (at.z > numberedMid.z ? 0 : 2) + (at.x > numberedMid.x ? 0 : 1); // fl fr rl rr
        }
        if (corner >= 0) {
            have[corner] = true;
            for (const kke::ModelVertex& v : m.vertices) {
                wmin[corner] = glm::min(wmin[corner], v.position);
                wmax[corner] = glm::max(wmax[corner], v.position);
            }
            if (corner < 2) wheels[corner].meshes.push_back(m); // front-left, front-right: the models
            continue;
        }
        // The steering wheel turns: a model of its own.
        if (m.name.find("SteeringW") != std::string::npos && !m.vertices.empty()) {
            steering.meshes.push_back(m);
            continue;
        }
        kke::ModelMesh& into = merged[std::min<size_t>(m.material, merged.size() - 1)];
        const uint32_t base = static_cast<uint32_t>(into.vertices.size());
        into.vertices.insert(into.vertices.end(), m.vertices.begin(), m.vertices.end());
        for (uint32_t i : m.indices) into.indices.push_back(base + i);
    }
    if (!have[0] || !have[1] || !have[2] || !have[3]) {
        kke::log::get("Racing")->info("{}: wheels not found by name: a block car instead", asset);
        return a;
    }
    for (kke::ModelMesh& m : merged)
        if (!m.indices.empty()) body.meshes.push_back(std::move(m));
    boundsOf(body);
    for (int c = 0; c < 4; ++c) a.wheelCenter[c] = (wmin[c] + wmax[c]) * 0.5f;
    a.wheelRadius = (wmax[0].y - wmin[0].y) * 0.5f;
    a.wheelWidth = wmax[0].x - wmin[0].x;
    for (int side = 0; side < 2; ++side) {
        for (kke::ModelMesh& m : wheels[side].meshes)
            for (kke::ModelVertex& v : m.vertices) v.position -= a.wheelCenter[side];
        boundsOf(wheels[side]);
        keepWheel(a, side, wheels[side]);
    }
    const std::string key = fmt::format("racing/{}/{}", asset, paints()[static_cast<size_t>(paint)].texture);
    for (size_t p = 0; p < body.meshes.size(); ++p)
        if (src.materials[body.meshes[p].material].name.find("Glass") != std::string::npos) a.glassParts.push_back(p);
    if (!steering.meshes.empty()) {
        // Its middle, and the column: the wheel's thinnest way through
        // (the smallest spread of its vertices), pointing back at the
        // driver. The driver's eyes are behind and above it.
        glm::vec3 lo(1e9f), hi(-1e9f);
        size_t n = 0;
        for (const kke::ModelMesh& m : steering.meshes)
            for (const kke::ModelVertex& v : m.vertices) {
                lo = glm::min(lo, v.position);
                hi = glm::max(hi, v.position);
                ++n;
            }
        const glm::vec3 mid = (lo + hi) * 0.5f;
        glm::mat3 spread(0.0f);
        for (const kke::ModelMesh& m : steering.meshes)
            for (const kke::ModelVertex& v : m.vertices) spread += glm::outerProduct(v.position - mid, v.position - mid);
        spread = spread / static_cast<float>(std::max<size_t>(n, 1)) + glm::mat3(1e-6f);
        // The smallest eigenvector: power iteration on the inverse.
        const glm::mat3 inv = glm::inverse(spread);
        glm::vec3 axis(0.0f, 0.3f, -1.0f);
        for (int i = 0; i < 40; ++i) axis = glm::normalize(inv * axis);
        if (axis.z > 0.0f) axis = -axis;
        a.steerCenter = mid;
        a.steerAxis = axis;
        for (kke::ModelMesh& m : steering.meshes)
            for (kke::ModelVertex& v : m.vertices) v.position -= mid;
        boundsOf(steering);
        a.steering = m_models->add(std::move(steering), key + "/steering");
        // A seated driver: the eyes about 60 cm behind the wheel's middle
        // and a third of a metre above it.
        a.eye = mid + glm::vec3(0.0f, 0.33f, -0.6f);
        a.interior = true;
    }
    finish(a, body);
    a.body = m_models->add(std::move(body), key + "/body");
    a.wheel[0] = m_models->add(std::move(wheels[0]), key + "/wheel_l");
    a.wheel[1] = m_models->add(std::move(wheels[1]), key + "/wheel_r");
    a.synty = a.body != 0;
    if (std::find(m_used.begin(), m_used.end(), asset) == m_used.end()) m_used.push_back(asset);
    return a;
}

CarArt CarGarage::makeBlock(int type, int paint) {
    CarArt a;
    const CarType& t = carTypes()[static_cast<size_t>(type)];
    const glm::vec3 size = t.blockSize;
    const float hw = size.x * 0.5f, hl = size.z * 0.5f;
    a.wheelRadius = 0.34f;
    a.wheelWidth = 0.28f;
    const float wx = hw - a.wheelWidth * 0.5f - 0.02f, wz = hl * 0.66f;
    a.wheelCenter[0] = { wx, a.wheelRadius, wz };
    a.wheelCenter[1] = { -wx, a.wheelRadius, wz };
    a.wheelCenter[2] = { wx, a.wheelRadius, -wz };
    a.wheelCenter[3] = { -wx, a.wheelRadius, -wz };
    kke::ModelData body;
    const glm::vec3 color = paints()[static_cast<size_t>(paint)].color;
    body.materials.push_back({ "paint", color, 0.3f, 0.35f, "", "" });
    body.materials.push_back({ "glass", glm::vec3(0.06f, 0.08f, 0.1f), 0.0f, 0.1f, "", "" });
    body.materials.push_back({ "trim", glm::vec3(0.12f, 0.12f, 0.13f), 0.2f, 0.6f, "", "" });
    kke::ModelMesh paintMesh, glass, trim;
    paintMesh.material = 0;
    glass.material = 1;
    trim.material = 2;
    const float floor = 0.3f, belt = size.y * 0.62f;
    addGridBox(paintMesh, { 0.0f, (floor + belt) * 0.5f, 0.0f }, { hw, (belt - floor) * 0.5f, hl }, 8);
    addGridBox(glass, { 0.0f, (belt + size.y) * 0.5f, -hl * 0.12f }, { hw * 0.82f, (size.y - belt) * 0.5f, hl * 0.42f }, 4);
    addGridBox(trim, { 0.0f, floor + 0.18f, hl + 0.06f }, { hw * 0.96f, 0.14f, 0.08f }, 3);
    addGridBox(trim, { 0.0f, floor + 0.18f, -hl - 0.06f }, { hw * 0.96f, 0.14f, 0.08f }, 3);
    body.meshes.push_back(std::move(paintMesh));
    body.meshes.push_back(std::move(glass));
    body.meshes.push_back(std::move(trim));
    boundsOf(body);
    finish(a, body);
    a.glassParts = { 1 };
    a.eye = glm::vec3(hw * 0.4f, belt + 0.3f, -hl * 0.1f);
    const std::string key = fmt::format("racing/block/{}/{}", t.id, paint);
    a.body = m_models->add(std::move(body), key + "/body");
    for (int side = 0; side < 2; ++side) {
        kke::ModelData wheel = blockWheel(a.wheelRadius, a.wheelWidth, side == 0);
        keepWheel(a, side, wheel);
        a.wheel[side] = m_models->add(std::move(wheel), key + (side == 0 ? "/wheel_l" : "/wheel_r"));
    }
    return a;
}

kke::VehicleDesc vehicleDesc(const CarType& t, const CarArt& art, bool manualGearbox, bool drift) {
    kke::VehicleDesc d;
    d.hull = art.hull;
    d.mass = t.mass;
    // Low and a little forward: the engine's weight.
    const glm::vec3 mid = (art.boundsMin + art.boundsMax) * 0.5f;
    d.centerOfMassOffset = glm::vec3(0.0f, art.wheelRadius * 0.9f - mid.y, 0.1f);
    d.maxTorque = t.torque;
    d.maxRpm = t.maxRpm;
    d.minRpm = 900.0f;
    d.shiftUpRpm = t.maxRpm * 0.88f;
    d.shiftDownRpm = t.maxRpm * 0.42f;
    d.manualGearbox = manualGearbox;
    d.gearRatios = { 2.9f, 2.0f, 1.5f, 1.18f, 0.96f, 0.8f };
    d.differentialRatio = 3.6f;
    d.limitedSlipRatio = drift ? 1.2f : 1.4f;
    d.antiRollStiffness = 9000.0f;
    d.friction = 0.35f;
    d.restitution = 0.15f;
    if (t.drive == 0) d.drivenAxles = { 1 };
    else if (t.drive == 1) d.drivenAxles = { 0 };
    else d.drivenAxles = { 0, 1 };
    for (int c = 0; c < 4; ++c) {
        kke::VehicleWheelDesc w;
        const bool front = c < 2;
        // Hung so that, settled on its springs, the wheel is where the art
        // has it: at rest the spring is squeezed ~6 cm from its 30 cm.
        w.position = art.wheelCenter[c] + glm::vec3(0.0f, 0.24f, 0.0f);
        w.radius = art.wheelRadius;
        w.width = art.wheelWidth;
        w.suspensionMin = 0.06f;
        w.suspensionMax = 0.3f;
        w.suspensionFrequency = 2.0f;
        w.suspensionDamping = 0.55f;
        w.maxSteerDegrees = front ? t.steer : 0.0f;
        w.maxBrakeTorque = front ? 2600.0f : 1400.0f; // biased forward: throttle + brake at the line spins the rears (a burnout)
        w.maxHandBrakeTorque = front ? 0.0f : 5000.0f;
        w.lateralGrip = t.grip * (drift && !front ? 0.86f : 1.0f);
        w.longitudinalGrip = 1.25f * t.grip;
        w.slideGrip = drift ? 0.72f : 0.82f;
        w.combinedSlipLoss = drift && !front ? 0.75f : 0.6f;
        d.wheels.push_back(w);
    }
    return d;
}

} // namespace racing
