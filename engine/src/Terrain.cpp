#include "kke/Terrain.h"

#include <stb_image.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <numeric>
#include <utility>

namespace kke {

namespace {

// splitmix64: a seed to a stream of well-mixed numbers.
uint64_t mix(uint64_t& s) {
    uint64_t z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// 2D gradient noise (Perlin's improved fade) on a seeded permutation.
// Plain float arithmetic only, so every machine makes the same land.
class GradientNoise {
public:
    explicit GradientNoise(uint32_t seed) {
        std::iota(m_perm.begin(), m_perm.begin() + 256, 0);
        uint64_t s = seed;
        for (int i = 255; i > 0; --i) std::swap(m_perm[size_t(i)], m_perm[size_t(mix(s) % uint64_t(i + 1))]);
        for (int i = 0; i < 256; ++i) m_perm[size_t(256 + i)] = m_perm[size_t(i)];
    }
    // About -1..1.
    float operator()(float x, float y) const {
        const float fx = std::floor(x), fy = std::floor(y);
        const int xi = int(fx) & 255, yi = int(fy) & 255;
        const float u = x - fx, v = y - fy;
        auto fade = [](float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); };
        auto grad = [this](int h, float dx, float dy) {
            // Eight directions round the circle.
            static constexpr float g[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {0.7071f, 0.7071f}, {-0.7071f, 0.7071f}, {0.7071f, -0.7071f}, {-0.7071f, -0.7071f}};
            const float* d = g[m_perm[size_t(h)] & 7];
            return d[0] * dx + d[1] * dy;
        };
        const int a = m_perm[size_t(xi)] + yi, b = m_perm[size_t(xi + 1)] + yi;
        const float n00 = grad(a, u, v), n10 = grad(b, u - 1.0f, v);
        const float n01 = grad(a + 1, u, v - 1.0f), n11 = grad(b + 1, u - 1.0f, v - 1.0f);
        const float su = fade(u), sv = fade(v);
        const float nx0 = n00 + su * (n10 - n00), nx1 = n01 + su * (n11 - n01);
        return (nx0 + sv * (nx1 - nx0)) * 1.4142f;
    }

private:
    std::array<int, 512> m_perm{};
};

// A small steady variation per sample, so a big field of one colour
// doesn't read as flat plastic.
float speckle(int x, int z) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(z) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return float((h ^ (h >> 16)) & 1023u) / 1023.0f;
}

} // namespace

void Terrain::Rect::add(const Rect& o) {
    if (o.empty()) return;
    if (empty()) {
        *this = o;
        return;
    }
    x0 = std::min(x0, o.x0);
    z0 = std::min(z0, o.z0);
    x1 = std::max(x1, o.x1);
    z1 = std::max(z1, o.z1);
}

Terrain::Terrain(float size, int samples, const glm::vec3& centre)
    : m_n(std::clamp(samples, kMinSamples, kMaxSamples)), m_size(std::max(1.0f, size)) {
    m_corner = centre - glm::vec3(m_size * 0.5f, 0.0f, m_size * 0.5f);
    m_corner.y = centre.y;
    m_heights.assign(size_t(m_n) * size_t(m_n), 0.0f);
    m_weights.assign(m_heights.size(), {255, 0, 0, 0});
    layers[0] = {"grass", {0.20f, 0.32f, 0.10f}};
    layers[1] = {"rock", {0.30f, 0.28f, 0.26f}};
    layers[2] = {"dirt", {0.33f, 0.24f, 0.15f}};
    layers[3] = {"snow", {0.85f, 0.87f, 0.90f}};
}

float Terrain::sample(int x, int z) const {
    x = std::clamp(x, 0, m_n - 1);
    z = std::clamp(z, 0, m_n - 1);
    return m_heights[size_t(z) * size_t(m_n) + size_t(x)];
}

void Terrain::setSample(int x, int z, float h) {
    if (x < 0 || z < 0 || x >= m_n || z >= m_n || !std::isfinite(h)) return;
    m_heights[size_t(z) * size_t(m_n) + size_t(x)] = h;
    ++m_version;
}

