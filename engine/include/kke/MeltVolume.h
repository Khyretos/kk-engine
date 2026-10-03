#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace kke {

class ParticleFluid;

// A solid that can melt: a voxel grid of density (1 = solid, 0 = empty)
// and temperature, drawn as a smooth surface. Poured-on hot liquid
// (kke::ParticleFluid) heats the voxels it touches, heat spreads through
// the solid, and voxels past their melting point lose density — the
// surface recedes where the heat is, and the melted matter comes back out
// as liquid particles of the solid's own material (ice -> water, wax ->
// molten wax). It "feels" like melting, which is the brief; it isn't a
// thermodynamics solver.
//
// Choices (docs/OPTIMIZATION.md):
//  - Latent heat, the cheap way: a voxel at its melting point can't get
//    hotter; extra heat goes into losing density instead. That one rule
//    is why melting looks gradual and eats inward from the contact.
//  - Surface by marching *tetrahedra* (each cell split into the same 6
//    Kuhn tets as kke::voxelizeToTets): 16 cases, no 256-entry tables, no
//    ambiguous configurations, crack-free. Vertices are shared through an
//    edge map and normals come from the density gradient (smooth).
//  - Remeshing only when something melted (dirty flag).
//  - The fluid collides with the solid through signedDistance(): a real
//    signed distance field (two-pass chamfer transform over the voxels,
//    rebuilt only after melting), so a particle that lands deep inside is
//    pushed straight back out in one step instead of shooting up.
//  - Melting collapses (collapse()): the grid's bottom layer is the
//    ground. Solid pieces no longer joined to it (6-connected flood fill
//    over what you see, density > 0.5) fall under gravity, a cell at a
//    time, until they land and merge with whatever they land on; hot,
//    soft material (MeltMaterial::softening) sags into any hole below it.
//    So a block whose middle melts out drops its top, an undercut side
//    breaks off, and wax slumps, instead of pieces floating in the air.
//    Labelling 24^3 voxels costs ~0.1 ms, and runs only after something
//    melted or while something is falling.
struct MeltMaterial {
    float meltingPoint = 0.0f;       // degrees C
    float heatCapacity = 4.0f;       // how much heat a voxel soaks up per degree (relative units)
    float liquidHeatCapacity = 1.0f; // same for a liquid particle touching it: higher = the liquid
                                     // stays hot longer (lava carries lots of heat; low values crust it instantly)
    float meltRate = 0.02f;          // density lost per degree of heat above the melting point (latent heat)
    float conduction = 3.0f;         // heat spread between voxels per second
    uint8_t liquidMaterial = 1;      // ParticleFluid material id of the melt
    float liquidTemperature = 5.0f;  // temperature of melted-off particles
    float softening = 0.0f;          // degrees below the melting point where it starts to sag (wax, chocolate); 0 = never
    float sagSpeed = 0.15f;          // m/s soft material creeps down into a hole below it
};

class MeltVolume {
public:
    // A grid of dims cells of `cellSize` metres starting at `origin`, empty.
    MeltVolume(const glm::ivec3& dims, const glm::vec3& origin, float cellSize);

    MeltMaterial& material() { return m_material; }
    void fillBox(const glm::vec3& mn, const glm::vec3& mx, float temperature);
    void clear();

    // Heat exchange with every fluid particle near the surface, heat
    // conduction inside, melting, and emission of melted matter as new
    // particles into `fluid` (capped by the fluid's capacity).
    // Returns how many particles were emitted.
    size_t step(float dt, ParticleFluid& fluid);

    // Signed distance estimate (negative inside) and outward normal.
    float signedDistance(const glm::vec3& p, glm::vec3& normal) const;
    float density(const glm::vec3& p) const;        // trilinear, 0..1
    float temperature(const glm::vec3& p) const;    // trilinear

    // Smooth surface at density 0.5. `glow` per vertex = how close to the
    // melting point the solid there is (0 cold .. 1 melting), for shading.
    // Rebuilt only if the density changed since the last call.
    bool rebuildMesh();
    const std::vector<glm::vec3>& meshPositions() const { return m_meshPos; }
    const std::vector<glm::vec3>& meshNormals() const { return m_meshNrm; }
    const std::vector<float>& meshGlow() const { return m_meshGlow; }
    const std::vector<uint32_t>& meshIndices() const { return m_meshIdx; }

    float solidFraction() const;                     // remaining solid / initial solid
    const glm::ivec3& dims() const { return m_dims; }
    size_t fallingVoxels() const { return m_fallingVoxels; } // solid voxels falling or sagging after the last step

    // Pieces not joined to the ground (the bottom layer) fall, soft
    // material sags; step() calls it. Public for tests and for a game that
    // carves the volume itself.
    void collapse(float dt);

private:
    size_t idx(int x, int y, int z) const { return (static_cast<size_t>(z) * m_dims.y + y) * m_dims.x + x; }
    glm::vec3 center(int x, int y, int z) const { return m_origin + (glm::vec3(x, y, z) + 0.5f) * m_cell; }
    float at(const std::vector<float>& f, int x, int y, int z) const;
    float trilinear(const std::vector<float>& f, const glm::vec3& p) const;

    glm::ivec3 m_dims;
    glm::vec3 m_origin;
    float m_cell;
    MeltMaterial m_material;
    std::vector<float> m_density, m_temp, m_meltAccum, m_scratch;
    std::vector<float> m_distance;    // signed distance per voxel (negative inside), rebuilt when density changes
    std::vector<float> m_distIn, m_distOut; // rebuildDistance()'s padded working grids, kept between rebuilds
    bool m_distanceDirty = true;
    void rebuildDistance();
    float m_initialSolid = 0.0f;
    float m_startTemperature = 20.0f; // of the solid when filled (for glow)
    bool m_dirty = true;

    // collapse(): per voxel, the piece it belongs to and how fast / far it
    // has fallen since it last moved a cell.
    std::vector<int> m_label;
    std::vector<uint32_t> m_stack, m_moving;
    std::vector<float> m_fallSpeed, m_fallDistance, m_sag; // m_sag: how far soft material has crept towards the cell below
    bool m_settled = false;   // nothing changed since the last collapse() found everything resting
    size_t m_fallingVoxels = 0;

    std::vector<glm::vec3> m_meshPos, m_meshNrm;
    std::vector<float> m_meshGlow;
    std::vector<uint32_t> m_meshIdx;
};

} // namespace kke
