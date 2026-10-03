#include "kke/Breakables.h"

#include "kke/Application.h"
#include "kke/InteriorColor.h"
#include "kke/Log.h"
#include "kke/ModelAsset.h"
#include "kke/VoronoiFracture.h"
#include "kke/VoxelTets.h"
#include "kke/modules/PhysicsModule.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace kke {

namespace {

Material makeMaterial(float density, float stiffness, float poisson, float fracture, float yield, float creep, float metallic, float roughness, int texture) {
    Material m;
    m.density = density;
    m.stiffness = stiffness;
    m.poissonsRatio = poisson;
    m.fractureStressThreshold = fracture;
    m.plasticYieldThreshold = yield;
    m.plasticCreep = creep;
    m.metallic = metallic;
    m.roughness = roughness;
    m.textureId = texture;
    return m;
}

// Tuned in games/sandbox (shooting balls at Synty props): pieces that
// read as the material, props that hold their own weight.
const BreakPreset kPresets[kBreakKindCount] = {
    { "wood", "Wood (splinters)", makeMaterial(600.0f, 1.0e7f, 0.30f, 1.2e5f, 6000.0f, 0.3f, 0.0f, 0.75f, 0), FracturePattern::Splinters, 0.35f, 0, false },
    { "stone", "Stone (chunks)", makeMaterial(2500.0f, 3.0e7f, 0.25f, 8.0e4f, 3000.0f, 0.1f, 0.0f, 0.9f, 1), FracturePattern::Voronoi, 0.3f, 3, false },
    { "glass", "Glass (shatters)", makeMaterial(2500.0f, 7.0e7f, 0.22f, 8.0e4f, 1800.0f, 0.02f, 0.0f, 0.05f, 4), FracturePattern::Radial, 0.45f, 0, false },
    { "ceramic", "Ceramic (shards)", makeMaterial(2300.0f, 5.0e7f, 0.22f, 8.0e4f, 2400.0f, 0.02f, 0.0f, 0.3f, 1), FracturePattern::Shards, 0.3f, 0, false },
    // Metal dents instead of breaking: FEMFX plasticity, no fracture.
    { "metal", "Metal (dents)", makeMaterial(7870.0f, 2.0e7f, 0.30f, 1.0e9f, 2000.0f, 0.5f, 0.9f, 0.35f, 2), FracturePattern::Solid, 1.0f, 0, true },
};

// The words of a name, lower case: "SM_Prop_WoodenCrate_01" -> sm, prop,
// wooden, crate (underscores, spaces, dashes, dots, digits and camel case
// split it).
std::vector<std::string> words(const std::string& name) {
    // Only the file name: "Packs/POLYGON_Town/.../SM_Prop_Crate_01.fbx".
    const size_t slash = name.find_last_of("/\\");
    std::string base = slash == std::string::npos ? name : name.substr(slash + 1);
    const size_t dot = base.rfind('.');
    if (dot != std::string::npos && dot > 0) base.resize(dot);
    std::vector<std::string> out;
    std::string w;
    auto flush = [&] {
        if (!w.empty()) out.push_back(w);
        w.clear();
    };
    for (size_t i = 0; i < base.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(base[i]);
        if (!std::isalpha(c)) {
            flush();
            continue;
        }
        // camelCase: a capital after a lower-case letter starts a word.
        if (std::isupper(c) && i > 0 && std::islower(static_cast<unsigned char>(base[i - 1]))) flush();
        w.push_back(static_cast<char>(std::tolower(c)));
    }
    flush();
    return out;
}

// A word "is" a keyword: the same, its plural, or (long keywords) starting with it.
bool matches(const std::string& word, const char* keyword) {
    const std::string k(keyword);
    if (word == k || word == k + "s" || word == k + "es") return true;
    return k.size() >= 5 && word.rfind(k, 0) == 0;
}

bool any(const std::vector<std::string>& ws, std::initializer_list<const char*> keywords) {
    for (const std::string& w : ws)
        for (const char* k : keywords)
            if (matches(w, k)) return true;
    return false;
}

} // namespace

