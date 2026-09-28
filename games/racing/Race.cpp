// The race: the tracks (tracks/*.yaml, Track.h) built into the world, the
// grid, laps and the order everyone is in, the lights and the finish.

#include "RacingModule.h"

#include "kke/Application.h"
#include "kke/DevTools.h"
#include "kke/ImpactSynth.h"
#include "kke/Log.h"
#include "kke/ParticleEffects.h"
#include "kke/SceneLoader.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>


#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace racing {

namespace {

// One box as 24 vertices (flat-shaded faces).
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

void appendMesh(const Track::Mesh& m, std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx) {
    const uint32_t base = static_cast<uint32_t>(v.size());
    for (size_t i = 0; i < m.positions.size(); ++i) v.push_back({ m.positions[i], m.colors[i], m.normals[i], glm::vec2(0.0f) });
    for (uint32_t i : m.indices) idx.push_back(base + i);
}

// The CPU drivers' names (a player's pick is never used twice).
const char* const kDrivers[] = { "Axel",  "Brooke", "Cruz",  "Dita",  "Enzo",  "Faye", "Gus",   "Hana",  "Ivo",   "Jett",  "Kira",  "Lando",
                                 "Mika",  "Nell",   "Otto",  "Pia",   "Quinn", "Rae",  "Sonny", "Tess",  "Ugo",   "Vi",    "Wes",   "Xan",
                                 "Yuki",  "Zed" };
constexpr float kDriftGap = 3.0f; // s between drift runs
constexpr float kRallyGap = 8.0f; // s between cars starting a stage
constexpr int kDriverCount = static_cast<int>(sizeof(kDrivers) / sizeof(kDrivers[0]));

uint32_t hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

glm::quat carRotation(const glm::vec3& forward, const glm::vec3& up) {
    const glm::vec3 f = glm::normalize(forward);
    const glm::vec3 l = glm::normalize(glm::cross(up, f)); // +X is the car's left
    const glm::vec3 u = glm::cross(f, l);
    return glm::quat_cast(glm::mat3(l, u, f));
}

} // namespace

std::string RacingModule::clockText(float seconds) {
    char buf[32];
    const int m = static_cast<int>(seconds) / 60;
    std::snprintf(buf, sizeof(buf), "%d:%06.3f", m, static_cast<double>(seconds - static_cast<float>(m) * 60.0f));
    return buf;
}

void RacingModule::loadTrackList() {
    const char* base = SDL_GetBasePath();
    const std::filesystem::path folder = std::filesystem::path(base ? base : "") / "tracks";
    std::vector<std::string> problems;
    m_tracks = loadTracks(folder, problems);
    // A track file with a mistake in it still loads; say what's wrong.
    for (const std::string& p : problems) kke::log::get(name())->warn("tracks: {}", p);
    if (m_tracks.empty()) {
        kke::log::get(name())->warn("no tracks in {}: the built-in speedway", folder.generic_string());
        m_tracks.push_back(defaultTrack());
    }
    kke::log::get(name())->info("{} tracks in {}", m_tracks.size(), folder.generic_string());
    // The pile-up benchmark happens in the derby pen.
    if (m_pileup)
        for (size_t i = 0; i < m_tracks.size() && m_forceTrack < 0; ++i)
            if (m_tracks[i].event == Event::Derby) m_forceTrack = static_cast<int>(i);
    if (const char* want = kke::dev::env("KKE_RACE_TRACK")) {
        for (size_t i = 0; i < m_tracks.size(); ++i)
            if (m_tracks[i].id == want || m_tracks[i].name == want) m_forceTrack = static_cast<int>(i);
        if (m_forceTrack < 0) kke::log::get(name())->warn("KKE_RACE_TRACK: no track called '{}'", want);
    }
}

int RacingModule::chosenTrack() const {
    const kke::Lobby::Option* o = m_lobby ? m_lobby->lobby().option("track") : nullptr;
    int pick = o ? o->value : m_forceTrack;
    if (m_track && netClient()) {
        // Online, the host's.
        for (size_t i = 0; i < m_tracks.size(); ++i)
            if (m_tracks[i].id == m_track->desc().id) pick = static_cast<int>(i);
    }
    return std::clamp(pick, 0, static_cast<int>(m_tracks.size()) - 1);
}

int RacingModule::lapsOf(Event e) const { return e == Event::Drag || e == Event::Derby || e == Event::Rally ? 1 : chosenLaps(); }

int RacingModule::chosenCars() const {
    const kke::Lobby::Option* o = m_lobby ? m_lobby->lobby().option("cars") : nullptr;
    int cars = o && o->value >= 0 && o->value < static_cast<int>(o->choices.size()) ? std::atoi(o->choices[static_cast<size_t>(o->value)].c_str())
                                                                                   : m_defaultCars;
    const Event ev = m_tracks[static_cast<size_t>(chosenTrack())].event;
    if (ev == Event::Drag) cars = std::min(cars, 8);   // eight lanes
    if (ev == Event::Derby) cars = std::min(cars, m_pileup ? 24 : 12); // room to get going in the pen (the pile-up: packed)
    if (ev == Event::Rally) cars = std::min(cars, 10); // the queue before the start
    return std::clamp(cars, 1, 24);
}

