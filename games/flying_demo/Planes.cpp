// The planes (README.md "The plane"): Synty's POLYGON Stunt Plane when
// the pack is there, split into its parts so the propeller spins and
// the ailerons, elevators, rudder and the pilot's stick move with the
// controls; a plane built from boxes in the pilot's colour when it isn't.
// And the smoke each plane trails.

#include "FlyingModule.h"

#include "kke/AssetCatalog.h"
#include "kke/DevTools.h"
#include "kke/Log.h"
#include "kke/SceneLoader.h"
#include "kke/SphereImpostors.h"

#include <SDL3/SDL.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace flying {

namespace {

constexpr const char* kPack = "POLYGON_StuntPlane";
constexpr const char* kPlane = "SM_Veh_Plane_Stunt_01";
constexpr float kTrailLife = 7.0f;      // s a puff of smoke hangs in the air
constexpr float kPuffSpacing = 1.5f;    // m between puffs

bool endsWith(const std::string& s, const std::string& tail) { return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0; }

void boundsOf(const std::vector<kke::ModelMesh>& meshes, glm::vec3& lo, glm::vec3& hi) {
    lo = glm::vec3(1e9f);
    hi = glm::vec3(-1e9f);
    for (const kke::ModelMesh& m : meshes)
        for (const kke::ModelVertex& v : m.vertices) {
            lo = glm::min(lo, v.position);
            hi = glm::max(hi, v.position);
        }
}

void appendBox(const glm::vec3& center, const glm::vec3& half, const glm::vec3& color, std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx) {
    const glm::vec3 normals[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (const glm::vec3& n : normals) {
        const glm::vec3 u = std::abs(n.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        const glm::vec3 w = glm::cross(n, u);
        const uint32_t base = static_cast<uint32_t>(v.size());
        for (glm::vec2 k : { glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1) })
            v.push_back({ center + (n + u * k.x + w * k.y) * half, color, n, glm::vec2(0.0f) });
        if (glm::dot(glm::cross(u, w), n) >= 0.0f) idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        else idx.insert(idx.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
    }
}

glm::mat4 planeMatrix(const glm::vec3& position, const glm::quat& rotation) { return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation); }

} // namespace

void FlyingModule::loadArt() {
    auto log = kke::log::get(name());
    const char* base = SDL_GetBasePath();
    std::vector<std::string> searched;
    const std::string folder = kke::findAssetFolder("assets/synty", { "KKE_ASSETS_DIR", "KKE_SYNTY_DIR" }, base ? base : "", &searched);
    kke::AssetCatalog catalog;
    if (!folder.empty()) {
        kke::CatalogScanOptions scan;
        scan.onlyPacks = { kPack };
        catalog = kke::AssetCatalog::scan(folder, scan);
    }
    const kke::CatalogAsset* asset = catalog.find(kPlane);
    if (!asset) {
        m_art.status = "POLYGON Stunt Plane isn't installed: the planes are built from boxes. Put the extracted pack in assets/synty/ or set "
                       "KKE_ASSETS_DIR (README.md \"The plane\").";
        log->info("{}", m_art.status); // an optional pack: the HUD says so too
        return;
    }
    kke::ModelData data;
    try {
        data = kke::loadModel(asset->path, kke::packLoadOptions(catalog, *asset));
    } catch (const std::exception& e) {
        m_art.status = std::string("The stunt plane didn't load (") + e.what() + "): the planes are built from boxes.";
        log->warn("{}", m_art.status);
        return;
    }
    // Sort the meshes into parts by name (MaterialList_PolygonStuntPlane.txt).
    struct Part {
        Art::Kind kind;
        std::vector<kke::ModelMesh> meshes;
    };
    std::vector<Part> parts;
    auto partFor = [&](Art::Kind k) -> Part& {
        for (Part& p : parts)
            if (p.kind == k && k != Art::Kind::AileronLeft && k != Art::Kind::AileronRight) return p;
        parts.push_back({ k, {} });
        return parts.back();
    };
    partFor(Art::Kind::Body); // first
    for (kke::ModelMesh& m : data.meshes) {
        const std::string& n = m.name;
        if (n.find("Crop_Duster") != std::string::npos) continue; // the crop-spraying kit: not on a stunt plane
        if (endsWith(n, "_Prop")) partFor(Art::Kind::Prop).meshes.push_back(std::move(m));
        else if (n.find("Flap_fl") != std::string::npos) parts.push_back({ Art::Kind::AileronLeft, { std::move(m) } });
        else if (n.find("Flap_fr") != std::string::npos) parts.push_back({ Art::Kind::AileronRight, { std::move(m) } });
        else if (n.find("Flap_rl") != std::string::npos || n.find("Flap_rr") != std::string::npos) parts.push_back({ Art::Kind::Elevator, { std::move(m) } });
        else if (n.find("Flap_Tail") != std::string::npos) partFor(Art::Kind::Rudder).meshes.push_back(std::move(m));
        else if (n.find("Cockpit_Stick") != std::string::npos) partFor(Art::Kind::Stick).meshes.push_back(std::move(m));
        else partFor(Art::Kind::Body).meshes.push_back(std::move(m));
    }
    glm::vec3 lo, hi;
    boundsOf(parts[0].meshes, lo, hi);
    if (parts[0].meshes.empty() || lo.x > hi.x) {
        m_art.status = "The stunt plane file has no body: the planes are built from boxes.";
        log->warn("{}", m_art.status);
        return;
    }
    // Which way the nose points: toward the propeller.
    glm::vec3 nose(0.0f, 0.0f, 1.0f);
    for (const Part& p : parts)
        if (p.kind == Art::Kind::Prop) {
            glm::vec3 plo, phi;
            boundsOf(p.meshes, plo, phi);
            const glm::vec3 d = (plo + phi) * 0.5f - (lo + hi) * 0.5f;
            nose = std::abs(d.x) > std::abs(d.z) ? glm::vec3(d.x > 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, d.z > 0.0f ? 1.0f : -1.0f);
        }
    const glm::vec3 side = glm::normalize(glm::cross(nose, glm::vec3(0.0f, 1.0f, 0.0f))); // the right wing
    // Metres: a stunt biplane's wings span about 8 m.
    const float span = std::abs(glm::dot(hi - lo, side));
    const float scale = span > 60.0f ? 0.01f : 1.0f;
    const glm::vec3 center((lo.x + hi.x) * 0.5f, lo.y, (lo.z + hi.z) * 0.5f);
    // Model -> plane space: nose to -Z, right wing to +X, wheels' bottom a
    // gear height under the origin.
    glm::mat4 turn(1.0f);
    turn[0] = glm::vec4(side, 0.0f);
    turn[1] = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
    turn[2] = glm::vec4(-nose, 0.0f);
    const glm::mat4 toPlane = glm::scale(glm::mat4(1.0f), glm::vec3(scale)) * glm::transpose(turn) * glm::translate(glm::mat4(1.0f), -center);
    m_art.toPlane = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -m_flight.gearHeight, 0.0f)) * toPlane;

    for (Part& p : parts) {
        if (p.meshes.empty()) continue;
        glm::vec3 plo, phi;
        boundsOf(p.meshes, plo, phi);
        // The hinge: a control surface turns on its front edge; the prop
        // and the stick on their middle and foot.
        glm::vec3 hinge = (plo + phi) * 0.5f, axis = side;
        const float front = glm::dot(nose, glm::dot(nose, phi) > glm::dot(nose, plo) ? phi : plo);
        switch (p.kind) {
        case Art::Kind::Prop: axis = nose; break;
        case Art::Kind::Rudder:
            axis = glm::vec3(0.0f, 1.0f, 0.0f);
            hinge += nose * (front - glm::dot(nose, hinge));
            break;
        case Art::Kind::AileronLeft:
        case Art::Kind::AileronRight:
        case Art::Kind::Elevator: hinge += nose * (front - glm::dot(nose, hinge)); break;
        case Art::Kind::Stick: hinge.y = plo.y; break;
        case Art::Kind::Body: break;
        }
        // As made, for dents (Damage.cpp).
        std::vector<std::vector<glm::vec3>> positions, normals;
        std::vector<std::vector<uint32_t>> indices;
        for (const kke::ModelMesh& m : p.meshes) {
            std::vector<glm::vec3> pos, nrm;
            for (const kke::ModelVertex& v : m.vertices) {
                pos.push_back(v.position);
                nrm.push_back(v.normal);
            }
            positions.push_back(std::move(pos));
            normals.push_back(std::move(nrm));
            indices.push_back(m.indices);
        }
        kke::ModelData d;
        d.sourcePath = data.sourcePath;
        d.materials = data.materials;
        d.meshes = std::move(p.meshes);
        d.boundsMin = plo;
        d.boundsMax = phi;
        const kke::ModelModule::ModelId id = m_models->add(std::move(d), std::string(kPlane) + "#" + std::to_string(m_art.parts.size()));
        if (!id) continue;
        m_art.parts.push_back(id);
        m_art.kinds.push_back(p.kind);
        m_art.hinges.push_back(hinge);
        m_art.axes.push_back(axis);
        m_art.positions.push_back(std::move(positions));
        m_art.normals.push_back(std::move(normals));
        m_art.indices.push_back(std::move(indices));
    }
    m_art.scale = scale;
    m_shape = planeShape(span * scale);
    if (const kke::CatalogPack* pack = catalog.pack(asset->pack)) m_art.liveries = pack->textureVariants;
    m_art.loaded = !m_art.parts.empty();
    log->info("stunt plane: {} parts, {:.1f} m span (scale {}), nose along ({:.0f}, {:.0f}, {:.0f}), {} paint jobs", m_art.parts.size(), span * scale,
              scale, nose.x, nose.y, nose.z, m_art.liveries.size());
}

