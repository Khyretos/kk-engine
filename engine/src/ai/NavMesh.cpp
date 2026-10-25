#include "kke/ai/NavMesh.h"

#include <DetourCommon.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <DetourTileCache.h>
#include <DetourTileCacheBuilder.h>
#include <Recast.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace kke::ai {

namespace {

constexpr unsigned short kWalkFlag = 0x01;
constexpr int kMaxPolys = 1024;
constexpr int kMaxNodes = 4096;

template <typename T, void (*Free)(T*)>
struct RcPtr {
    T* p = nullptr;
    ~RcPtr() { if (p) Free(p); }
};

// Detour's random functions take a plain function pointer.
thread_local const std::function<float()>* t_random = nullptr;
float randomTrampoline() {
    const float r = t_random && *t_random ? (*t_random)() : 0.5f;
    return std::clamp(r, 0.0f, 0.99999f);
}

// The tile cache keeps each tile's layers between rebuilds. They are small
// (a byte or two per cell), so they are stored as they are.
struct StoreAsIs : dtTileCacheCompressor {
    int maxCompressedSize(const int bufferSize) override { return bufferSize; }
    dtStatus compress(const unsigned char* buffer, const int bufferSize, unsigned char* compressed, const int maxCompressedSize,
                      int* compressedSize) override {
        if (bufferSize > maxCompressedSize) return DT_FAILURE | DT_BUFFER_TOO_SMALL;
        std::memcpy(compressed, buffer, size_t(bufferSize));
        *compressedSize = bufferSize;
        return DT_SUCCESS;
    }
    dtStatus decompress(const unsigned char* compressed, const int compressedSize, unsigned char* buffer, const int maxBufferSize,
                        int* bufferSize) override {
        if (compressedSize > maxBufferSize) return DT_FAILURE | DT_BUFFER_TOO_SMALL;
        std::memcpy(buffer, compressed, size_t(compressedSize));
        *bufferSize = compressedSize;
        return DT_SUCCESS;
    }
};

// Rebuilt tiles: what is walkable gets the walk flag (obstacles' cells are
// already cut out of the layer).
struct FlagWalkable : dtTileCacheMeshProcess {
    void process(dtNavMeshCreateParams* params, unsigned char* polyAreas, unsigned short* polyFlags) override {
        for (int i = 0; i < params->polyCount; ++i) {
            if (polyAreas[i] == DT_TILECACHE_WALKABLE_AREA) polyAreas[i] = RC_WALKABLE_AREA;
            polyFlags[i] = polyAreas[i] == RC_WALKABLE_AREA ? kWalkFlag : 0;
        }
    }
};

// Recast's settings in cells, from the engine's in metres.
rcConfig recastConfig(const NavMeshSettings& s) {
    rcConfig cfg{};
    cfg.cs = s.cellSize;
    cfg.ch = s.cellHeight;
    cfg.walkableSlopeAngle = s.agentMaxSlope;
    cfg.walkableHeight = int(std::ceil(s.agentHeight / cfg.ch));
    cfg.walkableClimb = int(std::floor(s.agentMaxClimb / cfg.ch));
    cfg.walkableRadius = int(std::ceil(s.agentRadius / cfg.cs));
    cfg.maxEdgeLen = int(s.edgeMaxLength / cfg.cs);
    cfg.maxSimplificationError = s.edgeMaxError;
    cfg.minRegionArea = int(s.regionMinSize * s.regionMinSize);
    cfg.mergeRegionArea = int(s.regionMergeSize * s.regionMergeSize);
    cfg.maxVertsPerPoly = std::clamp(s.vertsPerPoly, 3, int(DT_VERTS_PER_POLYGON));
    cfg.detailSampleDist = s.detailSampleDistance < 0.9f ? 0.0f : cfg.cs * s.detailSampleDistance;
    cfg.detailSampleMaxError = cfg.ch * s.detailSampleMaxError;
    return cfg;
}

unsigned nextPow2(unsigned v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

unsigned ilog2(unsigned v) {
    unsigned r = 0;
    while (v >>= 1) ++r;
    return r;
}

} // namespace

struct NavMesh::Impl {
    NavMeshSettings settings;
    dtNavMesh* mesh = nullptr;
    dtNavMeshQuery* query = nullptr;
    dtQueryFilter filter;

