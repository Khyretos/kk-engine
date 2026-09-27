#include "kke/ClimbWall.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace kke {

namespace {

// Deterministic everywhere: our own generator and float conversion, no
// std:: distributions (their output differs between standard libraries).
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) {}
    uint32_t next() {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return static_cast<uint32_t>((s * 0x2545F4914F6CDD1Dull) >> 32);
    }
    float unit() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); } // [0, 1)
    float range(float a, float b) { return a + (b - a) * unit(); }
};

uint32_t hash3(int x, int y, uint32_t seed) {
    uint32_t h = seed * 0x27d4eb2du ^ static_cast<uint32_t>(x) * 0x85ebca6bu ^ static_cast<uint32_t>(y) * 0xc2b2ae35u;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return h;
}

float lattice(int x, int y, uint32_t seed) { return static_cast<float>(hash3(x, y, seed) >> 8) * (2.0f / 16777216.0f) - 1.0f; }

// Smooth value noise, -1..1.
float valueNoise(float x, float y, uint32_t seed) {
    const float fx = std::floor(x), fy = std::floor(y);
    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
    float tx = x - fx, ty = y - fy;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    const float a = lattice(ix, iy, seed), b = lattice(ix + 1, iy, seed);
    const float c = lattice(ix, iy + 1, seed), d = lattice(ix + 1, iy + 1, seed);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

float fbm(float x, float y, uint32_t seed, int octaves) {
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * valueNoise(x, y, seed + static_cast<uint32_t>(o) * 101u);
        norm += amp;
        x *= 2.03f;
        y *= 2.03f;
        amp *= 0.5f;
    }
    return sum / norm;
}

glm::vec3 holdColor(ClimbHold::Kind k) {
    switch (k) {
    // Green = good (jugs), orange = hard (crimps), blue = hardest (slopers),
    // as the how-to-play screen and the HUD say.
    case ClimbHold::Kind::Jug: return { 0.28f, 0.78f, 0.32f };
    case ClimbHold::Kind::Crimp: return { 0.95f, 0.52f, 0.12f };
    case ClimbHold::Kind::Sloper: return { 0.18f, 0.45f, 0.95f };
    case ClimbHold::Kind::Edge: return { 0.55f, 0.5f, 0.45f };
    }
    return { 1.0f, 1.0f, 1.0f };
}

// How far the hand's grip point sits off the rock, and how far the hold
// sticks out (its apex).
float holdDepth(ClimbHold::Kind k, float size) {
    switch (k) {
    case ClimbHold::Kind::Jug: return size * 0.95f;
    case ClimbHold::Kind::Crimp: return size * 0.55f;
    case ClimbHold::Kind::Sloper: return size * 0.5f;
    case ClimbHold::Kind::Edge: return 0.0f;
    }
    return size;
}

void addTriangle(ClimbMesh& m, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& color) {
    glm::vec3 n = glm::cross(b - a, c - a);
    const float len = glm::length(n);
    if (len < 1e-9f) return;
    n /= len;
    const uint32_t base = static_cast<uint32_t>(m.positions.size());
    for (const glm::vec3& p : { a, b, c }) {
        m.positions.push_back(p);
        m.normals.push_back(n);
        m.colors.push_back(color);
    }
    m.indices.insert(m.indices.end(), { base, base + 1, base + 2 });
}

// Two triangles, counter-clockwise seen from where `facing` points.
void addQuad(ClimbMesh& m, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, const glm::vec3& facing,
             const glm::vec3& color) {
    if (glm::dot(glm::cross(b - a, c - a), facing) >= 0.0f) {
        addTriangle(m, a, b, c, color);
        addTriangle(m, a, c, d, color);
    } else {
        addTriangle(m, a, c, b, color);
        addTriangle(m, a, d, c, color);
    }
}

} // namespace

float holdGrip(ClimbHold::Kind kind) {
    switch (kind) {
    case ClimbHold::Kind::Jug: return 1.0f;
    case ClimbHold::Kind::Crimp: return 0.6f;
    case ClimbHold::Kind::Sloper: return 0.45f;
    case ClimbHold::Kind::Edge: return 1.0f;
    }
    return 1.0f;
}