void FlyingModule::spawnArt(Pilot& p) {
    removeArt(p);
    if (m_art.loaded) {
        for (kke::ModelModule::ModelId id : m_art.parts) {
            const kke::ModelModule::InstanceId inst = m_models->spawn(id);
            if (!m_art.liveries.empty()) m_models->setTextureOverride(inst, m_art.liveries[static_cast<size_t>(p.livery) % m_art.liveries.size()]);
            p.parts.push_back(inst);
        }
        return;
    }
    // No pack: a biplane of boxes, in the pilot's colour (plane space:
    // nose -Z, right wing +X, wheels' bottom a gear height down).
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    const glm::vec3 c = p.tint, dark = p.tint * 0.55f, white(0.92f), black(0.08f);
    const float g = -m_flight.gearHeight;
    appendBox({ 0.0f, g + 1.55f, 0.0f }, { 0.45f, 0.5f, 2.6f }, c, v, idx);          // fuselage
    appendBox({ 0.0f, g + 1.6f, -2.75f }, { 0.4f, 0.4f, 0.2f }, dark, v, idx);        // cowling
    appendBox({ 0.0f, g + 1.6f, -3.0f }, { 1.1f, 0.1f, 0.04f }, black, v, idx);       // propeller
    appendBox({ 0.0f, g + 2.55f, -0.9f }, { 4.0f, 0.06f, 0.7f }, white, v, idx);      // top wing
    appendBox({ 0.0f, g + 1.1f, -0.7f }, { 3.8f, 0.06f, 0.7f }, c, v, idx);           // bottom wing
    for (float x : { -2.8f, 2.8f }) appendBox({ x, g + 1.82f, -0.8f }, { 0.04f, 0.72f, 0.04f }, dark, v, idx); // struts
    appendBox({ 0.0f, g + 1.7f, 2.6f }, { 1.5f, 0.05f, 0.45f }, white, v, idx);       // tailplane
    appendBox({ 0.0f, g + 2.3f, 2.7f }, { 0.05f, 0.65f, 0.4f }, c, v, idx);           // fin
    appendBox({ 0.0f, g + 2.1f, 0.4f }, { 0.35f, 0.18f, 0.4f }, { 0.25f, 0.4f, 0.55f }, v, idx); // cockpit
    for (float x : { -0.9f, 0.9f }) appendBox({ x, g + 0.3f, -1.2f }, { 0.08f, 0.3f, 0.3f }, black, v, idx); // wheels
    p.blockPlane = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    p.blockPlane->upload(v, idx);
    p.blockBase = std::move(v);
    p.blockIndices = std::move(idx);
}