const BreakPreset& breakPreset(BreakKind kind) { return kPresets[std::clamp(static_cast<int>(kind), 0, kBreakKindCount - 1)]; }

bool breakKindFromId(const std::string& id, BreakKind& out) {
    for (int i = 0; i < kBreakKindCount; ++i)
        if (id == kPresets[i].id || id == kPresets[i].label) {
            out = static_cast<BreakKind>(i);
            return true;
        }
    return false;
}

bool guessBreakKind(const std::string& name, BreakKind& out) {
    const std::vector<std::string> ws = words(name);
    // What shouldn't break: the level itself, buildings' shells, soft or
    // living things (a bush in chunks looks wrong), characters, effects.
    if (any(ws, { "ground", "floor", "road", "terrain", "sky", "skybox", "water", "ocean", "sea", "river", "path", "driveway", "pavement",
                  "grass", "bush", "plant", "flower", "leaf", "leaves", "hedge", "ivy", "vine", "moss", "bld", "building", "roof", "interior",
                  "cliff", "mountain", "hill", "chr", "character", "fx", "particle", "decal", "hay", "cloth", "curtain", "rug", "carpet",
                  "pillow", "cushion", "food", "fruit", "tyre", "tire" }))
        return false;
    // Named materials first: "Fence_Wood" is wood, "Barrier_Concrete" stone.
    if (any(ws, { "glass", "window", "windscreen", "windshield", "bottle", "jar", "mirror", "lightbulb", "bulb" })) out = BreakKind::Glass;
    else if (any(ws, { "wood", "wooden", "timber", "plank", "log", "logpile" })) out = BreakKind::Wood;
    else if (any(ws, { "metal", "steel", "iron", "chrome", "aluminium", "aluminum", "tin", "copper", "brass", "bronze" })) out = BreakKind::Metal;
    else if (any(ws, { "concrete", "stone", "rock", "boulder", "brick", "marble", "granite", "cobble" })) out = BreakKind::Stone;
    else if (any(ws, { "ceramic", "porcelain", "clay", "terracotta", "pottery" })) out = BreakKind::Ceramic;
    // Then what things usually are.
    else if (any(ws, { "veh", "vehicle", "car", "sedan", "truck", "van", "bus", "ute", "hatch", "muscle", "sports", "exotic", "bike",
                       "motorbike", "tractor", "plane", "boat", "bin", "dumpster", "can", "pipe", "sign", "lamp", "streetlight", "pole",
                       "hydrant", "mailbox", "rail", "railing", "container", "toolcabinet", "locker", "fan", "anvil", "sword", "axe",
                       "shield", "weapon", "gun", "rifle", "pistol", "drum", "girder", "cage", "grate", "trolley", "cart", "machine",
                       "generator", "engine", "tank", "car", "barrier" }))
        out = BreakKind::Metal;
    else if (any(ws, { "pillar", "column", "statue", "curb", "kerb", "gravestone", "tombstone", "headstone", "block", "slab", "wall",
                       "fountain", "well", "arch", "monument", "ruin" }))
        out = BreakKind::Stone;
    else if (any(ws, { "vase", "pot", "plate", "mug", "cup", "bowl", "urn", "toilet", "sink", "teapot", "jug" })) out = BreakKind::Ceramic;
    else if (any(ws, { "crate", "barrel", "box", "fence", "chair", "table", "bench", "door", "shelf", "shelves", "bookcase", "stool",
                       "bed", "desk", "cabinet", "workbench", "pallet", "tree", "trunk", "stump", "branch", "post", "ladder", "cupboard",
                       "dresser", "wardrobe", "beam", "board", "basket", "chest", "coffin", "wheelbarrow", "stall", "gate" }))
        out = BreakKind::Wood;
    else return false;
    return true;
}

#if KKE_ENABLE_FEMFX

// How far (in barycentric weight) a drawn corner may sit outside the tet
// it's glued to: 0 = inside, 1 = a whole tet away. Whole triangles glued
// to the tet under their middle (embedTriangles) reach 2-3 at their
// corners, which follows fine; a pebble the voxels missed reaches far more.
constexpr float kMaxOutside = 6.0f;