int RacingModule::chosenLaps() const {
    if (m_forceLaps > 0) return m_forceLaps;
    const kke::Lobby::Option* o = m_lobby ? m_lobby->lobby().option("laps") : nullptr;
    if (o && o->value >= 0 && o->value < static_cast<int>(o->choices.size())) return std::max(1, std::atoi(o->choices[static_cast<size_t>(o->value)].c_str()));
    return m_tracks[static_cast<size_t>(chosenTrack())].laps;
}

int RacingModule::chosenSkill() const {
    const kke::Lobby::Option* o = m_lobby ? m_lobby->lobby().option("skill") : nullptr;
    return o ? std::clamp(o->value, 0, 3) : 2;
}

int RacingModule::chosenDamage() const {
    if (m_forceDamage >= 0) return std::clamp(m_forceDamage, 0, 2);
    const kke::Lobby::Option* o = m_lobby ? m_lobby->lobby().option("damage") : nullptr;
    return o ? std::clamp(o->value, 0, 2) : 1;
}

int RacingModule::humans() const {
    return static_cast<int>(std::count_if(m_cars.begin(), m_cars.end(), [](const Car& c) { return c.seat >= 0 && !c.remote; }));
}

std::vector<RacingModule::Entry> RacingModule::wantedRoster() const {
    std::vector<Entry> out;
    const int types = static_cast<int>(carTypes().size()), paintCount = static_cast<int>(paints().size());
    std::vector<bool> paintUsed(static_cast<size_t>(paintCount), false), nameUsed(static_cast<size_t>(kDriverCount), false);
    if (m_lobby) {
        const kke::Lobby& l = m_lobby->lobby();
        for (int seat : l.joinedSeats()) {
            const kke::Lobby::Seat& s = l.seat(seat);
            Entry e;
            e.seat = seat;
            e.name = l.lookFields()[0].choices[static_cast<size_t>(std::clamp(s.look[0], 0, static_cast<int>(l.lookFields()[0].choices.size()) - 1))];
            e.type = std::clamp(s.look[1], 0, types - 1);
            e.kit = std::clamp(s.look[2], 0, kKits - 1);
            e.paint = std::clamp(s.look[3], 0, paintCount - 1);
            paintUsed[static_cast<size_t>(e.paint)] = true;
            for (int i = 0; i < kDriverCount; ++i)
                if (e.name == kDrivers[i]) nameUsed[static_cast<size_t>(i)] = true;
            out.push_back(std::move(e));
        }
    }
    if (out.empty()) {
        Entry e;
        e.seat = 0;
        e.name = "You";
        e.paint = 0;
        paintUsed[0] = true;
        out.push_back(std::move(e));
    }
    if (!m_netName.empty()) out[0].name = m_netName; // the name it joins with, on every screen
    if (m_autopilot) out[0].name += " (autopilot)";
    // The CPU drivers: the cars nobody picked. The oval is stock cars
    // (mostly), the drift loop the lighter rear-drive cars, the strip any.
    const Event ev = m_tracks[static_cast<size_t>(chosenTrack())].event;
    const int total = std::max(chosenCars(), static_cast<int>(out.size()));
    const int skill = chosenSkill();
    for (int i = static_cast<int>(out.size()); i < total; ++i) {
        const uint32_t h = hash(static_cast<uint32_t>(i) * 7919u + 17u);
        Entry e;
        e.cpu = true;
        e.skill = skill;
        if (ev == Event::Oval) e.type = h % 5 < 3 ? carTypeIndex("muscle") : static_cast<int>((h >> 8) % static_cast<uint32_t>(types));
        else if (ev == Event::Drift) e.type = std::array<int, 4>{ carTypeIndex("sports"), carTypeIndex("muscle"), carTypeIndex("exotic"), carTypeIndex("ute") }[(h >> 4) % 4];
        // Old sedans, utes and stock cars in the pen; hatches and small sports cars on the gravel.
        else if (ev == Event::Derby) e.type = std::array<int, 4>{ carTypeIndex("sedan"), carTypeIndex("ute"), carTypeIndex("muscle"), carTypeIndex("sedan") }[(h >> 4) % 4];
        else if (ev == Event::Rally) e.type = std::array<int, 4>{ carTypeIndex("hatch"), carTypeIndex("hatch"), carTypeIndex("sports"), carTypeIndex("sedan") }[(h >> 4) % 4];
        else e.type = static_cast<int>((h >> 8) % static_cast<uint32_t>(types));
        e.type = std::max(0, e.type);
        e.kit = static_cast<int>((h >> 12) % kKits);
        int p = static_cast<int>((h >> 16) % static_cast<uint32_t>(paintCount));
        for (int k = 0; k < paintCount && paintUsed[static_cast<size_t>(p)]; ++k) p = (p + 1) % paintCount;
        paintUsed[static_cast<size_t>(p)] = true;
        if (std::all_of(paintUsed.begin(), paintUsed.end(), [](bool b) { return b; })) std::fill(paintUsed.begin(), paintUsed.end(), false);
        e.paint = p;
        int n = static_cast<int>((h >> 20) % kDriverCount);
        for (int k = 0; k < kDriverCount && nameUsed[static_cast<size_t>(n)]; ++k) n = (n + 1) % kDriverCount;
        nameUsed[static_cast<size_t>(n)] = true;
        e.name = std::string(kDrivers[n]) + " (CPU)";
        out.push_back(std::move(e));
    }
    return out;
}