bool Terrain::contains(float worldX, float worldZ) const {
    return worldX >= m_corner.x && worldZ >= m_corner.z && worldX <= m_corner.x + m_size && worldZ <= m_corner.z + m_size;
}

float Terrain::heightAt(float worldX, float worldZ) const {
    const float s = spacing();
    const float fx = std::clamp((worldX - m_corner.x) / s, 0.0f, float(m_n - 1));
    const float fz = std::clamp((worldZ - m_corner.z) / s, 0.0f, float(m_n - 1));
    const int x = std::min(int(fx), m_n - 2), z = std::min(int(fz), m_n - 2);
    const float u = fx - float(x), v = fz - float(z);
    const float h00 = sample(x, z), h10 = sample(x + 1, z), h01 = sample(x, z + 1), h11 = sample(x + 1, z + 1);
    // The cell's two triangles share the diagonal (x, z)-(x+1, z+1), as
    // Jolt's HeightFieldShape splits them.
    const float h = v >= u ? h00 + v * (h01 - h00) + u * (h11 - h01) : h00 + u * (h10 - h00) + v * (h11 - h10);
    return m_corner.y + h;
}

glm::vec3 Terrain::normalAt(float worldX, float worldZ) const {
    const float s = spacing();
    const float d = s;
    const float hl = heightAt(worldX - d, worldZ), hr = heightAt(worldX + d, worldZ);
    const float hd = heightAt(worldX, worldZ - d), hu = heightAt(worldX, worldZ + d);
    return glm::normalize(glm::vec3(hl - hr, 2.0f * d, hd - hu));
}

float Terrain::slopeAt(float worldX, float worldZ) const {
    return glm::degrees(std::acos(std::clamp(normalAt(worldX, worldZ).y, -1.0f, 1.0f)));
}

void Terrain::generate(const Noise& noise) {
    const GradientNoise gn(noise.seed);
    const float feature = noise.feature > 0.0f ? noise.feature : m_size * 0.25f;
    const int octaves = std::clamp(noise.octaves, 1, 10);
    const float rough = std::clamp(noise.roughness, 0.05f, 0.95f);
    const float s = spacing();
    float lo = std::numeric_limits<float>::max(), hi = -lo;
    for (int z = 0; z < m_n; ++z)
        for (int x = 0; x < m_n; ++x) {
            float freq = 1.0f / feature, amp = 1.0f, sum = 0.0f;
            for (int o = 0; o < octaves; ++o) {
                // Each octave offset so their zero lines don't stack up at the origin.
                const float n = gn(float(x) * s * freq + 17.3f * float(o), float(z) * s * freq - 9.1f * float(o));
                sum += amp * (noise.ridged ? 1.0f - std::abs(n) : n);
                freq *= 2.0f;
                amp *= rough;
            }
            m_heights[size_t(z) * size_t(m_n) + size_t(x)] = sum;
            lo = std::min(lo, sum);
            hi = std::max(hi, sum);
        }
    const float scale = hi > lo ? std::max(0.0f, noise.height) / (hi - lo) : 0.0f;
    for (float& h : m_heights) h = (h - lo) * scale;
    ++m_version;
}

bool Terrain::loadImage(const std::filesystem::path& file, float height, std::string* error) {
    int w = 0, h = 0, channels = 0;
    std::unique_ptr<stbi_us, void (*)(void*)> px(stbi_load_16(file.string().c_str(), &w, &h, &channels, 1), stbi_image_free);
    if (!px || w < 2 || h < 2) {
        if (error) *error = "can't read " + file.string() + (stbi_failure_reason() ? std::string(": ") + stbi_failure_reason() : std::string());
        return false;
    }
    for (int z = 0; z < m_n; ++z)
        for (int x = 0; x < m_n; ++x) {
            // Bilinear, corner to corner.
            const float fx = float(x) / float(m_n - 1) * float(w - 1), fz = float(z) / float(m_n - 1) * float(h - 1);
            const int ix = std::min(int(fx), w - 2), iz = std::min(int(fz), h - 2);
            const float u = fx - float(ix), v = fz - float(iz);
            auto at = [&](int a, int b) { return float(px.get()[size_t(b) * size_t(w) + size_t(a)]) / 65535.0f; };
            const float top = at(ix, iz) + u * (at(ix + 1, iz) - at(ix, iz));
            const float bottom = at(ix, iz + 1) + u * (at(ix + 1, iz + 1) - at(ix, iz + 1));
            m_heights[size_t(z) * size_t(m_n) + size_t(x)] = (top + v * (bottom - top)) * height;
        }
    ++m_version;
    return true;
}