void FlyingModule::removeArt(Pilot& p) {
    for (kke::ModelModule::InstanceId i : p.parts) m_models->remove(i);
    p.parts.clear();
    p.dented.clear();
    p.dentedNormals.clear();
    p.blockDented.clear();
    p.dentsChanged = false;
    if (p.blockPlane) m_app->renderer().retire(std::move(p.blockPlane)); // frames in flight still draw it
}

// Every part where the plane is, turned on its hinge by the controls.
void FlyingModule::poseArt(Pilot& p, float dt) {
    const float throttle = p.remote ? p.net.throttle : p.controls.throttle;
    const bool down = p.remote ? p.net.crashed : (p.plane.crashed || p.respawnIn > 0.0f);
    p.propAngle = std::fmod(p.propAngle + dt * (down ? 0.0f : 900.0f + 2600.0f * throttle), 360.0f);
    if (p.blockPlane && p.dentsChanged) {
        p.dentsChanged = false;
        p.blockPlane->upload(p.blockDented.empty() ? p.blockBase : p.blockDented, p.blockIndices);
    }
    if (p.parts.empty()) return;
    const glm::vec3 position = p.remote ? p.net.position : p.plane.position;
    const glm::quat rotation = p.remote ? p.drawnRotation : p.plane.rotation;
    const glm::mat4 world = planeMatrix(position, rotation) * m_art.toPlane;
    if (p.dentsChanged) {
        p.dentsChanged = false;
        for (size_t i = 0; i < p.parts.size(); ++i) {
            if (p.dented.size() > i && !p.dented[i].empty()) m_models->setDeformedVertices(p.parts[i], p.dented[i], p.dentedNormals[i], true);
            else if (m_models->isDeformed(p.parts[i])) m_models->setDeformedVertices(p.parts[i], {}, {}, true);
        }
    }
    // Remote planes: the controls aren't sent, so their surfaces rest.
    const Controls c = p.remote ? Controls{} : p.controls;
    for (size_t i = 0; i < p.parts.size() && i < m_art.kinds.size(); ++i) {
        m_models->setVisible(p.parts[i], !down && present(p));
        float angle = 0.0f;
        glm::vec3 axis = m_art.axes[i];
        switch (m_art.kinds[i]) {
        case Art::Kind::Prop: angle = p.propAngle; break;
        case Art::Kind::AileronLeft: angle = c.roll * 22.0f; break;
        case Art::Kind::AileronRight: angle = -c.roll * 22.0f; break;
        case Art::Kind::Elevator: angle = -c.pitch * 22.0f; break;
        case Art::Kind::Rudder: angle = -c.yaw * 25.0f; break;
        case Art::Kind::Stick: angle = -c.pitch * 14.0f; break;
        case Art::Kind::Body: break;
        }
        glm::mat4 local(1.0f);
        if (angle != 0.0f) {
            const glm::vec3 h = m_art.hinges[i];
            local = glm::translate(glm::mat4(1.0f), h) * glm::rotate(glm::mat4(1.0f), glm::radians(angle), axis) * glm::translate(glm::mat4(1.0f), -h);
        }
        m_models->setTransform(p.parts[i], world * local);
    }
}