void RacingModule::clearTrack() {
    kke::RigidWorld& w = m_rigid->world();
    for (kke::RigidWorld::BodyId b : m_trackBodies) w.remove(b);
    m_trackBodies.clear();
    for (kke::ModelModule::InstanceId i : m_props) m_models->remove(i);
    m_props.clear();
    // Frames still in flight draw these meshes: they go once those are done.
    kke::Renderer& r = m_app->renderer();
    for (auto* mesh : { &m_ground, &m_road, &m_walls, &m_markings, &m_skidMesh })
        if (*mesh) r.retire(std::move(*mesh));
    clearEffects();
    m_track.reset();
    m_builtTrack.clear();
}

void RacingModule::buildTrack(const TrackDesc& desc) {
    // The cars stand on the old track: they go first (buildRace makes them again).
    for (Car& c : m_cars) removeCar(c);
    m_cars.clear();
    clearTrack();
    m_track = std::make_unique<Track>(desc);
    m_builtTrack = desc.id;
    const Track& t = *m_track;
    if (!desc.mood.empty() && desc.mood != m_builtMood) {
        m_builtMood = desc.mood;
        m_app->setMood(desc.mood);
    }
    kke::RigidWorld& w = m_rigid->world();

    // The ground: a slab under everything (grass round the speedway,
    // concrete at the docks and the strip, the arena's dirt floor), or a
    // stage's hills.
    glm::vec3 mn(1e9f), mx(-1e9f);
    for (const Track::Sample& s : t.samples()) {
        mn = glm::min(mn, s.p);
        mx = glm::max(mx, s.p);
    }
    const glm::vec3 center((mn.x + mx.x) * 0.5f, -0.5f, (mn.z + mx.z) * 0.5f);
    const glm::vec3 half((mx.x - mn.x) * 0.5f + 220.0f, 0.5f, (mx.z - mn.z) * 0.5f + 220.0f);
    // What the tyres find under them (docs/VEHICLES.md "Tyres"): the road's
    // ground, and off it the verge's.
    setGround(kke::AudioMaterialTable::Stone, groundNamed(desc.ground));
    setGround(kke::AudioMaterialTable::Dirt, groundNamed(desc.verge));
    {
        std::vector<kke::Vertex> v;
        std::vector<uint32_t> idx;
        kke::RigidWorld::BodyDesc d;
        d.motion = kke::RigidWorld::Motion::Static;
        d.friction = 0.7f;
        d.material = kke::AudioMaterialTable::Dirt;
        if (t.stage()) {
            appendMesh(t.terrain(), v, idx);
            d.shape = kke::RigidWorld::Shape::Mesh;
            d.points = t.terrain().positions;
            d.indices = t.terrain().indices;
        } else {
            appendBox(center, half, groundColor(desc.verge), v, idx);
            d.halfExtents = half;
            d.position = center;
        }
        m_ground = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
        m_ground->upload(v, idx);
        m_trackBodies.push_back(w.add(d));
    }
    // The road (and the apron) as one static mesh; the walls as boxes.
    if (!t.surface().indices.empty()) { // an arena's floor is the ground
        std::vector<kke::Vertex> v;
        std::vector<uint32_t> idx;
        appendMesh(t.surface(), v, idx);
        m_road = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
        m_road->upload(v, idx);
        kke::RigidWorld::BodyDesc d;
        d.shape = kke::RigidWorld::Shape::Mesh;
        d.motion = kke::RigidWorld::Motion::Static;
        d.points = t.surface().positions;
        d.indices = t.surface().indices;
        d.friction = 1.0f;
        d.material = kke::AudioMaterialTable::Stone;
        m_trackBodies.push_back(w.add(d));
    }
    {
        std::vector<kke::Vertex> v;
        std::vector<uint32_t> idx;
        appendMesh(t.walls(), v, idx);
        m_walls = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
        m_walls->upload(v, idx);
        for (const Track::WallBox& b : t.wallBoxes()) {
            kke::RigidWorld::BodyDesc d;
            d.motion = kke::RigidWorld::Motion::Static;
            d.halfExtents = b.half;
            d.position = b.center;
            d.rotation = glm::angleAxis(std::atan2(b.forward.x, b.forward.z), glm::vec3(0.0f, 1.0f, 0.0f));
            d.friction = 0.25f; // cars slide along it rather than stopping dead
            d.restitution = 0.2f;
            d.material = kke::AudioMaterialTable::Stone;
            m_trackBodies.push_back(w.add(d));
        }
    }
    {
        std::vector<kke::Vertex> v;
        std::vector<uint32_t> idx;
        appendMesh(t.markings(), v, idx);
        m_markings = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
        m_markings->upload(v, idx);
    }
    m_skidMesh = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    buildScenery();
    kke::log::get(name())->info("track {} ({}): {:.0f} m, {} m wide, {} wall boxes, {} props", desc.name, eventName(desc.event), t.length(),
                                desc.width, t.wallBoxes().size(), m_props.size());
}