    Impl() {
        filter.setIncludeFlags(kWalkFlag);
        filter.setExcludeFlags(0);
    }
    // Dynamic meshes: the tiles' layers, and what rebuilds them.
    dtTileCache* cache = nullptr;
    dtTileCacheAlloc cacheAlloc;
    StoreAsIs compressor;
    FlagWalkable process;
    uint32_t revision = 0;
    bool dirty = false; // obstacles changed since the mesh was last up to date
    std::unordered_set<dtObstacleRef> obstacles; // added and not removed

    ~Impl() { reset(); }
    void reset() {
        if (cache) dtFreeTileCache(cache);
        if (query) dtFreeNavMeshQuery(query);
        if (mesh) dtFreeNavMesh(mesh);
        cache = nullptr;
        obstacles.clear();
        dirty = false;
        query = nullptr;
        mesh = nullptr;
    }
    bool finish(unsigned char* data, int size, std::string* error) {
        mesh = dtAllocNavMesh();
        if (!mesh || dtStatusFailed(mesh->init(data, size, DT_TILE_FREE_DATA))) {
            dtFree(data);
            if (error) *error = "Detour could not load the mesh data";
            reset();
            return false;
        }
        query = dtAllocNavMeshQuery();
        if (!query || dtStatusFailed(query->init(mesh, kMaxNodes))) {
            if (error) *error = "Detour could not start a query";
            reset();
            return false;
        }
        return true;
    }
    dtPolyRef nearest(const glm::vec3& p, float* snapped, const glm::vec3& ext = { 2.0f, 4.0f, 2.0f }) const {
        if (!query) return 0;
        dtPolyRef ref = 0;
        const float half[3] = { ext.x, ext.y, ext.z };
        if (dtStatusFailed(query->findNearestPoly(&p.x, half, &filter, &ref, snapped))) return 0;
        return ref;
    }
};

NavMesh::NavMesh() : m(std::make_unique<Impl>()) {}
NavMesh::~NavMesh() = default;
NavMesh::NavMesh(NavMesh&&) noexcept = default;
NavMesh& NavMesh::operator=(NavMesh&&) noexcept = default;

bool NavMesh::valid() const { return m && m->query; }
const NavMeshSettings& NavMesh::settings() const { return m->settings; }

bool NavMesh::build(std::span<const glm::vec3> vertices, std::span<const uint32_t> indices, const NavMeshSettings& s, std::string* error) {
    m->reset();
    m->settings = s;
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    if (vertices.empty() || indices.size() < 3) return fail("no triangles to build from");
    const int nverts = int(vertices.size());
    const int ntris = int(indices.size() / 3);
    std::vector<int> tris(size_t(ntris) * 3);
    for (size_t i = 0; i < tris.size(); ++i) {
        if (indices[i] >= vertices.size()) return fail("a triangle index is past the last vertex");
        tris[i] = int(indices[i]);
    }
    const float* verts = &vertices[0].x;

    rcConfig cfg = recastConfig(s);
    rcCalcBounds(verts, nverts, cfg.bmin, cfg.bmax);
    rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

    rcContext ctx(false);
    RcPtr<rcHeightfield, rcFreeHeightField> solid{ rcAllocHeightfield() };
    if (!solid.p || !rcCreateHeightfield(&ctx, *solid.p, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch))
        return fail("Recast could not allocate the heightfield (level too big for the cell size?)");
    std::vector<unsigned char> areas(size_t(ntris), 0);
    rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle, verts, nverts, tris.data(), ntris, areas.data());
    if (!rcRasterizeTriangles(&ctx, verts, nverts, tris.data(), areas.data(), ntris, *solid.p, cfg.walkableClimb))
        return fail("Recast could not rasterize the triangles");
    rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *solid.p);
    rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid.p);
    rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *solid.p);

    RcPtr<rcCompactHeightfield, rcFreeCompactHeightfield> chf{ rcAllocCompactHeightfield() };
    if (!chf.p || !rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid.p, *chf.p))
        return fail("Recast could not build the compact heightfield");
    if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf.p)) return fail("Recast could not erode the walkable area");
    if (!rcBuildDistanceField(&ctx, *chf.p)) return fail("Recast could not build the distance field");
    if (!rcBuildRegions(&ctx, *chf.p, 0, cfg.minRegionArea, cfg.mergeRegionArea)) return fail("Recast could not build regions");

    RcPtr<rcContourSet, rcFreeContourSet> cset{ rcAllocContourSet() };
    if (!cset.p || !rcBuildContours(&ctx, *chf.p, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset.p))
        return fail("Recast could not build contours");
    if (cset.p->nconts == 0) return fail("nothing walkable (too steep, too small, or too low for the agent)");
    RcPtr<rcPolyMesh, rcFreePolyMesh> pmesh{ rcAllocPolyMesh() };
    if (!pmesh.p || !rcBuildPolyMesh(&ctx, *cset.p, cfg.maxVertsPerPoly, *pmesh.p)) return fail("Recast could not build polygons");
    RcPtr<rcPolyMeshDetail, rcFreePolyMeshDetail> dmesh{ rcAllocPolyMeshDetail() };
    if (!dmesh.p || !rcBuildPolyMeshDetail(&ctx, *pmesh.p, *chf.p, cfg.detailSampleDist, cfg.detailSampleMaxError, *dmesh.p))
        return fail("Recast could not build the detail mesh");
    if (pmesh.p->npolys == 0) return fail("nothing walkable (too steep, too small, or too low for the agent)");

    for (int i = 0; i < pmesh.p->npolys; ++i) {
        pmesh.p->flags[i] = pmesh.p->areas[i] == RC_WALKABLE_AREA ? kWalkFlag : 0;
    }

    dtNavMeshCreateParams params{};
    params.verts = pmesh.p->verts;
    params.vertCount = pmesh.p->nverts;
    params.polys = pmesh.p->polys;
    params.polyAreas = pmesh.p->areas;
    params.polyFlags = pmesh.p->flags;
    params.polyCount = pmesh.p->npolys;
    params.nvp = pmesh.p->nvp;
    params.detailMeshes = dmesh.p->meshes;
    params.detailVerts = dmesh.p->verts;
    params.detailVertsCount = dmesh.p->nverts;
    params.detailTris = dmesh.p->tris;
    params.detailTriCount = dmesh.p->ntris;
    params.walkableHeight = s.agentHeight;
    params.walkableRadius = s.agentRadius;
    params.walkableClimb = s.agentMaxClimb;
    rcVcopy(params.bmin, pmesh.p->bmin);
    rcVcopy(params.bmax, pmesh.p->bmax);
    params.cs = cfg.cs;
    params.ch = cfg.ch;
    params.buildBvTree = true;
    unsigned char* data = nullptr;
    int dataSize = 0;
    if (!dtCreateNavMeshData(&params, &data, &dataSize)) return fail("Detour could not create the mesh data (too many vertices?)");
    return m->finish(data, dataSize, error);
}