// Smoke: a puff dropped behind the tail every metre and a half flown
// while it's on (along the way since last frame, so a slow frame leaves
// no gaps); each ages, grows and drifts up a little.
void FlyingModule::updateTrail(Pilot& p, float dt) {
    Trail& t = p.trail;
    for (Trail::Point& pt : t.points) {
        pt.age += dt;
        pt.at.y += dt * 0.4f;
    }
    while (!t.points.empty() && t.points.front().age > kTrailLife) t.points.pop_front();
    const bool on = p.remote ? p.net.smoke && !p.net.crashed : p.smoke && !p.plane.crashed && p.respawnIn <= 0.0f;
    if (!on) {
        t.dropping = false;
        return;
    }
    const glm::vec3 position = p.remote ? p.net.position : p.plane.position;
    const glm::quat rotation = p.remote ? p.drawnRotation : p.plane.rotation;
    const glm::vec3 tail = position + rotation * glm::vec3(0.0f, 0.4f, 4.5f);
    if (!t.dropping || glm::length(tail - t.last) > 200.0f) { // just switched on, or put somewhere else
        t.dropping = true;
        t.last = tail;
        t.points.push_back({ tail, 0.0f });
        return;
    }
    const glm::vec3 way = tail - t.last;
    const float length = glm::length(way);
    const int puffs = static_cast<int>(length / kPuffSpacing);
    for (int i = 1; i <= puffs; ++i) {
        const float k = static_cast<float>(i) * kPuffSpacing / length;
        t.points.push_back({ t.last + way * k, dt * (1.0f - k) }); // the earlier ones are a little older
    }
    if (puffs > 0) t.last += way * (static_cast<float>(puffs) * kPuffSpacing / length);
}