kke::ModelModule::InstanceId RacingModule::placeProp(const std::string& asset, const glm::vec3& pos, const glm::vec3& forward, float scale, const char* pack) {
    const kke::CatalogAsset* a = m_catalog.find(asset, { pack });
    if (!a) return 0;
    kke::ModelLoadOptions opts = kke::packLoadOptions(m_catalog, *a);
    opts.loadAnimations = false;
    const kke::ModelModule::ModelId id = m_models->load(a->path, opts);
    if (!id) return 0;
    const glm::mat4 xf = glm::scale(glm::translate(glm::mat4(1.0f), pos) *
                                        glm::mat4_cast(glm::angleAxis(std::atan2(forward.x, forward.z), glm::vec3(0.0f, 1.0f, 0.0f))),
                                    glm::vec3(scale));
    const kke::ModelModule::InstanceId inst = m_models->spawn(id, xf);
    m_models->setOverlayEnabled(inst, false);
    m_props.push_back(inst);
    if (std::find(m_propsUsed.begin(), m_propsUsed.end(), asset) == m_propsUsed.end()) m_propsUsed.push_back(asset);
    return inst;
}

// Dressing from the Street Racer pack, all of it well outside the walls
// (nothing a car can reach): light towers, the start and finish gantries,
// the garage behind the pit lane, containers and cranes at the docks.
void RacingModule::buildScenery() {
    if (!m_garage->hasPack()) return;
    const Track& t = *m_track;
    const float hw = t.halfWidth();
    // A spot `out` m beyond the right-hand wall at s, facing the track;
    // only if it's that far from every part of the track (a loop's other
    // side can be close).
    auto spot = [&](float s, float side, float out, glm::vec3& pos, glm::vec3& face) {
        const Track::Sample f = t.at(s);
        const glm::vec3 leftFlat(f.forward.z, 0.0f, -f.forward.x);
        const float edge = side > 0.0f ? t.maxU() + 0.4f : hw;
        pos = glm::vec3(f.p.x, 0.0f, f.p.z) + leftFlat * (side * (edge + out));
        face = -leftFlat * side;
        const Track::Where w = t.locate(pos);
        return w.u > t.maxU() + out * 0.6f || w.u < -hw - out * 0.6f;
    };
    glm::vec3 pos, face;
    const TrackDesc& d = t.desc();
    if (t.arena()) {
        buildArenaScenery();
        return;
    }
    if (t.stage()) {
        buildStageScenery();
        return;
    }
    if (d.event == Event::Drag) {
        for (float s = 0.0f; s < t.finishS() + 200.0f; s += 60.0f)
            for (float side : { -1.0f, 1.0f })
                if (spot(s, side, 6.0f, pos, face)) placeProp("SM_Prop_Pole_Large_Lights_01", pos, face);
        if (spot(t.startS(), -1.0f, 3.0f, pos, face)) placeProp("SM_Prop_Sign_Start_01", pos, face);
        if (spot(t.finishS(), -1.0f, 3.0f, pos, face)) placeProp("SM_Prop_Sign_Finish_01", pos, face);
        for (float s = t.finishS() + 140.0f; s < t.length(); s += 12.0f)
            for (float side : { -1.0f, 1.0f })
                if (spot(s, side, 1.5f, pos, face)) placeProp("SM_Prop_Barrier_Tyre_StackA_01", pos, face);
        return;
    }
    if (d.event == Event::Oval) {
        for (float s = 0.0f; s < t.length(); s += 70.0f)
            if (spot(s, -1.0f, 7.0f, pos, face)) placeProp("SM_Prop_Pole_Large_Lights_01", pos, face);
        if (spot(6.0f, -1.0f, 4.0f, pos, face)) placeProp("SM_Prop_Sign_Finish_01", pos, face);
        if (t.hasPits()) {
            // The garage behind the pit lane, tyres and tools by the boxes.
            const float mid = (t.pitStart() + t.pitEnd()) * 0.5f;
            if (spot(mid, 1.0f, 16.0f, pos, face)) placeProp("SM_Bld_RepairShop_Large_01", pos, face);
            for (float s = t.pitStart() + 16.0f; s < t.pitEnd() - 8.0f; s += 24.0f) {
                if (spot(s, 1.0f, 3.0f, pos, face)) placeProp("SM_Prop_TyreStack_01", pos, face);
                if (spot(s + 6.0f, 1.0f, 3.0f, pos, face)) placeProp("SM_Prop_ToolCabinet_01_Preset", pos, face);
            }
        }
        for (float s = 40.0f; s < t.length(); s += 150.0f)
            if (spot(s, 1.0f, 2.5f, pos, face)) placeProp("SM_Prop_Barrier_Tyre_StackB_01", pos, face);
        return;
    }
    // The docks: stacks of containers along the loop, cranes above them,
    // warehouses and light towers.
    const char* stacks[] = { "SM_Prop_Container_Stack_01", "SM_Prop_Container_Stack_04", "SM_Prop_Container_Large_Stack_02", "SM_Prop_Container_Stack_07" };
    int k = 0;
    for (float s = 0.0f; s < t.length(); s += 38.0f, ++k) {
        const float side = k % 2 ? 1.0f : -1.0f;
        if (spot(s, side, 9.0f, pos, face)) placeProp(stacks[k % 4], pos, face);
    }
    for (float s = 20.0f; s < t.length(); s += 90.0f)
        if (spot(s, -1.0f, 5.0f, pos, face)) placeProp("SM_Prop_Pole_Large_Lights_02", pos, face);
    for (float s = 60.0f; s < t.length(); s += 260.0f)
        if (spot(s, 1.0f, 30.0f, pos, face)) placeProp("SM_Bld_Crane_01", pos, face);
    if (spot(t.length() * 0.5f, -1.0f, 34.0f, pos, face)) placeProp("SM_Bld_Warehouse_01", pos, face);
    for (float s = 10.0f; s < t.length(); s += 55.0f)
        if (spot(s, 1.0f, 1.2f, pos, face)) placeProp("SM_Prop_Barrier_Tyre_StackA_02", pos, face);
}