bool NavMesh::buildDynamic(std::span<const glm::vec3> vertices, std::span<const uint32_t> indices, const NavMeshSettings& s,
                           const DynamicNavSettings& d, std::string* error) {
    m->reset();
    m->settings = s;
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        m->reset();
        return false;
    };
    if (vertices.empty() || indices.size() < 3) return fail("no triangles to build from");
    const int nverts = int(vertices.size());
    const int ntris = int(indices.size() / 3);
    std::vector<int> tris(size_t(ntris) * 3);
    for (size_t i = 0; i < tris.size(); ++i) {
        if (indices[i] >= vertices.size()) return fail("a triangle index is past the last vertex");
        tris[i] = int(indices[i]);
    }
    const float* verts = &vertices[0].x;

    rcConfig base = recastConfig(s);
    float bmin[3], bmax[3];
    rcCalcBounds(verts, nverts, bmin, bmax);
    const int tileCells = std::max(16, int(std::lround(std::max(1.0f, d.tileSize) / base.cs)));
    const float tileWorld = float(tileCells) * base.cs;
    int gw = 0, gh = 0;
    rcCalcGridSize(bmin, bmax, base.cs, &gw, &gh);
    const int tilesX = (gw + tileCells - 1) / tileCells;
    const int tilesZ = (gh + tileCells - 1) / tileCells;
    constexpr int kLayersPerTile = 4; // floors stacked over one tile (a bridge over a road)
    const unsigned maxTiles = nextPow2(unsigned(tilesX * tilesZ * kLayersPerTile));
    const unsigned tileBits = std::min(ilog2(maxTiles), 14u);
    if (unsigned(tilesX * tilesZ) > (1u << tileBits) / kLayersPerTile + 1)
        return fail("the level needs " + std::to_string(tilesX * tilesZ) + " tiles; use a bigger DynamicNavSettings::tileSize");

    dtTileCacheParams tc{};
    rcVcopy(tc.orig, bmin);
    tc.cs = base.cs;
    tc.ch = base.ch;
    tc.width = tileCells;
    tc.height = tileCells;
    tc.walkableHeight = s.agentHeight;
    tc.walkableRadius = s.agentRadius;
    tc.walkableClimb = s.agentMaxClimb;
    tc.maxSimplificationError = s.edgeMaxError;
    tc.maxTiles = tilesX * tilesZ * kLayersPerTile;
    tc.maxObstacles = std::clamp(d.maxObstacles, 1, 4096);
    m->cache = dtAllocTileCache();
    if (!m->cache || dtStatusFailed(m->cache->init(&tc, &m->cacheAlloc, &m->compressor, &m->process))) return fail("Detour could not start the tile cache");

    dtNavMeshParams np{};
    rcVcopy(np.orig, bmin);
    np.tileWidth = tileWorld;
    np.tileHeight = tileWorld;
    np.maxTiles = int(1u << tileBits);
    np.maxPolys = int(1u << (22 - tileBits));
    m->mesh = dtAllocNavMesh();
    if (!m->mesh || dtStatusFailed(m->mesh->init(&np))) return fail("Detour could not start the tiled mesh");
    m->query = dtAllocNavMeshQuery();
    if (!m->query || dtStatusFailed(m->query->init(m->mesh, kMaxNodes))) return fail("Detour could not start a query");

    // Each tile: the triangles over it (and a border, so tiles meet without
    // gaps) become a heightfield, then layers (one per floor) the cache keeps.
    rcContext ctx(false);
    int layers = 0;
    std::vector<unsigned char> areas;
    std::vector<int> tileTris;
    for (int tz = 0; tz < tilesZ; ++tz)
        for (int tx = 0; tx < tilesX; ++tx) {
            rcConfig cfg = base;
            cfg.tileSize = tileCells;
            cfg.borderSize = cfg.walkableRadius + 3;
            cfg.width = cfg.tileSize + cfg.borderSize * 2;
            cfg.height = cfg.tileSize + cfg.borderSize * 2;
            rcVcopy(cfg.bmin, bmin);
            rcVcopy(cfg.bmax, bmax);
            cfg.bmin[0] = bmin[0] + float(tx) * tileWorld - float(cfg.borderSize) * cfg.cs;
            cfg.bmin[2] = bmin[2] + float(tz) * tileWorld - float(cfg.borderSize) * cfg.cs;
            cfg.bmax[0] = bmin[0] + float(tx + 1) * tileWorld + float(cfg.borderSize) * cfg.cs;
            cfg.bmax[2] = bmin[2] + float(tz + 1) * tileWorld + float(cfg.borderSize) * cfg.cs;

            tileTris.clear();
            for (int t = 0; t < ntris; ++t) {
                const glm::vec3& a = vertices[size_t(tris[size_t(t) * 3])];
                const glm::vec3& b = vertices[size_t(tris[size_t(t) * 3 + 1])];
                const glm::vec3& c = vertices[size_t(tris[size_t(t) * 3 + 2])];
                if (std::max({ a.x, b.x, c.x }) < cfg.bmin[0] || std::min({ a.x, b.x, c.x }) > cfg.bmax[0] ||
                    std::max({ a.z, b.z, c.z }) < cfg.bmin[2] || std::min({ a.z, b.z, c.z }) > cfg.bmax[2])
                    continue;
                tileTris.insert(tileTris.end(), &tris[size_t(t) * 3], &tris[size_t(t) * 3] + 3);
            }
            if (tileTris.empty()) continue;
            const int n = int(tileTris.size() / 3);

            RcPtr<rcHeightfield, rcFreeHeightField> solid{ rcAllocHeightfield() };
            if (!solid.p || !rcCreateHeightfield(&ctx, *solid.p, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch))
                return fail("Recast could not allocate a tile's heightfield");
            areas.assign(size_t(n), 0);
            rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle, verts, nverts, tileTris.data(), n, areas.data());
            if (!rcRasterizeTriangles(&ctx, verts, nverts, tileTris.data(), areas.data(), n, *solid.p, cfg.walkableClimb))
                return fail("Recast could not rasterize a tile's triangles");
            rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *solid.p);
            rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid.p);
            rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *solid.p);
            RcPtr<rcCompactHeightfield, rcFreeCompactHeightfield> chf{ rcAllocCompactHeightfield() };
            if (!chf.p || !rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid.p, *chf.p))
                return fail("Recast could not build a tile's compact heightfield");
            if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf.p)) return fail("Recast could not erode a tile");
            RcPtr<rcHeightfieldLayerSet, rcFreeHeightfieldLayerSet> lset{ rcAllocHeightfieldLayerSet() };
            if (!lset.p || !rcBuildHeightfieldLayers(&ctx, *chf.p, cfg.borderSize, cfg.walkableHeight, *lset.p))
                return fail("Recast could not build a tile's layers");
            for (int i = 0; i < std::min(lset.p->nlayers, kLayersPerTile); ++i) {
                const rcHeightfieldLayer& layer = lset.p->layers[i];
                dtTileCacheLayerHeader h{};
                h.magic = DT_TILECACHE_MAGIC;
                h.version = DT_TILECACHE_VERSION;
                h.tx = tx;
                h.ty = tz;
                h.tlayer = i;
                rcVcopy(h.bmin, layer.bmin);
                rcVcopy(h.bmax, layer.bmax);
                h.width = static_cast<unsigned char>(layer.width);
                h.height = static_cast<unsigned char>(layer.height);
                h.minx = static_cast<unsigned char>(layer.minx);
                h.maxx = static_cast<unsigned char>(layer.maxx);
                h.miny = static_cast<unsigned char>(layer.miny);
                h.maxy = static_cast<unsigned char>(layer.maxy);
                h.hmin = static_cast<unsigned short>(layer.hmin);
                h.hmax = static_cast<unsigned short>(layer.hmax);
                unsigned char* data = nullptr;
                int size = 0;
                if (dtStatusFailed(dtBuildTileCacheLayer(&m->compressor, &h, layer.heights, layer.areas, layer.cons, &data, &size)))
                    return fail("Detour could not store a tile layer");
                if (dtStatusFailed(m->cache->addTile(data, size, DT_COMPRESSEDTILE_FREE_DATA, nullptr))) {
                    dtFree(data);
                    return fail("Detour's tile cache is full");
                }
                ++layers;
            }
        }
    if (layers == 0) return fail("nothing walkable (too steep, too small, or too low for the agent)");
    for (int tz = 0; tz < tilesZ; ++tz)
        for (int tx = 0; tx < tilesX; ++tx)
            if (dtStatusFailed(m->cache->buildNavMeshTilesAt(tx, tz, m->mesh))) return fail("Detour could not build a tile");
    if (polygonCount() == 0) return fail("nothing walkable (too steep, too small, or too low for the agent)");
    return true;
}

