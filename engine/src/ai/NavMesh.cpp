#include "kke/ai/NavMesh.h"

#include <DetourCommon.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>

#include <algorithm>
#include <cmath>
#include <cstring>

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
    ~Impl() { reset(); }
    void reset() {
        if (query) dtFreeNavMeshQuery(query);
        if (mesh) dtFreeNavMesh(mesh);
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

std::vector<uint8_t> NavMesh::save() const {
    std::vector<uint8_t> out;
    if (!m->mesh) return out;
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