void RacingModule::addSolid(const glm::vec3& center, const glm::vec3& half, float yaw, uint32_t material) {
    kke::RigidWorld::BodyDesc d;
    d.motion = kke::RigidWorld::Motion::Static;
    d.halfExtents = half;
    d.position = center;
    d.rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    d.friction = 0.5f;
    d.restitution = 0.15f;
    d.material = material;
    m_trackBodies.push_back(m_rigid->world().add(d));
}

// The derby pen: a ring of tyre stacks round the outside of the wall,
// floodlights, and the scrapyard it's in (dirt heaps, containers, a crane,
// a shelter for the crowd).
void RacingModule::buildArenaScenery() {
    const Track& t = *m_track;
    const glm::vec2 half = t.desc().size * 0.5f;
    auto rim = [&](float th, float out) { return glm::vec3(std::cos(th) * (half.x + out), 0.0f, std::sin(th) * (half.y + out)); };
    const float around = glm::pi<float>() * (half.x + half.y); // about the perimeter
    const int stacks = static_cast<int>(around / 5.0f);
    for (int i = 0; i < stacks; ++i) {
        const float th = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(stacks);
        const glm::vec3 p = rim(th, 0.9f);
        placeProp(i % 3 ? "SM_Prop_Barrier_Tyre_StackA_01" : "SM_Prop_Barrier_Tyre_StackB_01", p, -p);
    }
    for (int i = 0; i < 6; ++i) {
        const float th = glm::two_pi<float>() * (static_cast<float>(i) + 0.5f) / 6.0f;
        const glm::vec3 p = rim(th, 5.0f);
        placeProp("SM_Prop_Pole_Large_Lights_01", p, -p);
    }
    const char* heaps[] = { "SM_Env_Dirt_Pile_01", "SM_Env_Dirt_Pile_02", "SM_Env_Dirt_Pile_03" };
    for (int i = 0; i < 9; ++i) {
        const float th = glm::two_pi<float>() * (static_cast<float>(i) + 0.25f) / 9.0f;
        placeProp(heaps[i % 3], rim(th, 13.0f + static_cast<float>(i % 3) * 4.0f), rim(th + 1.0f, 0.0f));
    }
    const char* boxes[] = { "SM_Prop_Container_Stack_01", "SM_Prop_Container_Stack_04", "SM_Prop_Container_Large_Stack_02" };
    for (int i = 0; i < 7; ++i) {
        const float th = glm::pi<float>() * (0.15f + 0.1f * static_cast<float>(i));
        const glm::vec3 p = rim(th, 28.0f);
        placeProp(boxes[i % 3], p, -p);
    }
    const glm::vec3 stand = rim(-glm::half_pi<float>(), 11.0f);
    placeProp("SM_Bld_Shelter_01", stand, -stand);
    placeProp("SM_Bld_Crane_01", rim(1.9f, 45.0f), -rim(1.9f, 0.0f));
}