bool NavMesh::dynamic() const { return m->cache != nullptr; }

NavMesh::ObstacleId NavMesh::addCylinderObstacle(const glm::vec3& base, float radius, float height) {
    if (!m->cache || !(radius > 0.0f) || !(height > 0.0f) || !std::isfinite(base.x + base.y + base.z)) return 0;
    dtObstacleRef ref = 0;
    float p[3] = { base.x, base.y, base.z };
    if (dtStatusFailed(m->cache->addObstacle(p, radius, height, &ref))) return 0;
    m->obstacles.insert(ref);
    m->dirty = true;
    return ref;
}

NavMesh::ObstacleId NavMesh::addBoxObstacle(const glm::vec3& centre, const glm::vec3& halfExtents, float yawRadians) {
    if (!m->cache || !(halfExtents.x > 0.0f && halfExtents.y > 0.0f && halfExtents.z > 0.0f) ||
        !std::isfinite(centre.x + centre.y + centre.z + yawRadians))
        return 0;
    dtObstacleRef ref = 0;
    float c[3] = { centre.x, centre.y, centre.z }, h[3] = { halfExtents.x, halfExtents.y, halfExtents.z };
    // Detour's yaw turns +x towards -z, as glm::rotate about +y does.
    if (dtStatusFailed(m->cache->addBoxObstacle(c, h, yawRadians, &ref))) return 0;
    m->obstacles.insert(ref);
    m->dirty = true;
    return ref;
}