void Terrain::fill(float h) {
    std::fill(m_heights.begin(), m_heights.end(), std::isfinite(h) ? h : 0.0f);
    ++m_version;
}

Terrain::Rect Terrain::brushRect(const glm::vec3& at, float radius) const {
    const float s = spacing();
    Rect r;
    r.x0 = std::max(0, int(std::ceil((at.x - radius - m_corner.x) / s)));
    r.z0 = std::max(0, int(std::ceil((at.z - radius - m_corner.z) / s)));
    r.x1 = std::min(m_n - 1, int(std::floor((at.x + radius - m_corner.x) / s)));
    r.z1 = std::min(m_n - 1, int(std::floor((at.z + radius - m_corner.z) / s)));
    return r;
}

float Terrain::falloff(float dx, float dz, float radius) const {
    const float t = (dx * dx + dz * dz) / (radius * radius);
    if (t >= 1.0f) return 0.0f;
    const float k = 1.0f - t;
    return k * k; // smooth to nothing at the rim
}

Terrain::Rect Terrain::raise(const glm::vec3& at, float radius, float metres) {
    if (!(radius > 0.0f) || !std::isfinite(metres)) return {};
    const Rect r = brushRect(at, radius);
    const float s = spacing();
    for (int z = r.z0; z <= r.z1; ++z)
        for (int x = r.x0; x <= r.x1; ++x) {
            const float w = falloff(m_corner.x + float(x) * s - at.x, m_corner.z + float(z) * s - at.z, radius);
            m_heights[size_t(z) * size_t(m_n) + size_t(x)] += metres * w;
        }
    if (!r.empty()) ++m_version;
    return r;
}

Terrain::Rect Terrain::smooth(const glm::vec3& at, float radius, float strength) {
    if (!(radius > 0.0f)) return {};
    Rect r = brushRect(at, radius);
    if (r.empty()) return r;
    strength = std::clamp(strength, 0.0f, 1.0f);
    const float s = spacing();
    const std::vector<float> before = m_heights;
    auto old = [&](int x, int z) {
        x = std::clamp(x, 0, m_n - 1);
        z = std::clamp(z, 0, m_n - 1);
        return before[size_t(z) * size_t(m_n) + size_t(x)];
    };
    for (int z = r.z0; z <= r.z1; ++z)
        for (int x = r.x0; x <= r.x1; ++x) {
            const float w = falloff(m_corner.x + float(x) * s - at.x, m_corner.z + float(z) * s - at.z, radius) * strength;
            float sum = 0.0f;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx) sum += old(x + dx, z + dz);
            float& h = m_heights[size_t(z) * size_t(m_n) + size_t(x)];
            h += (sum / 9.0f - h) * w;
        }
    ++m_version;
    return r;
}

Terrain::Rect Terrain::flatten(const glm::vec3& at, float radius, float worldHeight, float strength) {
    if (!(radius > 0.0f) || !std::isfinite(worldHeight)) return {};
    const Rect r = brushRect(at, radius);
    strength = std::clamp(strength, 0.0f, 1.0f);
    const float target = worldHeight - m_corner.y, s = spacing();
    for (int z = r.z0; z <= r.z1; ++z)
        for (int x = r.x0; x <= r.x1; ++x) {
            // A flat top out to most of the radius, so a building fits.
            const float d = std::sqrt(glm::dot(glm::vec2(m_corner.x + float(x) * s - at.x, m_corner.z + float(z) * s - at.z),
                                               glm::vec2(m_corner.x + float(x) * s - at.x, m_corner.z + float(z) * s - at.z))) / radius;
            const float w = (d <= 0.6f ? 1.0f : d >= 1.0f ? 0.0f : 1.0f - (d - 0.6f) / 0.4f) * strength;
            float& h = m_heights[size_t(z) * size_t(m_n) + size_t(x)];
            h += (target - h) * w;
        }
    if (!r.empty()) ++m_version;
    return r;
}