// A rally stage: forest either side (as thick as `trees`), rocks close to
// the road, the start and finish signs. The trees and rocks near the road
// are solid: leave it and you can hit them.
void RacingModule::buildStageScenery() {
    const Track& t = *m_track;
    const TrackDesc& d = t.desc();
    const float hw = t.halfWidth();
    uint32_t seed = 0x9e3779b9u;
    auto rnd = [&seed]() {
        seed = hash(seed + 0x6d2b79f5u);
        return static_cast<float>(seed & 0xffffffu) / static_cast<float>(0x1000000);
    };
    auto groundAt = [&](const glm::vec3& p) { return glm::vec3(p.x, t.groundHeight(p.x, p.z), p.z); };
    // Clear of the road everywhere (a bend's other side can be close)?
    auto clear = [&](const glm::vec3& p, float room) {
        const Track::Where w = t.locate(p);
        return std::fabs(w.u) > hw + room || w.s < 1.0f || w.s > t.length() - 1.0f;
    };
    const char* trees[] = { "SM_Tree_Pine_01", "SM_Tree_01", "SM_Tree_Pine_01", "SM_Tree_02", "SM_Tree_Birch_01", "SM_Tree_03" };
    const char* rocks[] = { "SM_Rock_01", "SM_Rock_02", "SM_Rock_03", "SM_Rock_Small_01" };
    const float every = 26.0f - 20.0f * d.trees; // m along the road between trees, each side
    int placed = 0;
    for (float s = 0.0f; s < t.length() && d.trees > 0.0f; s += every) {
        const Track::Sample f = t.at(s);
        const glm::vec3 leftFlat(f.forward.z, 0.0f, -f.forward.x);
        for (float side : { -1.0f, 1.0f }) {
            // Near the road (solid), and a second row further back (just to look at).
            for (int row = 0; row < 2; ++row) {
                const float out = row == 0 ? hw + 4.0f + rnd() * 18.0f : hw + 26.0f + rnd() * 60.0f;
                const glm::vec3 p = groundAt(f.p + leftFlat * (side * out) + f.forward * ((rnd() - 0.5f) * every));
                if (!clear(p, 3.5f)) continue;
                const float scale = 0.8f + rnd() * 0.5f;
                const float yaw = rnd() * glm::two_pi<float>();
                if (!placeProp(trees[static_cast<size_t>(rnd() * 6.0f) % 6], p - glm::vec3(0.0f, 0.2f, 0.0f), glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw)), scale, "POLYGON_Nature"))
                    continue;
                ++placed;
                if (row == 0) addSolid(p + glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.3f * scale, 2.0f, 0.3f * scale), yaw, kke::AudioMaterialTable::Wood);
            }
        }
        // Now and then a rock on the verge: cut the corner, pay for it.
        if (rnd() < 0.25f) {
            const float side = rnd() < 0.5f ? -1.0f : 1.0f;
            const glm::vec3 p = groundAt(f.p + leftFlat * (side * (hw + 1.8f + rnd() * 4.0f)));
            const float scale = 0.6f + rnd() * 0.6f;
            if (clear(p, 1.2f) && placeProp(rocks[static_cast<size_t>(rnd() * 4.0f) % 4], p, f.forward, scale, "POLYGON_Nature"))
                addSolid(p + glm::vec3(0.0f, 0.3f * scale, 0.0f), glm::vec3(0.6f * scale, 0.35f * scale, 0.6f * scale), rnd() * 3.0f, kke::AudioMaterialTable::Stone);
        }
    }
    // The start and the finish.
    for (float at : { t.startS(), t.finishS() }) {
        const Track::Sample f = t.at(at);
        const glm::vec3 leftFlat(f.forward.z, 0.0f, -f.forward.x);
        placeProp(at == t.startS() ? "SM_Prop_Sign_Start_01" : "SM_Prop_Sign_Finish_01", groundAt(f.p + leftFlat * (hw + 2.5f)), -leftFlat);
        for (float side : { -1.0f, 1.0f })
            placeProp("SM_Prop_Barrier_Tyre_StackA_01", groundAt(f.p + leftFlat * (side * (hw + 1.2f)) + f.forward * 3.0f), f.forward);
    }
    kke::log::get(name())->info("stage: {} trees and rocks", placed);
}

void RacingModule::buildRace(const std::vector<Entry>& roster) {
    for (Car& c : m_cars) removeCar(c);
    m_cars.clear();
    clearEffects();
    m_cars.resize(roster.size());
    m_trails.assign(roster.size(), {});
    for (size_t i = 0; i < roster.size(); ++i) {
        const Entry& e = roster[i];
        Car& c = m_cars[i];
        c.seat = e.remote ? -1 : e.seat;
        c.remote = e.remote;
        c.cpu = !e.remote && (e.cpu || m_autopilot);
        c.netId = e.netId;
        c.skill = e.skill;
        c.name = e.name;
        c.type = e.type;
        c.kit = e.kit;
        c.paint = e.paint;
        c.color = paints()[static_cast<size_t>(std::clamp(e.paint, 0, static_cast<int>(paints().size()) - 1))].color;
        c.rng = hash(static_cast<uint32_t>(i) * 2654435761u + 99u) | 1u;
        buildCar(c, static_cast<int>(i));
    }
    if (m_lobby) {
        for (Car& c : m_cars)
            if (c.seat >= 0) c.player = std::max(0, m_lobby->playerOf(c.seat));
    }
    m_rosterChanged = false;
}