const char* holdKindName(ClimbHold::Kind kind) {
    switch (kind) {
    case ClimbHold::Kind::Jug: return "jug";
    case ClimbHold::Kind::Crimp: return "crimp";
    case ClimbHold::Kind::Sloper: return "sloper";
    case ClimbHold::Kind::Edge: return "edge";
    }
    return "?";
}

ClimbWall ClimbWall::generate(const ClimbWallDesc& desc) {
    ClimbWall w;
    w.m_desc = desc;
    w.m_desc.cell = std::max(0.1f, desc.cell);
    w.m_desc.width = std::max(4.0f, desc.width);
    w.m_desc.height = std::max(4.0f, desc.height);
    w.buildSurface();
    w.placeLedges();
    w.placeHolds();
    return w;
}

void ClimbWall::buildSurface() {
    const ClimbWallDesc& d = m_desc;
    m_cols = static_cast<int>(std::ceil(d.width / d.cell)) + 1;
    m_rows = static_cast<int>(std::ceil(d.height / d.cell)) + 1;
    Rng rng(d.seed);

    // Bands of one lean, bottom to top. The first eases in (a slab or
    // vertical), one around the middle is always an overhang (the crux),
    // and the last one before the top leans back a little.
    struct Band { float top, lean; };
    std::vector<Band> bands;
    float y = 0.0f;
    bool overhang = false;
    while (y < d.height) {
        const float top = std::min(d.height, y + rng.range(d.sectionMin, d.sectionMax));
        float lean;
        const float mid = (y + top) * 0.5f;
        if (bands.empty()) lean = rng.range(-d.maxSlab * 0.6f, 0.0f);
        else if (top >= d.height) lean = rng.range(-d.maxSlab, -6.0f);
        else if (!overhang && mid > d.height * 0.4f) lean = rng.range(d.maxOverhang * 0.6f, d.maxOverhang);
        else {
            const float pick = rng.unit();
            lean = pick < 0.35f ? rng.range(-d.maxSlab, -8.0f) : pick < 0.7f ? rng.range(-4.0f, 4.0f) : rng.range(8.0f, d.maxOverhang);
        }
        overhang = overhang || lean > 5.0f;
        bands.push_back({ top, lean });
        y = top;
    }
    // Lean per row, blended over a metre across band edges.
    m_lean.assign(static_cast<size_t>(m_rows), 0.0f);
    auto bandLean = [&](float yy) {
        for (const Band& b : bands)
            if (yy <= b.top) return b.lean;
        return bands.back().lean;
    };
    for (int r = 0; r < m_rows; ++r) {
        const float yy = static_cast<float>(r) * d.cell;
        float sum = 0.0f;
        for (int k = -4; k <= 4; ++k) sum += bandLean(std::clamp(yy + static_cast<float>(k) * 0.25f, 0.0f, d.height));
        m_lean[static_cast<size_t>(r)] = sum / 9.0f;
    }
    // The profile: z climbs with tan(lean) per metre of height.
    std::vector<float> profile(static_cast<size_t>(m_rows), 0.0f);
    for (int r = 1; r < m_rows; ++r)
        profile[static_cast<size_t>(r)] = profile[static_cast<size_t>(r - 1)] + std::tan(glm::radians(m_lean[static_cast<size_t>(r)])) * d.cell;

    const uint32_t seed = d.seed * 7919u + 17u;
    m_z.assign(static_cast<size_t>(m_rows * m_cols), 0.0f);
    for (int r = 0; r < m_rows; ++r) {
        const float yy = static_cast<float>(r) * d.cell;
        // Calm near the ground and at the lip, so standing and topping out are clean.
        const float calm = std::clamp(yy / 1.5f, 0.0f, 1.0f) * std::clamp((d.height - yy) / 1.0f, 0.0f, 1.0f);
        for (int c = 0; c < m_cols; ++c) {
            const float x = -d.width * 0.5f + static_cast<float>(c) * d.cell;
            const float relief = d.bumpiness * fbm(x * 0.9f, yy * 0.9f, seed, 3) + d.buttress * valueNoise(x * 0.2f, yy * 0.06f, seed + 999u);
            m_z[static_cast<size_t>(r * m_cols + c)] = profile[static_cast<size_t>(r)] + relief * calm;
        }
    }
}