Terrain::Rect Terrain::paint(const glm::vec3& at, float radius, int layer, float strength) {
    if (!(radius > 0.0f) || layer < 0 || layer >= kLayers) return {};
    const Rect r = brushRect(at, radius);
    strength = std::clamp(strength, 0.0f, 1.0f);
    const float s = spacing();
    for (int z = r.z0; z <= r.z1; ++z)
        for (int x = r.x0; x <= r.x1; ++x) {
            const float w = falloff(m_corner.x + float(x) * s - at.x, m_corner.z + float(z) * s - at.z, radius) * strength;
            if (w <= 0.0f) continue;
            std::array<uint8_t, kLayers>& wt = m_weights[size_t(z) * size_t(m_n) + size_t(x)];
            // Move w of the way towards all-`layer`, keeping the sum at 255.
            std::array<float, kLayers> f{};
            for (int i = 0; i < kLayers; ++i) f[size_t(i)] = float(wt[size_t(i)]) * (1.0f - w) + (i == layer ? 255.0f * w : 0.0f);
            int total = 0;
            for (int i = 0; i < kLayers; ++i) {
                wt[size_t(i)] = uint8_t(std::lround(std::clamp(f[size_t(i)], 0.0f, 255.0f)));
                total += wt[size_t(i)];
            }
            wt[size_t(layer)] = uint8_t(std::clamp(int(wt[size_t(layer)]) + 255 - total, 0, 255)); // rounding left over
        }
    if (!r.empty()) ++m_paintVersion;
    return r;
}

std::array<uint8_t, Terrain::kLayers> Terrain::weights(int x, int z) const {
    x = std::clamp(x, 0, m_n - 1);
    z = std::clamp(z, 0, m_n - 1);
    return m_weights[size_t(z) * size_t(m_n) + size_t(x)];
}

glm::vec3 Terrain::colorAt(int x, int z) const {
    const std::array<uint8_t, kLayers> w = weights(x, z);
    glm::vec3 c(0.0f);
    for (int i = 0; i < kLayers; ++i) c += layers[size_t(i)].color * (float(w[size_t(i)]) / 255.0f);
    if (autoRock) {
        const float s = spacing();
        const float slope = slopeAt(m_corner.x + float(x) * s, m_corner.z + float(z) * s);
        const float k = std::clamp((slope - (rockSlope - 6.0f)) / 12.0f, 0.0f, 1.0f); // eases in over 12 degrees
        c = glm::mix(c, layers[1].color, k);
    }
    return c * (0.92f + 0.16f * speckle(x, z));
}