void RacingModule::resetRace() {
    ++m_round;
    m_laps = lapsOf(event());
    m_damage = event() == Event::Derby ? std::max(1, chosenDamage()) : chosenDamage(); // a derby without damage never ends
    m_phase = Phase::Countdown;
    m_countdown = event() == Event::Drag ? 4.0f : m_pileup ? 1.0f : 3.0f;
    m_pileupTimer = 0.0f;
    m_pileups = 0;
    m_raceClock = 0.0f;
    m_leaderDone = -1.0f;
    m_finishedFor = 0.0f;
    m_winner.clear();
    clearEffects();
    const Track& t = *m_track;
    const int cars = static_cast<int>(m_cars.size());
    for (int i = 0; i < cars; ++i) {
        Car& c = m_cars[static_cast<size_t>(i)];
        const glm::vec3 p = t.gridPosition(i, cars);
        const Track::Where w = t.locate(p);
        if (t.arena()) placeCar(c, p + glm::vec3(0.0f, 0.12f, 0.0f), t.gridForward(i, cars), glm::vec3(0.0f, 1.0f, 0.0f)); // round the pen, facing in
        else resetCarOnTrack(c, w.s, w.u);
        c.where = t.locate(carPosition(c));
        c.lap = t.closed() && c.where.s > t.length() * 0.5f ? -1 : 0; // behind the line: the first crossing starts lap 1
        c.progress = 0.0f;
        c.lapStart = c.bestLap = c.lastLap = 0.0f;
        c.finished = false;
        c.finishTime = 0.0f;
        c.place = i + 1;
        c.wrongWay = c.upsideDown = c.offTrack = 0.0f;
        repairCar(c, 1000.0f);
        c.totalled = false;
        c.hits = 0;
        c.derbyPoints = 0.0f;
        c.wrecked = 0;
        c.lastAttack = c.lastHitAt = c.outAt = 0.0f;
        c.lastHitBy = c.aiTarget = -1;
        c.aiUnstick = 0.0f;
        c.wantsPit = false;
        c.pitTime = 0.0f;
        c.pitStops = 0;
        c.driftScore = c.driftChain = c.driftHold = c.driftGrace = c.driftBest = 0.0f;
        c.driftCombo = 1.0f;
        c.drifting = false;
        c.gear = 1;
        c.falseStart = false;
        c.reaction = -1.0f;
        c.trapSpeed = 0.0f;
        c.startS = c.where.s;
        c.note.clear();
        c.noteTime = 0.0f;
        c.camInit = false;
        c.aiLane = c.where.u;
        c.aiLaneTimer = 2.0f + random01(c) * 4.0f;
        c.aiStuck = c.aiReverse = c.aiSlide = c.aiWrongWay = 0.0f;
        // Drift runs leave the line one after another, room to slide; rally
        // cars start the stage one at a time.
        c.releaseAt = event() == Event::Drift ? static_cast<float>(i) * kDriftGap : event() == Event::Rally ? static_cast<float>(i) * kRallyGap : 0.0f;
        // Every CPU driver a little different: how hard it corners, when
        // it shifts and reacts on the strip.
        const float skill = static_cast<float>(c.skill);
        c.aiPace = 0.8f + 0.055f * skill + 0.05f * random01(c);
        c.aiShiftAt = 0.8f + 0.05f * skill + 0.04f * random01(c);
        c.aiReact = 0.5f - 0.1f * skill + 0.15f * random01(c);
    }
    m_tvCar = -1;
    m_tvSpot = glm::vec3(0.0f);
    m_netHold = false;
    kke::log::get(name())->info("race {}: {} on {}, {} cars ({} here, {} CPU, {} online), {} laps, damage {}", m_round, eventName(event()),
                                t.desc().name, cars, humans(), std::count_if(m_cars.begin(), m_cars.end(), [](const Car& c) { return c.cpu && c.seat < 0; }),
                                std::count_if(m_cars.begin(), m_cars.end(), [](const Car& c) { return c.remote; }), m_laps,
                                m_damage == 0 ? "off" : m_damage == 1 ? "normal" : "brutal");
}

void RacingModule::updateRace(float dt) {
    const bool stopped = m_howto && !netConnected();
    if (m_phase == Phase::Countdown && !stopped) {
        const int before = static_cast<int>(std::ceil(m_countdown));
        if (!m_netHold) m_countdown -= dt; // online: until every machine is on the grid
        if (static_cast<int>(std::ceil(m_countdown)) != before && m_countdown > 0.0f && m_countdown < 3.0f) tone(static_cast<int>(kke::Earcon::Tick));
        // The strip: moving off before green is a false start (red light).
        if (event() == Event::Drag && m_countdown < 1.5f)
            for (Car& c : m_cars)
                if (!c.remote && !c.falseStart && m_track->delta(c.startS, c.where.s) > 0.4f) {
                    c.falseStart = true;
                    c.note = "False start!";
                    c.noteTime = 3.0f;
                    if (c.seat >= 0) tone(static_cast<int>(kke::Earcon::Error), 0.7f);
                }
        if (m_countdown <= 0.0f) {
            m_phase = Phase::Racing;
            m_raceClock = 0.0f;
            for (Car& c : m_cars) c.lapStart = c.releaseAt;
            tone(static_cast<int>(kke::Earcon::Activate), 0.8f);
        }
    }
    if (m_phase == Phase::Racing && !stopped) {
        m_raceClock += dt;
        if (event() == Event::Drag)
            for (Car& c : m_cars)
                if (c.reaction < 0.0f && !c.remote && m_track->delta(c.startS, c.where.s) > 0.3f) c.reaction = m_raceClock;
        if (m_leaderDone >= 0.0f) m_leaderDone += dt;
        if (m_pileup && m_track->arena()) updatePileup(dt);
        else if (event() == Event::Derby) updateDerby();
        // Over when every car is home (or out), when every player here is
        // and the rest had a while, or a minute after the winner.
        bool allDone = true, playersDone = true, anyPlayer = false;
        for (const Car& c : m_cars) {
            allDone = allDone && done(c);
            if (c.seat >= 0 && !c.remote) {
                anyPlayer = true;
                playersDone = playersDone && done(c);
            }
        }
        // Drift runs left one after another: the last one gets its lap too.
        const float grace = event() == Event::Drift ? kDriftGap * static_cast<float>(m_cars.size())
                          : event() == Event::Rally ? kRallyGap * static_cast<float>(m_cars.size()) + 60.0f // the stage's longer than a lap
                                                    : 0.0f;
        if (allDone || (anyPlayer && playersDone && m_leaderDone > 15.0f + grace) || m_leaderDone > 60.0f + grace) {
            m_phase = Phase::Finished;
            m_finishedFor = 0.0f;
            updateStandings(); // a drift winner is only known now
            kke::log::get(name())->info("race {} over after {}: {} wins", m_round, clockText(m_raceClock), m_winner.empty() ? "nobody" : m_winner);
        }
    } else if (m_phase == Phase::Finished) {
        m_finishedFor += dt;
        // Autopilot is a show: round again.
        if (m_autopilot && m_finishedFor > 10.0f && !netClient()) resetRace();
    }
    updateStandings();
}