float ClimbWall::surfaceZ(float x, float y) const {
    if (m_cols < 2 || m_rows < 2) return 0.0f;
    const float fx = std::clamp((x + m_desc.width * 0.5f) / m_desc.cell, 0.0f, static_cast<float>(m_cols - 1) - 1e-4f);
    const float fy = std::clamp(y / m_desc.cell, 0.0f, static_cast<float>(m_rows - 1) - 1e-4f);
    const int c = static_cast<int>(fx), r = static_cast<int>(fy);
    const float tx = fx - static_cast<float>(c), ty = fy - static_cast<float>(r);
    auto at = [&](int rr, int cc) { return m_z[static_cast<size_t>(rr * m_cols + cc)]; };
    const float a = at(r, c) + (at(r, c + 1) - at(r, c)) * tx;
    const float b = at(r + 1, c) + (at(r + 1, c + 1) - at(r + 1, c)) * tx;
    return a + (b - a) * ty;
}

glm::vec3 ClimbWall::surfaceNormal(float x, float y) const {
    const float h = m_desc.cell * 0.5f;
    const float dzdx = (surfaceZ(x + h, y) - surfaceZ(x - h, y)) / (2.0f * h);
    const float dzdy = (surfaceZ(x, y + h) - surfaceZ(x, y - h)) / (2.0f * h);
    return glm::normalize(glm::vec3(-dzdx, -dzdy, 1.0f));
}

glm::vec3 ClimbWall::surfacePoint(float x, float y, float out) const {
    return glm::vec3(x, y, surfaceZ(x, y)) + surfaceNormal(x, y) * out;
}

float ClimbWall::leanAt(float y) const {
    if (m_lean.empty()) return 0.0f;
    const float f = std::clamp(y / m_desc.cell, 0.0f, static_cast<float>(m_rows - 1));
    const int r = std::min(static_cast<int>(f), m_rows - 2 < 0 ? 0 : m_rows - 2);
    const float t = f - static_cast<float>(r);
    const float a = m_lean[static_cast<size_t>(r)];
    const float b = m_lean[static_cast<size_t>(std::min(r + 1, m_rows - 1))];
    return a + (b - a) * t;
}

void ClimbWall::placeLedges() {
    const ClimbWallDesc& d = m_desc;
    Rng rng(d.seed * 31u + 5u);
    m_ledges.clear();
    for (int i = 0; i < d.ledges; ++i) {
        const float y = d.height * static_cast<float>(i + 1) / static_cast<float>(d.ledges + 1) + rng.range(-1.2f, 1.2f);
        const float hw = rng.range(1.4f, 2.4f);
        const float room = std::max(0.0f, d.width * 0.5f - d.margin - hw);
        const float cx = rng.range(-room, room);
        // The back sits inside the rock wherever it bulges most; the
        // front stands 0.9 m clear of the furthest-out rock.
        float zMin = 1e9f, zMax = -1e9f;
        for (float x = cx - hw; x <= cx + hw + 1e-3f; x += d.cell * 0.5f)
            for (float yy = y - 0.4f; yy <= y + 0.05f; yy += d.cell * 0.5f) {
                const float z = surfaceZ(x, yy);
                zMin = std::min(zMin, z);
                zMax = std::max(zMax, z);
            }
        const float back = zMin - 0.35f, front = zMax + 0.9f;
        ClimbLedge l;
        l.halfExtents = glm::vec3(hw, 0.2f, (front - back) * 0.5f);
        l.center = glm::vec3(cx, y - l.halfExtents.y, (front + back) * 0.5f);
        m_ledges.push_back(l);
    }
}

bool ClimbWall::blockedByLedge(float x, float y) const {
    for (const ClimbLedge& l : m_ledges)
        if (std::abs(x - l.center.x) < l.halfExtents.x + 0.3f && y > l.top() - 0.2f && y < l.top() + 0.35f) return true;
    return false;
}