struct Breakables::Entry {
    ModelModule::InstanceId instance = 0;
    PhysicsModule::ObjectHandle handle = PhysicsModule::kInvalidHandle;
    TetEmbedding embedding;
    std::vector<glm::vec3> restNormals;
    std::vector<size_t> partOffsets; // where each mesh part starts in the arrays above
    bool settled = false;            // asleep and drawn as it lies
};

Breakables::Breakables(Application& app) : m_app(app) {}
// Doesn't touch physics: on the way out PhysicsModule may be gone already.
// A game that keeps running without its breakables calls clear().
Breakables::~Breakables() = default;

Breakables::Entry* Breakables::find(ModelModule::InstanceId instance) const {
    for (const auto& e : m_entries)
        if (e->instance == instance) return e.get();
    return nullptr;
}

bool Breakables::has(ModelModule::InstanceId instance) const { return find(instance) != nullptr; }
uint32_t Breakables::handle(ModelModule::InstanceId instance) const {
    const Entry* e = find(instance);
    return e ? e->handle : 0;
}
size_t Breakables::count() const { return m_entries.size(); }

uint32_t Breakables::make(ModelModule::InstanceId instance, const glm::mat4& t, const Options& options, std::string* stats) {
    auto* physics = m_app.getModule<PhysicsModule>();
    auto* models = m_app.getModule<ModelModule>();
    if (!physics || !models || has(instance)) return 0;
    const ModelData* d = models->model(models->instanceModel(instance));
    if (!d) return 0;
    for (const ModelMesh& mesh : d->meshes)
        if (mesh.skinned) return 0; // a character: ragdolls, not tets
    const double start = static_cast<double>(SDL_GetPerformanceCounter()) / static_cast<double>(SDL_GetPerformanceFrequency());
    const glm::mat3 rot(glm::transpose(glm::inverse(glm::mat3(t)))); // normals, also right when scaled

    // 1. World-space vertices and triangles of every mesh part.
    std::vector<glm::vec3> points;
    std::vector<glm::vec2> uvs;
    std::vector<uint32_t> tris;
    for (const ModelMesh& mesh : d->meshes) {
        const uint32_t base = static_cast<uint32_t>(points.size());
        for (const ModelVertex& v : mesh.vertices) {
            points.push_back(glm::vec3(t * glm::vec4(v.position, 1.0f)));
            uvs.push_back(v.uv);
        }
        for (uint32_t i : mesh.indices) tris.push_back(base + i);
    }
    if (tris.empty()) return 0;
    // Simulated around the prop's centre (FEMFX adds the spawn position).
    glm::vec3 mn(1e30f), mx(-1e30f);
    for (const glm::vec3& p : points) {
        mn = glm::min(mn, p);
        mx = glm::max(mx, p);
    }
    const glm::vec3 center = (mn + mx) * 0.5f;
    for (glm::vec3& p : points) p -= center;
    const glm::vec3 size = mx - mn;
    const float maxDim = std::max({ size.x, size.y, size.z });
    if (options.maxSize > 0.0f && maxDim > options.maxSize) return 0;
    const bool tall = size.y > 2.5f * std::max(size.x, size.z);

    // 2. Tets, pieces.
    const BreakPreset& bp = breakPreset(options.kind);
    const FracturePattern pattern = options.overridePattern ? options.pattern : bp.pattern;
    // Pieces need several voxels each to have any shape: cells well under
    // the piece size (the budget grows them again for big props).
    const float cell = std::clamp(std::min(maxDim / 8.0f, bp.chunkSize * options.chunkScale * 0.4f), 0.04f, 0.4f);
    VoxelTetMesh vox = voxelizeToTets(points, tris, cell, options.maxCells);
    if (vox.mesh.tets.empty()) return 0;
    // One solid only: a scatter of pebbles or a set of separate parts in
    // one file voxelizes into islands that fly apart from the first step.
    if (chunkCount(faceConnectedComponents(vox.mesh)) > 1) {
        log::get("Breakables")->info("instance {}: left whole, it's several separate pieces in one model", instance);
        return 0;
    }
    // Hug the prop: pull the voxel surface onto the real triangles.
    fitSurfaceToMesh(vox.mesh, points, tris, vox.cellSize * 0.75f);
    FractureSeedOptions fo;
    fo.pattern = pattern;
    fo.chunkSize = bp.chunkSize * options.chunkScale;
    fo.seed = options.seed ? options.seed : 1u;
    fo.cellsPerCluster = bp.cellsPerCluster;
    BakedFracture baked = bakeFracture(vox.mesh, fo);
    vox.mesh = baked.cut.mesh; // same tets, border vertices on the Voronoi planes

    Material material = bp.material;
    material.fractureStressThreshold *= options.toughness;
    PhysicsModule::TetSpawnOptions opts;
    opts.fracture = pattern != FracturePattern::Solid;
    if (tall && options.anchorTall) {
        if (opts.fracture && !options.allowToppling) return 0;
        // Its foot: every tet corner in the bottom layer of cells, held.
        // A coarse 1-2 cell thick post carries its whole weight on its
        // foot: with sheet-metal numbers it would droop over and stay
        // bent. A real post is a stiff tube: stiffer, and it yields only
        // to a real hit.
        if (!opts.fracture) {
            material.stiffness *= 10.0f;
            material.plasticYieldThreshold *= 20.0f;
            float lowest = 1e30f;
            for (const glm::vec3& v : vox.mesh.vertices) lowest = std::min(lowest, v.y);
            for (size_t v = 0; v < vox.mesh.vertices.size(); ++v)
                if (vox.mesh.vertices[v].y < lowest + vox.cellSize3.y * 0.5f) opts.pinnedVerts.push_back(static_cast<uint32_t>(v));
        }
    }
    opts.plastic = bp.plastic;
    if (opts.fracture) {
        opts.chunkOfTet = baked.cut.chunkOfTet;
        opts.tetStrength = baked.cut.tetStrength;
    }
    opts.drawOnlyCracks = true;
    opts.armFractureAfterSeconds = options.armAfterSeconds;
    // Insides take the prop's own colours: each tet vertex gets the UV of
    // the nearest prop vertex, drawn with the prop's texture. Synty atlases
    // map each part to one colour swatch, so a blue crate is blue inside.
    opts.vertexUVs.resize(vox.mesh.vertices.size());
    for (size_t v = 0; v < vox.mesh.vertices.size(); ++v) {
        float best = 1e30f;
        for (size_t i = 0; i < points.size(); ++i) {
            const glm::vec3 dd = points[i] - vox.mesh.vertices[v];
            const float d2 = glm::dot(dd, dd);
            if (d2 < best) {
                best = d2;
                opts.vertexUVs[v] = uvs[i];
            }
        }
    }
    opts.texturePath = options.texture;
    if (opts.texturePath.empty())
        for (const ModelMaterial& m : d->materials)
            if (!m.albedoTexture.empty()) {
                opts.texturePath = m.albedoTexture;
                break;
            }
    // The inside of every piece: one colour, the texture's dominant colour
    // as this prop uses it (each triangle's UV centre, weighted by its
    // area: a stone pillar with a bronze trim is stone inside), a little
    // deeper. Per-vertex swatches made pieces patchy.
    if (!opts.texturePath.empty()) {
        std::vector<glm::vec2> centres;
        std::vector<float> areas;
        for (size_t i = 0; i + 2 < tris.size(); i += 3) {
            const uint32_t a = tris[i], b = tris[i + 1], c = tris[i + 2];
            centres.push_back((uvs[a] + uvs[b] + uvs[c]) / 3.0f);
            areas.push_back(0.5f * glm::length(glm::cross(points[b] - points[a], points[c] - points[a])));
        }
        opts.interior = interiorFillFromTexture(opts.texturePath, centres, areas);
        if (!opts.interior.valid)
            log::get("Breakables")->warn("no interior colour from '{}' (unreadable or fully transparent); pieces use the surface texture inside",
                                         opts.texturePath);
    }
    // 3 mm up so it doesn't start inside FEMFX's ground plane (y = 0); the
    // drawn mesh follows the tets, so the drop is invisible. A prop set
    // into the ground (Synty rocks sit a few cm deep) comes up out of it:
    // tets starting under the ground get shot out of it.
    float lowest = 1e30f;
    for (const glm::vec3& v : vox.mesh.vertices) lowest = std::min(lowest, v.y);
    float lift = 0.003f;
    if (center.y > 0.0f && center.y + lowest < 0.0f) lift -= center.y + lowest;
    const PhysicsModule::ObjectHandle h = physics->spawnTetMeshWithOptions(vox.mesh, center + glm::vec3(0.0f, lift, 0.0f), material, opts);
    if (h == PhysicsModule::kInvalidHandle) return 0;

    // 3. The drawn surface: every part as an unshared, subdivided triangle
    // soup glued triangle by triangle to the tets.
    auto e = std::make_unique<Entry>();
    e->instance = instance;
    e->handle = h;
    std::vector<std::vector<ModelVertex>> topology(d->meshes.size());
    std::vector<glm::vec3> soupPositions;
    std::vector<uint32_t> pieceOfTriangle;
    const size_t budgetPerPart = 12000 / std::max<size_t>(1, d->meshes.size());
    for (size_t m = 0; m < d->meshes.size(); ++m) {
        const ModelMesh& mesh = d->meshes[m];
        TriangleSoup soup;
        for (uint32_t i : mesh.indices) {
            const ModelVertex& v = mesh.vertices[i];
            soup.positions.push_back(glm::vec3(t * glm::vec4(v.position, 1.0f)) - center);
            soup.normals.push_back(glm::normalize(rot * v.normal));
            soup.uvs.push_back(v.uv);
        }
        // Triangles no longer than a cell bend with the tets; props that
        // only dent keep half a cell, so the dents show better
        // (docs/OPTIMIZATION.md #31).
        const float maxEdge = std::max({ vox.cellSize3.x, vox.cellSize3.y, vox.cellSize3.z }) * (opts.fracture ? 1.0f : 0.5f);
        subdivideSoup(soup, maxEdge, budgetPerPart);
        // Cut triangles that straddle a crack, so each piece's surface ends
        // exactly at its crack face. Room for the cuts: +50% over the budget.
        if (opts.fracture) {
            std::vector<uint32_t> pieces = splitSoupAtPieces(soup, vox.mesh, baked.cut.chunkOfTet, baked.seeds, budgetPerPart + budgetPerPart / 2);
            pieceOfTriangle.insert(pieceOfTriangle.end(), pieces.begin(), pieces.end());
        }
        e->partOffsets.push_back(soupPositions.size());
        for (size_t v = 0; v < soup.positions.size(); ++v) {
            ModelVertex mv;
            mv.position = soup.positions[v] + center;
            mv.normal = soup.normals[v];
            mv.uv = soup.uvs[v];
            topology[m].push_back(mv);
        }
        soupPositions.insert(soupPositions.end(), soup.positions.begin(), soup.positions.end());
        e->restNormals.insert(e->restNormals.end(), soup.normals.begin(), soup.normals.end());
    }
    e->embedding = opts.fracture ? embedTrianglesInPieces(vox.mesh, soupPositions, baked.cut.chunkOfTet, pieceOfTriangle)
                                 : embedTriangles(vox.mesh, soupPositions);
    // A corner glued far outside its tet (weights well past 0..1) swings
    // out as soon as the tet turns: a scatter of pebbles or a flag on a
    // one-cell pole draws long streaks. Such a prop stays a plain prop.
    float worst = 0.0f;
    for (const glm::vec4& w : e->embedding.weights) worst = std::max({ worst, -w.x, -w.y, -w.z, -w.w, w.x - 1.0f, w.y - 1.0f, w.z - 1.0f, w.w - 1.0f });
    if (worst > kMaxOutside) {
        physics->removeObject(h);
        log::get("Breakables")->info("instance {} ({}): left whole, its mesh reaches {:.1f} tets outside its voxel volume", instance, bp.id, worst);
        return 0;
    }
    models->setDeformedTopology(instance, topology);
    m_entries.push_back(std::move(e));
    if (stats) {
        const double ms = (static_cast<double>(SDL_GetPerformanceCounter()) / static_cast<double>(SDL_GetPerformanceFrequency()) - start) * 1000.0;
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s%s, %zu cells (%.2fx%.2fx%.2f m), %zu tets, %zu pieces (%s, seed %u), %zu triangles glued (reach %.1f), %.1f ms",
                      bp.id, opts.pinnedVerts.empty() ? "" : " anchored at its foot",
                      vox.solidCells, static_cast<double>(vox.cellSize3.x), static_cast<double>(vox.cellSize3.y), static_cast<double>(vox.cellSize3.z),
                      vox.mesh.tets.size(), opts.fracture ? baked.pieces : size_t(1), fracturePatternName(pattern), fo.seed, soupPositions.size() / 3,
                      static_cast<double>(worst), ms);
        *stats = buf;
    }
    return h;
}