void Terrain::buildMesh(int x0, int z0, int cellsX, int cellsZ, int step, float skirt, std::vector<Vertex>& vertices,
                        std::vector<uint32_t>& indices) const {
    x0 = std::clamp(x0, 0, m_n - 1);
    z0 = std::clamp(z0, 0, m_n - 1);
    cellsX = std::clamp(cellsX, 0, m_n - 1 - x0);
    cellsZ = std::clamp(cellsZ, 0, m_n - 1 - z0);
    if (cellsX == 0 || cellsZ == 0) return;
    step = std::max(1, step);
    while (step > 1 && (cellsX % step || cellsZ % step)) step /= 2;
    const int nx = cellsX / step + 1, nz = cellsZ / step + 1;
    const float s = spacing();
    const uint32_t base = uint32_t(vertices.size());
    auto vertexAt = [&](int x, int z) {
        Vertex v;
        v.position = m_corner + glm::vec3(float(x) * s, sample(x, z), float(z) * s);
        // Normals from the full-detail samples, so distant chunks shade like near ones.
        v.normal = glm::normalize(glm::vec3(sample(x - 1, z) - sample(x + 1, z), 2.0f * s, sample(x, z - 1) - sample(x, z + 1)));
        v.color = colorAt(x, z);
        v.uv = glm::vec2(0.0f); // DynamicMeshRenderer: uv.x is glow
        return v;
    };
    for (int j = 0; j < nz; ++j)
        for (int i = 0; i < nx; ++i) vertices.push_back(vertexAt(x0 + i * step, z0 + j * step));
    for (int j = 0; j + 1 < nz; ++j)
        for (int i = 0; i + 1 < nx; ++i) {
            const uint32_t a = base + uint32_t(j * nx + i), b = a + 1, c = a + uint32_t(nx), d = c + 1;
            // (x,z) (x,z+1) (x+1,z+1) and (x,z) (x+1,z+1) (x+1,z): the collision's split, facing up.
            indices.insert(indices.end(), {a, c, d, a, d, b});
        }
    if (skirt <= 0.0f) return;
    // A curtain hanging from each edge, facing out.
    auto curtain = [&](int count, auto edgeIndex, bool flip) {
        const uint32_t first = uint32_t(vertices.size());
        for (int k = 0; k < count; ++k) {
            Vertex v = vertices[edgeIndex(k)];
            v.position.y -= skirt;
            vertices.push_back(v);
        }
        for (int k = 0; k + 1 < count; ++k) {
            const uint32_t top0 = edgeIndex(k), top1 = edgeIndex(k + 1), bot0 = first + uint32_t(k), bot1 = bot0 + 1;
            if (flip) indices.insert(indices.end(), {top0, top1, bot1, top0, bot1, bot0});
            else indices.insert(indices.end(), {top0, bot1, top1, top0, bot0, bot1});
        }
    };
    curtain(nx, [&](int k) { return base + uint32_t(k); }, true);                                     // z0 edge, faces -z
    curtain(nx, [&](int k) { return base + uint32_t((nz - 1) * nx + k); }, false);                      // far z edge, faces +z
    curtain(nz, [&](int k) { return base + uint32_t(k * nx); }, false);                                 // x0 edge, faces -x
    curtain(nz, [&](int k) { return base + uint32_t(k * nx + nx - 1); }, true);                       // far x edge, faces +x
}

glm::mat4 Terrain::Placement::transform() const {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
    const glm::vec3 u = glm::normalize(up);
    const float c = glm::clamp(u.y, -1.0f, 1.0f);
    if (c < 0.9999f) {
        // Lean from straight up to `up`.
        const glm::vec3 axis = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), u));
        m = glm::rotate(m, std::acos(c), axis);
    }
    m = glm::rotate(m, glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
    return glm::scale(m, glm::vec3(scale));
}