bool NavMesh::removeObstacle(ObstacleId id) {
    if (!m->cache || !m->obstacles.count(id)) return false;
    if (dtStatusFailed(m->cache->removeObstacle(id))) return false; // too many changes waiting: update() first
    m->obstacles.erase(id);
    m->dirty = true;
    return true;
}

size_t NavMesh::obstacleCount() const {
    return m->obstacles.size();
}

bool NavMesh::update() {
    if (!m->cache) return true;
    bool upToDate = false;
    if (dtStatusFailed(m->cache->update(0.0f, m->mesh, &upToDate))) return false;
    if (m->dirty) {
        ++m->revision; // tiles were rebuilt by this call
        m->dirty = !upToDate;
    }
    return upToDate;
}

bool NavMesh::updateAll(int maxSteps) {
    for (int i = 0; i < maxSteps; ++i)
        if (update()) return true;
    return false;
}

uint32_t NavMesh::revision() const { return m->revision; }

size_t NavMesh::tileCount() const {
    if (!m->mesh) return 0;
    const dtNavMesh* mesh = m->mesh;
    size_t n = 0;
    for (int t = 0; t < mesh->getMaxTiles(); ++t)
        if (const dtMeshTile* tile = mesh->getTile(t); tile && tile->header) ++n;
    return n;
}

