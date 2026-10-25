#pragma once

#include "kke/Mesh.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace kke {

// Heightmap terrain (ENG-F75/F76/F77, docs/TERRAIN.md): a square grid of
// heights you can generate from a seed, load from a greyscale image,
// sculpt (raise, lower, smooth, flatten) and paint with up to four ground
// layers. It knows nothing about the GPU or physics: TerrainChunks cuts it
// into meshes with distance LOD, and RigidWorld's HeightField shape
// collides with the same triangles (heightAt() matches them exactly).
//
// Sample (x, z) sits at corner() + (x * spacing, height, z * spacing);
// x runs along world +x, z along world +z.
struct TerrainLayer {
    std::string name;
    glm::vec3 color{0.5f};   // linear, like physics.box colours
};

class Terrain {
public:
    static constexpr int kMinSamples = 9;
    static constexpr int kMaxSamples = 2049;
    static constexpr int kLayers = 4;

    // `size` metres across, `samples` heights per side (clamped to
    // kMinSamples..kMaxSamples), centred on `centre` (its y is the base
    // height: a flat terrain sits there).
    Terrain(float size, int samples, const glm::vec3& centre = glm::vec3(0.0f));

    int samples() const { return m_n; }
    float size() const { return m_size; }
    float spacing() const { return m_size / float(m_n - 1); }
    glm::vec3 corner() const { return m_corner; }
    glm::vec3 centre() const { return m_corner + glm::vec3(m_size * 0.5f, 0.0f, m_size * 0.5f); }

    // Heights relative to corner().y, row-major (z * samples + x).
    const std::vector<float>& heights() const { return m_heights; }
    float sample(int x, int z) const; // clamped to the grid
    void setSample(int x, int z, float h);

    bool contains(float worldX, float worldZ) const;
    // World height at (x, z), on the same two triangles per cell as the
    // collision (diagonal from (x, z) to (x+1, z+1)); clamped at the edges.
    float heightAt(float worldX, float worldZ) const;
    // Smooth normal there (from the neighbouring samples).
    glm::vec3 normalAt(float worldX, float worldZ) const;
    // Steepness in degrees (0 flat, 90 a wall).
    float slopeAt(float worldX, float worldZ) const;

    // ---- making one
    struct Noise {
        uint32_t seed = 1;
        float height = 20.0f;   // metres from the lowest to the highest point (about)
        float feature = 0.0f;   // metres across the biggest hills; 0 = a quarter of the size
        int octaves = 5;        // layers of finer detail
        float roughness = 0.5f; // how much each finer layer adds (0.3 soft .. 0.7 rocky)
        bool ridged = false;    // mountain ridges instead of rolling hills
    };
    // Replaces every height with noise (the same seed gives the same land
    // on every machine). The result runs from 0 up to about `height`.
    void generate(const Noise& noise);
    // Replaces every height from a greyscale image (8- or 16-bit PNG, ...):
    // black is 0, white is `height` metres; resampled to the grid. False
    // with `error` when it can't be read.
    bool loadImage(const std::filesystem::path& file, float height, std::string* error = nullptr);
    void fill(float h);

    // ---- sculpting. Brushes fall off smoothly to nothing at `radius`
    // (metres, around a world position; only x and z count). Each returns
    // the samples it changed as a rectangle (empty when it missed).
    struct Rect {
        int x0 = 0, z0 = 0, x1 = -1, z1 = -1; // inclusive
        bool empty() const { return x1 < x0 || z1 < z0; }
        void add(const Rect& o);
    };
    Rect raise(const glm::vec3& at, float radius, float metres);   // negative lowers
    Rect smooth(const glm::vec3& at, float radius, float strength = 0.5f);
    Rect flatten(const glm::vec3& at, float radius, float worldHeight, float strength = 1.0f);

    // ---- ground layers. Layer 0 is everywhere at first; painting moves
    // weight towards another layer. With autoRock, slopes steeper than
    // rockSlope degrees show layer 1 (rock) whatever was painted.
    std::array<TerrainLayer, kLayers> layers;
    bool autoRock = true;
    float rockSlope = 38.0f;
    Rect paint(const glm::vec3& at, float radius, int layer, float strength = 1.0f);
    // Weights 0..255 per layer at a sample (they add up to 255).
    std::array<uint8_t, kLayers> weights(int x, int z) const;
    glm::vec3 colorAt(int x, int z) const; // what a vertex there is coloured

    // Bumps with every change to heights; paintVersion with every paint.
    uint64_t version() const { return m_version; }
    uint64_t paintVersion() const { return m_paintVersion; }

    // The mesh for samples [x0, x0+cellsX] x [z0, z0+cellsZ], every `step`
    // samples (1 = full detail; halved until it divides both). With
    // `skirt` > 0, the edges get a curtain that deep, hiding cracks
    // between neighbours drawn at a different step. World positions.
    void buildMesh(int x0, int z0, int cellsX, int cellsZ, int step, float skirt, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) const;

private:
    Rect brushRect(const glm::vec3& at, float radius) const;
    float falloff(float dx, float dz, float radius) const;
    int m_n = 0;
    float m_size = 0.0f;
    glm::vec3 m_corner{0.0f};
    std::vector<float> m_heights;
    std::vector<std::array<uint8_t, kLayers>> m_weights;
    uint64_t m_version = 1, m_paintVersion = 1;
};

// Which detail each chunk of a terrain is drawn at, by distance from the
// camera. Chunks are `cells` x `cells` samples; the step doubles every
// `lodDistance` metres (1, 2, 4, 8). update() says which chunks need a new
// mesh (their step changed, or the terrain changed under them).
class TerrainChunks {
public:
    struct Chunk {
        int x0 = 0, z0 = 0;   // first sample
        int step = 0;         // drawn at; 0 = not built yet
        bool dirty = true;    // heights or paint changed inside it
    };
    TerrainChunks(const Terrain& terrain, int cells = 32, float lodDistance = 60.0f);
    int cells() const { return m_cells; }
    std::vector<Chunk>& chunks() { return m_chunks; }
    const std::vector<Chunk>& chunks() const { return m_chunks; }
    // Marks the chunks touching `r` (samples) dirty.
    void touch(const Terrain::Rect& r);
    void touchAll();
    // The step a chunk wants from `camera`.
    int stepFor(const Chunk& c, const glm::vec3& camera) const;
    // Indices of chunks to rebuild now (at most `budget` per call, nearest first).
    std::vector<size_t> update(const glm::vec3& camera, size_t budget = 8);
    // After rebuilding chunk i at `step`.
    void built(size_t i, int step);

private:
    const Terrain& m_terrain;
    int m_cells;
    float m_lodDistance;
    std::vector<Chunk> m_chunks;
};

} // namespace kke