std::vector<Terrain::Placement> Terrain::scatter(const ScatterRule& rule) const {
    std::vector<Placement> out;
    if (!(rule.density > 0.0f)) return out;
    const float cell = std::max(0.25f, std::sqrt(100.0f / rule.density));
    const int cells = int(std::ceil(m_size / cell));
    const float s = spacing();
    const int layer = rule.layer >= 0 && rule.layer < kLayers ? rule.layer : -1;
    for (int j = 0; j < cells; ++j)
        for (int i = 0; i < cells; ++i) {
            if (out.size() >= rule.maxCount) return out;
            // One point per cell, anywhere in it: random but steady (a hash of seed and cell).
            uint64_t h = (uint64_t(rule.seed) << 32) ^ (uint64_t(uint32_t(i)) * 0x9E3779B1ull) ^ (uint64_t(uint32_t(j)) * 0x85EBCA77ull << 16);
            auto next = [&h]() { return float(mix(h) >> 40) / float(1ull << 24); }; // 0..1
            const float wx = m_corner.x + (float(i) + next()) * cell, wz = m_corner.z + (float(j) + next()) * cell;
            const float yaw = next() * 360.0f, sc = rule.minScale + (rule.maxScale - rule.minScale) * next(), keep = next();
            if (!contains(wx, wz)) continue;
            if (rule.radius > 0.0f && glm::length(glm::vec2(wx, wz) - rule.centre) > rule.radius) continue;
            bool blocked = false;
            for (const glm::vec3& c : rule.keepClear) blocked = blocked || glm::length(glm::vec2(wx - c.x, wz - c.y)) < c.z;
            if (blocked) continue;
            const float y = heightAt(wx, wz);
            if (y < rule.minHeight || y > rule.maxHeight) continue;
            if (slopeAt(wx, wz) > rule.maxSlope) continue;
            if (layer >= 0) {
                const int sx = int(std::lround((wx - m_corner.x) / s)), sz = int(std::lround((wz - m_corner.z) / s));
                const float w = float(weights(sx, sz)[size_t(layer)]) / 255.0f;
                if (w < rule.minWeight) continue;
                if (keep > (w - rule.minWeight) / std::max(1e-3f, 1.0f - rule.minWeight) + 0.5f) continue; // thinner at the layer's edge
            }
            Placement p;
            p.position = {wx, y, wz};
            p.yawDegrees = yaw;
            p.scale = sc;
            if (rule.alignToSlope) p.up = normalAt(wx, wz);
            out.push_back(p);
        }
    return out;
}

TerrainChunks::TerrainChunks(const Terrain& terrain, int cells, float lodDistance)
    : m_terrain(terrain), m_cells(std::max(4, cells)), m_lodDistance(std::max(1.0f, lodDistance)) {
    const int last = terrain.samples() - 1;
    for (int z = 0; z < last; z += m_cells)
        for (int x = 0; x < last; x += m_cells) m_chunks.push_back({x, z});
}

void TerrainChunks::touch(const Terrain::Rect& r) {
    if (r.empty()) return;
    for (Chunk& c : m_chunks) {
        // A chunk's normals read one sample past its edge, so widen by one.
        if (r.x1 + 1 < c.x0 || r.x0 - 1 > c.x0 + m_cells || r.z1 + 1 < c.z0 || r.z0 - 1 > c.z0 + m_cells) continue;
        c.dirty = true;
    }
}

void TerrainChunks::touchAll() {
    for (Chunk& c : m_chunks) c.dirty = true;
}

int TerrainChunks::stepFor(const Chunk& c, const glm::vec3& camera) const {
    const float s = m_terrain.spacing();
    const glm::vec3 lo = m_terrain.corner() + glm::vec3(float(c.x0) * s, 0.0f, float(c.z0) * s);
    const glm::vec2 hi = glm::vec2(lo.x, lo.z) + glm::vec2(float(m_cells) * s);
    // Distance from the camera to the chunk's square (flat; height barely matters).
    const glm::vec2 p(camera.x, camera.z);
    const glm::vec2 d = glm::max(glm::max(glm::vec2(lo.x, lo.z) - p, p - hi), glm::vec2(0.0f));
    const float dist = glm::length(d);
    int step = 1;
    while (step < 8 && step * 2 <= m_cells && dist > m_lodDistance * float(step)) step *= 2;
    return step;
}

std::vector<size_t> TerrainChunks::update(const glm::vec3& camera, size_t budget) {
    std::vector<std::pair<int, size_t>> want; // (step wanted, chunk)
    for (size_t i = 0; i < m_chunks.size(); ++i) {
        const Chunk& c = m_chunks[i];
        const int step = stepFor(c, camera);
        if (c.dirty || c.step != step) want.push_back({step, i});
    }
    // Finest wanted first: they are the nearest.
    std::sort(want.begin(), want.end());
    std::vector<size_t> out;
    for (size_t k = 0; k < want.size() && out.size() < budget; ++k) out.push_back(want[k].second);
    return out;
}

void TerrainChunks::built(size_t i, int step) {
    if (i >= m_chunks.size()) return;
    m_chunks[i].step = step;
    m_chunks[i].dirty = false;
}

} // namespace kke