void Breakables::remove(ModelModule::InstanceId instance) {
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if ((*it)->instance != instance) continue;
        if (auto* physics = m_app.getModule<PhysicsModule>()) physics->removeObject((*it)->handle);
        m_entries.erase(it);
        return;
    }
}

void Breakables::restore(ModelModule::InstanceId instance) {
    if (!has(instance)) return;
    remove(instance);
    if (auto* models = m_app.getModule<ModelModule>()) {
        models->setDeformedVertices(instance, {}, {});
        models->setVisible(instance, true);
    }
}

void Breakables::clear() {
    auto* physics = m_app.getModule<PhysicsModule>();
    if (physics)
        for (const auto& e : m_entries) physics->removeObject(e->handle);
    m_entries.clear();
}

// Skipped once a prop is asleep and its last pose was drawn: a settled
// pile of pieces costs nothing here (docs/OPTIMIZATION.md rule 1).
void Breakables::update() {
    m_lost.clear();
    auto* physics = m_app.getModule<PhysicsModule>();
    auto* models = m_app.getModule<ModelModule>();
    if (!physics || !models) return;
    std::vector<glm::vec3> pos, nrm;
    std::vector<std::vector<glm::vec3>> partPos, partNrm;
    for (size_t i = 0; i < m_entries.size();) {
        Entry& e = *m_entries[i];
        const bool asleep = physics->isObjectAsleep(e.handle);
        if (asleep && e.settled) {
            ++i;
            continue;
        }
        if (!physics->deformEmbedded(e.handle, e.embedding, e.restNormals, pos, nrm)) {
            // Physics dropped it (its runaway guard): a plain prop again.
            const ModelModule::InstanceId instance = e.instance;
            m_lost.push_back(instance);
            restore(instance);
            continue;
        }
        const size_t parts = e.partOffsets.size();
        partPos.resize(parts);
        partNrm.resize(parts);
        for (size_t p = 0; p < parts; ++p) {
            const size_t begin = e.partOffsets[p], end = p + 1 < parts ? e.partOffsets[p + 1] : pos.size();
            partPos[p].assign(pos.begin() + static_cast<std::ptrdiff_t>(begin), pos.begin() + static_cast<std::ptrdiff_t>(end));
            partNrm[p].assign(nrm.begin() + static_cast<std::ptrdiff_t>(begin), nrm.begin() + static_cast<std::ptrdiff_t>(end));
        }
        models->setDeformedVertices(e.instance, partPos, partNrm);
        e.settled = asleep;
        ++i;
    }
}

#else

// Without FEMFX: nothing breaks; props stay as they were.
struct Breakables::Entry {};
Breakables::Breakables(Application& app) : m_app(app) {}
Breakables::~Breakables() = default;
Breakables::Entry* Breakables::find(ModelModule::InstanceId) const { return nullptr; }
uint32_t Breakables::make(ModelModule::InstanceId, const glm::mat4&, const Options&, std::string*) { return 0; }
void Breakables::restore(ModelModule::InstanceId) {}
void Breakables::remove(ModelModule::InstanceId) {}
void Breakables::clear() {}
bool Breakables::has(ModelModule::InstanceId) const { return false; }
uint32_t Breakables::handle(ModelModule::InstanceId) const { return 0; }
size_t Breakables::count() const { return 0; }
void Breakables::update() {
    (void)m_app;
    m_lost.clear();
}

#endif

} // namespace kke