std::vector<uint8_t> NavMesh::save() const {
    std::vector<uint8_t> out;
    if (!m->mesh || m->cache) return out;
    const dtNavMesh* mesh = m->mesh;
    const dtMeshTile* tile = mesh->getTile(0);
    if (!tile || !tile->header || !tile->data) return out;
    // Settings first (so a loaded mesh knows its agent), then Detour's tile.
    out.resize(sizeof(NavMeshSettings) + size_t(tile->dataSize));
    std::memcpy(out.data(), &m->settings, sizeof(NavMeshSettings));
    std::memcpy(out.data() + sizeof(NavMeshSettings), tile->data, size_t(tile->dataSize));
    return out;
}

bool NavMesh::load(std::span<const uint8_t> bytes, std::string* error) {
    m->reset();
    if (bytes.size() <= sizeof(NavMeshSettings) + sizeof(dtMeshHeader)) {
        if (error) *error = "not a saved navmesh (too short)";
        return false;
    }
    std::memcpy(&m->settings, bytes.data(), sizeof(NavMeshSettings));
    const size_t size = bytes.size() - sizeof(NavMeshSettings);
    dtMeshHeader header;
    std::memcpy(&header, bytes.data() + sizeof(NavMeshSettings), sizeof(header));
    if (header.magic != DT_NAVMESH_MAGIC || header.version != DT_NAVMESH_VERSION) {
        if (error) *error = "not a saved navmesh (wrong magic or version)";
        return false;
    }
    auto* data = static_cast<unsigned char*>(dtAlloc(int(size), DT_ALLOC_PERM));
    if (!data) {
        if (error) *error = "out of memory";
        return false;
    }
    std::memcpy(data, bytes.data() + sizeof(NavMeshSettings), size);
    return m->finish(data, int(size), error);
}

bool NavMesh::nearestPoint(const glm::vec3& p, glm::vec3& out, const glm::vec3& searchExtents) const {
    float snapped[3];
    if (!m->nearest(p, snapped, searchExtents)) return false;
    out = { snapped[0], snapped[1], snapped[2] };
    return true;
}

bool NavMesh::findPath(const glm::vec3& from, const glm::vec3& to, Path& out) const {
    out.points.clear();
    out.partial = false;
    float start[3], end[3];
    const dtPolyRef startRef = m->nearest(from, start);
    const dtPolyRef endRef = m->nearest(to, end);
    if (!startRef || !endRef) return false;
    dtPolyRef polys[kMaxPolys];
    int npolys = 0;
    const dtStatus st = m->query->findPath(startRef, endRef, start, end, &m->filter, polys, &npolys, kMaxPolys);
    if (dtStatusFailed(st) || npolys == 0) return false;
    float target[3] = { end[0], end[1], end[2] };
    if (polys[npolys - 1] != endRef) {
        // Unreachable: go to the closest point of the last polygon reached.
        out.partial = true;
        bool over = false;
        m->query->closestPointOnPoly(polys[npolys - 1], end, target, &over);
    }
    float straight[kMaxPolys * 3];
    int nstraight = 0;
    if (dtStatusFailed(m->query->findStraightPath(start, target, polys, npolys, straight, nullptr, nullptr, &nstraight, kMaxPolys)))
        return false;
    out.points.reserve(size_t(nstraight));
    for (int i = 0; i < nstraight; ++i) out.points.push_back({ straight[i * 3], straight[i * 3 + 1], straight[i * 3 + 2] });
    if (dtStatusDetail(st, DT_PARTIAL_RESULT)) out.partial = true;
    return !out.points.empty();
}