ClimbHold ClimbWall::makeHold(float x, float y, ClimbHold::Kind kind) const {
    ClimbHold h;
    h.kind = kind;
    const uint32_t r = hash3(static_cast<int>(x * 97.0f), static_cast<int>(y * 89.0f), m_desc.seed);
    const float jitter = 0.85f + 0.3f * static_cast<float>(r & 1023u) / 1023.0f;
    h.size = (kind == ClimbHold::Kind::Jug ? 0.13f : kind == ClimbHold::Kind::Crimp ? 0.09f : 0.15f) * jitter;
    h.normal = surfaceNormal(x, y);
    h.position = glm::vec3(x, y, surfaceZ(x, y)) + h.normal * (holdDepth(kind, h.size) * 0.8f);
    return h;
}

void ClimbWall::placeHolds() {
    const ClimbWallDesc& d = m_desc;
    Rng rng(d.seed * 131u + 7u);
    m_holds.clear();
    const float xMin = -d.width * 0.5f + d.margin, xMax = d.width * 0.5f - d.margin;
    auto pickKind = [&](float y, bool onRoute) {
        const float lean = leanAt(y);
        float jug = lean > 5.0f ? 0.5f : lean < -5.0f ? 0.25f : 0.35f;
        float crimp = lean < -5.0f ? 0.35f : 0.4f;
        if (onRoute) jug += 0.1f;
        jug = std::max(0.02f, jug + d.jugBias);
        crimp = std::max(0.02f, crimp + d.crimpBias);
        const float p = rng.unit() * (jug + crimp + 0.25f);
        return p < jug ? ClimbHold::Kind::Jug : p < jug + crimp ? ClimbHold::Kind::Crimp : ClimbHold::Kind::Sloper;
    };

    // Ledge and summit edges first: the route may pass through them.
    for (size_t i = 0; i < m_ledges.size(); ++i) {
        const ClimbLedge& l = m_ledges[i];
        for (float x = l.center.x - l.halfExtents.x + 0.25f; x <= l.center.x + l.halfExtents.x - 0.25f + 1e-3f; x += 0.45f) {
            ClimbHold h;
            h.kind = ClimbHold::Kind::Edge;
            h.size = 0.2f;
            h.ledge = static_cast<int>(i);
            h.position = glm::vec3(x, l.top(), l.center.z + l.halfExtents.z - 0.04f);
            m_holds.push_back(h);
        }
    }
    for (float x = xMin; x <= xMax + 1e-3f; x += 0.45f) {
        ClimbHold h;
        h.kind = ClimbHold::Kind::Edge;
        h.size = 0.2f;
        h.position = glm::vec3(x, d.height, surfaceZ(x, d.height) - 0.03f);
        h.normal = glm::vec3(0, 0, 1);
        m_holds.push_back(h);
    }

    // The guaranteed route: a zigzag of steps no longer than routeStep
    // from the ground to the summit. Where it runs into a ledge it uses
    // the ledge's edge (already there) and carries on above it.
    auto tooClose = [&](float x, float y, float spacing) {
        for (const ClimbHold& h : m_holds)
            if (h.kind != ClimbHold::Kind::Edge && std::abs(h.position.x - x) < spacing && std::abs(h.position.y - y) < spacing &&
                glm::length(glm::vec2(h.position.x - x, h.position.y - y)) < spacing)
                return true;
        return false;
    };
    float x = rng.range(xMin + 1.0f, xMax - 1.0f), y = 1.2f;
    float side = rng.unit() < 0.5f ? -1.0f : 1.0f;
    m_line.clear();
    auto addRoute = [&](const ClimbHold& h) {
        m_holds.push_back(h);
        m_holds.back().route = true;
        m_line.push_back(static_cast<int>(m_holds.size()) - 1);
    };
    // The nearest edge hold of ledge `li` (-1 = the summit) to x.
    auto edgeAt = [&](int li, float ex) {
        int best = -1;
        for (size_t i = 0; i < m_holds.size(); ++i)
            if (m_holds[i].kind == ClimbHold::Kind::Edge && m_holds[i].ledge == li &&
                (best < 0 || std::abs(m_holds[i].position.x - ex) < std::abs(m_holds[static_cast<size_t>(best)].position.x - ex)))
                best = static_cast<int>(i);
        return best;
    };
    addRoute(makeHold(x, y, ClimbHold::Kind::Jug));
    while (y < d.height - 0.9f) {
        float dy = rng.range(0.45f, 0.85f);
        const float dxMax = std::sqrt(std::max(0.0f, d.routeStep * d.routeStep - dy * dy));
        float dx = side * rng.range(0.15f, 0.8f) * dxMax;
        side = -side;
        // The line goes by every ledge (they're the rests): drift toward
        // the next one's span over the last few metres below it.
        bool steering = false;
        for (const ClimbLedge& l : m_ledges) {
            const float below = l.top() - y;
            if (below <= 0.0f || below > 8.0f) continue;
            const float lo = l.center.x - l.halfExtents.x + 0.4f, hi = l.center.x + l.halfExtents.x - 0.4f;
            if (x < lo) dx = std::min(std::abs(dx) + 0.3f * dxMax, lo - x + 0.3f);
            else if (x > hi) dx = -std::min(std::abs(dx) + 0.3f * dxMax, x - hi + 0.3f);
            else break;
            dx = std::clamp(dx, -dxMax, dxMax);
            steering = true;
            break;
        }
        if (!steering && (x + dx < xMin || x + dx > xMax)) dx = -dx;
        const float nx = std::clamp(x + dx, xMin, xMax), ny = y + dy;
        // Landing in a ledge's band, or stepping past it: take the ledge.
        auto crosses = [&](const ClimbLedge& l) {
            return std::abs(nx - l.center.x) < l.halfExtents.x + 0.3f && ny > l.top() - 0.2f && y < l.top() + 0.35f && y < l.top() - 0.01f;
        };
        bool ledge = false;
        for (const ClimbLedge& l : m_ledges) ledge = ledge || crosses(l);
        if (ledge) {
            // Up to the ledge's lip: a hold just under it (when the last
            // one is further down), then the edge, then on from its top.
            for (size_t li = 0; li < m_ledges.size(); ++li) {
                const ClimbLedge& l = m_ledges[li];
                if (crosses(l)) {
                    x = std::clamp(nx, l.center.x - l.halfExtents.x + 0.25f, l.center.x + l.halfExtents.x - 0.25f);
                    if (y < l.top() - 0.45f) addRoute(makeHold(x, l.top() - 0.3f, ClimbHold::Kind::Jug));
                    y = l.top();
                    const int e = edgeAt(static_cast<int>(li), x);
                    if (e >= 0) m_line.push_back(e);
                }
            }
            continue;
        }
        x = nx;
        y = ny;
        if (y >= d.height - 0.3f) break;
        ClimbHold h = makeHold(x, y, pickKind(y, true));
        if (tooClose(h.position.x, h.position.y, d.spacing * 0.6f)) {
            // A hold already here does the job.
            const int near = nearestHold(h.position, d.spacing);
            if (near >= 0 && m_line.back() != near) m_line.push_back(near);
            continue;
        }
        addRoute(h);
    }
    if (const int top = edgeAt(-1, x); top >= 0) m_line.push_back(top);

    // Standing on a ledge you can always get going again: jugs at arm's
    // height above it.
    for (const ClimbLedge& l : m_ledges)
        for (float f : { -0.5f, 0.0f, 0.5f }) {
            const float hx = l.center.x + f * (l.halfExtents.x - 0.3f), hy = l.top() + 1.75f;
            ClimbHold h = makeHold(hx, hy, ClimbHold::Kind::Jug);
            if (!tooClose(h.position.x, h.position.y, d.spacing)) m_holds.push_back(h);
        }

    // Everything else: random holds, kept apart.
    const float area = (xMax - xMin) * d.height;
    const int attempts = static_cast<int>(area * d.density * 1.6f);
    for (int i = 0; i < attempts; ++i) {
        const float hx = rng.range(xMin, xMax), hy = rng.range(0.5f, d.height - 0.45f);
        const ClimbHold::Kind kind = pickKind(hy, false);
        const bool loose = rng.unit() < d.looseChance;
        if (blockedByLedge(hx, hy)) continue;
        ClimbHold h = makeHold(hx, hy, kind);
        if (tooClose(h.position.x, h.position.y, d.spacing)) continue;
        h.loose = loose;
        m_holds.push_back(h);
    }
}