// Every puff of every plane's smoke, for render(): rebuilt each frame
// in update() (render() runs once per split-screen view). A puff starts
// small and tinted with the plane's colour, then grows and whitens.
void FlyingModule::rebuildTrails() {
    m_puffs.clear();
    // The cameras here: a puff right in front of one would fill its view
    // (the chase camera flies down its own plane's smoke), so those go.
    std::vector<glm::vec3> eyes;
    for (const Pilot& p : m_pilots)
        if (p.seat >= 0 && !p.remote) eyes.push_back(p.camera.position);
    if (eyes.empty()) eyes.push_back(m_app->camera().position);
    for (const Pilot& p : m_pilots) {
        const glm::vec3 tint = glm::mix(p.tint, glm::vec3(1.0f), 0.75f);
        for (const Trail::Point& pt : p.trail.points) {
            const float t = std::min(pt.age / kTrailLife, 1.0f);
            const float radius = 0.35f + 2.4f * std::sqrt(t);
            bool nearEye = false;
            for (const glm::vec3& e : eyes) nearEye = nearEye || glm::length(e - pt.at) < radius + 6.0f;
            if (nearEye) continue;
            m_puffs.push_back({ pt.at, radius, glm::mix(tint, glm::vec3(0.97f), std::sqrt(t)), 0.0f, 1.0f });
        }
    }
    // Explosions (Damage.cpp): balls of fire swelling, going orange, dark and out.
    for (const Fireball& f : m_fireballs) {
        if (f.age < 0.0f) continue;
        const float t = std::clamp(f.age / 1.2f, 0.0f, 1.0f);
        const glm::vec3 colour = t < 0.5f ? glm::mix(glm::vec3(1.0f, 0.9f, 0.55f), glm::vec3(1.0f, 0.45f, 0.1f), t * 2.0f)
                                          : glm::mix(glm::vec3(1.0f, 0.45f, 0.1f), glm::vec3(0.15f, 0.12f, 0.1f), (t - 0.5f) * 2.0f);
        m_puffs.push_back({ f.position, f.size * (0.45f + 1.1f * std::sqrt(t)), colour, std::max(0.0f, 1.0f - t * 1.3f), 1.0f });
    }
    if (!m_smoke && !m_puffs.empty()) m_smoke = std::make_unique<kke::SphereImpostorRenderer>(*m_app);
}

} // namespace flying