glm::vec3 NavMesh::moveAlongSurface(const glm::vec3& from, const glm::vec3& to) const {
    float start[3];
    const dtPolyRef ref = m->nearest(from, start);
    if (!ref) return to;
    float result[3];
    dtPolyRef visited[16];
    int nvisited = 0;
    if (dtStatusFailed(m->query->moveAlongSurface(ref, start, &to.x, &m->filter, result, visited, &nvisited, 16))) return from;
    // moveAlongSurface keeps the start height; ask the polygon it ended on.
    if (nvisited > 0) {
        float h = result[1];
        if (dtStatusSucceed(m->query->getPolyHeight(visited[nvisited - 1], result, &h))) result[1] = h;
    }
    return { result[0], result[1], result[2] };
}

bool NavMesh::walkable(const glm::vec3& from, const glm::vec3& to, float* hitFraction) const {
    float start[3];
    const dtPolyRef ref = m->nearest(from, start);
    if (!ref) {
        if (hitFraction) *hitFraction = 0.0f;
        return false;
    }
    float t = 0.0f, normal[3];
    dtPolyRef path[64];
    int npath = 0;
    if (dtStatusFailed(m->query->raycast(ref, start, &to.x, &m->filter, &t, normal, path, &npath, 64))) {
        if (hitFraction) *hitFraction = 0.0f;
        return false;
    }
    const bool clear = t > 1.0f; // FLT_MAX: no wall
    if (hitFraction) *hitFraction = clear ? 1.0f : t;
    return clear;
}

bool NavMesh::randomPointNear(const glm::vec3& centre, float radius, const std::function<float()>& random01, glm::vec3& out) const {
    float start[3];
    const dtPolyRef ref = m->nearest(centre, start, { std::max(radius, 2.0f), 4.0f, std::max(radius, 2.0f) });
    if (!ref) return false;
    t_random = &random01;
    dtPolyRef resultRef = 0;
    float pt[3];
    const dtStatus st = m->query->findRandomPointAroundCircle(ref, start, radius, &m->filter, randomTrampoline, &resultRef, pt);
    t_random = nullptr;
    if (dtStatusFailed(st)) return false;
    out = { pt[0], pt[1], pt[2] };
    return true;
}

void NavMesh::debugTriangles(std::vector<glm::vec3>& out, float lift) const {
    if (!m->mesh) return;
    const dtNavMesh* mesh = m->mesh;
    for (int t = 0; t < mesh->getMaxTiles(); ++t) {
        const dtMeshTile* tile = mesh->getTile(t);
        if (!tile || !tile->header) continue;
        for (int i = 0; i < tile->header->polyCount; ++i) {
            const dtPoly& p = tile->polys[i];
            if (p.getType() == DT_POLYTYPE_OFFMESH_CONNECTION) continue;
            const dtPolyDetail& pd = tile->detailMeshes[i];
            for (int j = 0; j < pd.triCount; ++j) {
                const unsigned char* tv = &tile->detailTris[(pd.triBase + unsigned(j)) * 4];
                for (int k = 0; k < 3; ++k) {
                    const float* v = tv[k] < p.vertCount ? &tile->verts[p.verts[tv[k]] * 3]
                                                          : &tile->detailVerts[(pd.vertBase + tv[k] - p.vertCount) * 3];
                    out.push_back({ v[0], v[1] + lift, v[2] });
                }
            }
        }
    }
}

size_t NavMesh::polygonCount() const {
    if (!m->mesh) return 0;
    const dtNavMesh* mesh = m->mesh;
    size_t n = 0;
    for (int t = 0; t < mesh->getMaxTiles(); ++t) {
        const dtMeshTile* tile = mesh->getTile(t);
        if (tile && tile->header) n += size_t(tile->header->polyCount);
    }
    return n;
}

} // namespace kke::ai