void RacingModule::crossLine(Car& c, int direction) {
    if (direction < 0) {
        --c.lap; // backwards over the line: that lap doesn't count
        return;
    }
    ++c.lap;
    if (m_phase != Phase::Racing || c.finished) return;
    if (c.lap >= 1) {
        c.lastLap = m_raceClock - c.lapStart;
        if (c.bestLap <= 0.0f || c.lastLap < c.bestLap) c.bestLap = c.lastLap;
    }
    c.lapStart = m_raceClock;
    // The leader's done: everyone else finishes as they next cross the line.
    if (c.lap >= m_laps || (m_leaderDone >= 0.0f && c.lap >= 1)) {
        finishCar(c);
        return;
    }
    // CPU drivers with a bad car head for the pits.
    if (c.cpu && m_track->hasPits() && (c.health < 55.0f || tyresHurt(c)) && m_laps - c.lap >= 1 && m_damage > 0) c.wantsPit = true;
    if (c.seat >= 0 && c.lap == m_laps - 1) {
        c.note = "Last lap!";
        c.noteTime = 2.5f;
    }
}

void RacingModule::finishCar(Car& c) {
    if (c.finished || c.remote) return;
    c.finished = true;
    c.finishTime = event() == Event::Rally ? m_raceClock - c.lapStart : m_raceClock; // a stage: from its own start
    if (event() == Event::Drag) c.trapSpeed = carSpeed(c);
    if (m_leaderDone < 0.0f) m_leaderDone = 0.0f;
    // Drift, derby and rally: the winner's only known at the end (updateStandings).
    if (m_winner.empty() && !scoredAtEnd(event()) && !(event() == Event::Drag && c.falseStart)) m_winner = c.name;
    kke::log::get(name())->info("{} finished in {}{}", c.name, clockText(c.finishTime),
                                event() == Event::Drag ? fmt::format(" (reaction {:.3f} s, {:.0f} km/h at the line{})", c.reaction, c.trapSpeed * 3.6f,
                                                                     c.falseStart ? ", false start" : "")
                                                       : std::string());
    if (c.seat >= 0) tone(static_cast<int>(kke::Earcon::ToggleOn), 0.8f);
    netFinished(c);
}

void RacingModule::updateStandings() {
    if (m_cars.empty()) return;
    std::vector<int> order(m_cars.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    const Event ev = event();
    std::stable_sort(order.begin(), order.end(), [&](int ia, int ib) {
        const Car& a = m_cars[static_cast<size_t>(ia)];
        const Car& b = m_cars[static_cast<size_t>(ib)];
        if (ev == Event::Drift) return a.driftScore + a.driftChain > b.driftScore + b.driftChain;
        // The derby: still running first, the most damage dealt; then who lasted longest.
        if (ev == Event::Derby) {
            if (a.totalled != b.totalled) return !a.totalled;
            if (!a.totalled) return a.derbyPoints > b.derbyPoints;
            return a.outAt != b.outAt ? a.outAt > b.outAt : a.derbyPoints > b.derbyPoints;
        }
        if (ev == Event::Drag && a.falseStart != b.falseStart) return !a.falseStart;
        if (a.finished != b.finished) return a.finished;
        if (a.finished) return a.finishTime < b.finishTime;
        if (a.totalled != b.totalled) return !a.totalled;
        return a.progress > b.progress;
    });
    for (size_t i = 0; i < order.size(); ++i) m_cars[static_cast<size_t>(order[i])].place = static_cast<int>(i) + 1;
    // Drift, derby, rally: the top of the table wins once everyone's done
    // (a stage only if they got to the end).
    const Car& top = m_cars[static_cast<size_t>(order[0])];
    if (scoredAtEnd(ev) && m_phase == Phase::Finished && m_winner.empty() && (ev != Event::Rally || top.finished)) m_winner = top.name;
}

void RacingModule::resetCarOnTrack(Car& c, float s, float u) {
    const Track& t = *m_track;
    const Track::Sample f = t.at(s);
    const bool apron = u > t.halfWidth();
    const glm::vec3 up = apron ? glm::vec3(0.0f, 1.0f, 0.0f) : f.up;
    // Up a stage's hills: along the slope, not into it.
    const glm::vec3 forward = t.stage() ? glm::normalize(glm::cross(f.left, up)) : f.forward;
    placeCar(c, t.point(s, u) + up * 0.12f, forward, up);
}

void RacingModule::placeCar(Car& c, const glm::vec3& pos, const glm::vec3& forward, const glm::vec3& up) {
    const glm::quat rot = carRotation(forward, up);
    kke::RigidWorld& w = m_rigid->world();
    w.setTransform(c.body, pos, rot);
    w.setVelocity(c.body, glm::vec3(0.0f));
    w.setAngularVelocity(c.body, glm::vec3(0.0f));
    if (c.vehicle) w.resetVehicle(c.vehicle);
    c.xf = c.prevXf = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot);
    c.velocity = glm::vec3(0.0f);
    c.input = kke::VehicleInput{};
    c.upsideDown = c.offTrack = 0.0f;
    c.aiStuck = c.aiReverse = c.aiWrongWay = c.aiSlide = 0.0f;
    c.camInit = false;
    const size_t index = static_cast<size_t>(&c - m_cars.data());
    if (index < m_trails.size())
        for (SkidTrail& tr : m_trails[index]) tr.on = false;
}

} // namespace racing