int ClimbWall::nearestHold(const glm::vec3& p, float maxDist, int skip) const {
    int best = -1;
    float bestD = maxDist * maxDist;
    for (size_t i = 0; i < m_holds.size(); ++i) {
        if (static_cast<int>(i) == skip) continue;
        const glm::vec3 d = m_holds[i].position - p;
        const float dd = glm::dot(d, d);
        if (dd <= bestD) {
            bestD = dd;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void ClimbWall::holdsNear(const glm::vec3& p, float radius, std::vector<int>& out) const {
    out.clear();
    const float r2 = radius * radius;
    for (size_t i = 0; i < m_holds.size(); ++i) {
        const glm::vec3 d = m_holds[i].position - p;
        if (glm::dot(d, d) <= r2) out.push_back(static_cast<int>(i));
    }
}

std::vector<int> ClimbWall::route(float span) const {
    const size_t n = m_holds.size();
    std::vector<int> prev(n, -2);
    std::queue<int> open;
    for (size_t i = 0; i < n; ++i)
        if (!m_holds[i].loose && m_holds[i].position.y <= 2.2f) {
            prev[i] = -1;
            open.push(static_cast<int>(i));
        }
    while (!open.empty()) {
        const int a = open.front();
        open.pop();
        const ClimbHold& ha = m_holds[static_cast<size_t>(a)];
        if (ha.kind == ClimbHold::Kind::Edge && ha.ledge < 0) {
            std::vector<int> path;
            for (int k = a; k >= 0; k = prev[static_cast<size_t>(k)]) path.push_back(k);
            std::reverse(path.begin(), path.end());
            return path;
        }
        for (size_t b = 0; b < n; ++b) {
            if (prev[b] != -2 || m_holds[b].loose) continue;
            if (reachDistance(m_holds[b].position, ha.position) <= span) {
                prev[b] = a;
                open.push(static_cast<int>(b));
            }
        }
    }
    return {};
}

float ClimbWall::reachDistance(const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 d = b - a;
    return std::sqrt(d.x * d.x + d.y * d.y + 0.25f * d.z * d.z);
}

bool ClimbWall::routeExists(float span) const { return !route(span).empty(); }

void ClimbWall::appendHold(const ClimbHold& hold, uint32_t seed, const glm::vec3& origin, ClimbMesh& mesh) {
    if (hold.kind == ClimbHold::Kind::Edge) return;
    const glm::vec3 n = glm::normalize(hold.normal);
    glm::vec3 up = glm::vec3(0, 1, 0) - n * n.y;
    up = glm::length(up) < 1e-4f ? glm::vec3(0, 0, 1) : glm::normalize(up);
    const glm::vec3 right = glm::cross(up, n);
    const float depth = holdDepth(hold.kind, hold.size);
    const glm::vec3 base = hold.position - origin - n * (depth * 0.8f + 0.03f);
    // A ring on (a little into) the rock and an apex out from it. Jugs
    // are deep with the apex low (a lip to pull on), crimps wide and
    // flat, slopers a round dome.
    const float wide = hold.kind == ClimbHold::Kind::Crimp ? 1.6f : 1.0f;
    const float apexDrop = hold.kind == ClimbHold::Kind::Jug ? -0.35f : hold.kind == ClimbHold::Kind::Crimp ? 0.1f : 0.0f;
    constexpr int kRing = 7;
    glm::vec3 ring[kRing];
    const uint32_t h0 = hash3(static_cast<int>(hold.position.x * 131.0f), static_cast<int>(hold.position.y * 137.0f), seed);
    for (int i = 0; i < kRing; ++i) {
        const float a = 6.2831853f * (static_cast<float>(i) + 0.3f * static_cast<float>((h0 >> (i * 3)) & 3u) / 3.0f) / kRing;
        const float r = hold.size * (0.8f + 0.4f * static_cast<float>(hash3(i, 7, h0) & 255u) / 255.0f);
        ring[i] = base + right * (std::cos(a) * r * wide) + up * (std::sin(a) * r);
    }
    const glm::vec3 apex = base + n * (depth + 0.03f) + up * (hold.size * apexDrop);
    // A second, smaller ring half-way out rounds slopers and jugs.
    glm::vec3 mid[kRing];
    for (int i = 0; i < kRing; ++i) mid[i] = base + (ring[i] - base) * 0.7f + n * ((depth + 0.03f) * 0.65f) + up * (hold.size * apexDrop * 0.5f);
    glm::vec3 color = holdColor(hold.kind);
    if (hold.loose) color = color * 0.55f + glm::vec3(0.42f, 0.38f, 0.33f) * 0.45f; // dull: a careful eye sees it
    for (int i = 0; i < kRing; ++i) {
        const int j = (i + 1) % kRing;
        addQuad(mesh, ring[i], ring[j], mid[j], mid[i], n + (ring[i] - base), color);
        // Outward: wound so the apex cap faces away from the rock.
        if (glm::dot(glm::cross(mid[j] - mid[i], apex - mid[i]), n) >= 0.0f) addTriangle(mesh, mid[i], mid[j], apex, color);
        else addTriangle(mesh, mid[j], mid[i], apex, color);
    }
}

ClimbMesh ClimbWall::buildMesh() const {
    ClimbMesh m;
    const ClimbWallDesc& d = m_desc;
    const uint32_t seed = d.seed * 3u + 11u;
    auto point = [&](int r, int c) {
        return glm::vec3(-d.width * 0.5f + static_cast<float>(c) * d.cell, std::min(static_cast<float>(r) * d.cell, d.height),
                         m_z[static_cast<size_t>(r * m_cols + c)]);
    };
    auto rock = [&](const glm::vec3& p, float lean) {
        const float v = fbm(p.x * 0.35f, p.y * 0.35f, seed, 2) * 0.5f + 0.5f;
        glm::vec3 c = glm::mix(glm::vec3(0.36f, 0.33f, 0.3f), glm::vec3(0.55f, 0.5f, 0.44f), v);
        if (lean > 5.0f) c *= 0.85f; // overhangs: darker, weathered less
        const float band = 0.5f + 0.5f * std::sin(p.y * 1.7f + v * 3.0f);
        return c * (0.92f + 0.08f * band);
    };
    // The face.
    for (int r = 0; r + 1 < m_rows; ++r)
        for (int c = 0; c + 1 < m_cols; ++c) {
            const glm::vec3 a = point(r, c), b = point(r, c + 1), cc = point(r + 1, c + 1), dd = point(r + 1, c);
            addQuad(m, a, b, cc, dd, glm::vec3(0, 0, 1), rock((a + cc) * 0.5f, m_lean[static_cast<size_t>(r)]));
        }
    // Side skirts back into the mountain, and the summit's top.
    float zBack = 1e9f;
    for (float z : m_z) zBack = std::min(zBack, z);
    zBack -= 6.0f;
    const glm::vec3 side(0.38f, 0.35f, 0.32f);
    for (int r = 0; r + 1 < m_rows; ++r) {
        const glm::vec3 l0 = point(r, 0), l1 = point(r + 1, 0);
        addQuad(m, l0, l1, glm::vec3(l1.x, l1.y, zBack), glm::vec3(l0.x, l0.y, zBack), glm::vec3(-1, 0, 0), side);
        const glm::vec3 r0 = point(r, m_cols - 1), r1 = point(r + 1, m_cols - 1);
        addQuad(m, r0, r1, glm::vec3(r1.x, r1.y, zBack), glm::vec3(r0.x, r0.y, zBack), glm::vec3(1, 0, 0), side);
    }
    const glm::vec3 grass(0.34f, 0.46f, 0.25f);
    for (int c = 0; c + 1 < m_cols; ++c) {
        const glm::vec3 a = point(m_rows - 1, c), b = point(m_rows - 1, c + 1);
        addQuad(m, a, b, glm::vec3(b.x, d.height, zBack), glm::vec3(a.x, d.height, zBack), glm::vec3(0, 1, 0), grass);
    }
    for (const ClimbHold& h : m_holds)
        if (!h.loose) appendHold(h, d.seed, glm::vec3(0.0f), m);
    return m;
}

} // namespace kke
