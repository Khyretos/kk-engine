#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace kke {

// Decals (ENG-F12): scorch marks, splats, cracks, bullet holes, paint and
// puddles laid onto whatever is there. A decal is a box projected onto the
// surfaces inside it. Most engines draw decals as boxes into a G-buffer
// (deferred), or as extra geometry clipped to the meshes they land on. Ours
// are applied inside the shading of every lit surface: each pixel looks up
// the decals its cell of the view lists (the same cells as the many lights,
// ENG-F08) and mixes them into its colour and roughness before it is lit.
// So a decal is lit, shadowed and reflected like the surface under it, sits
// right on curved and moving surfaces, keeps MSAA's edges and costs nothing
// where there are none. Up to kMax at once; the oldest goes when full.
//
//   auto id = app.decals().place({ .texture = "scorch", .position = hit.point, .normal = hit.normal, .size = 2.0f }, app.time());
//   app.decals().remove(id);
struct Decal {
    std::string texture = "scorch"; // a built-in (decalBuiltIns()) or assets/decals/<name>.png
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f}; // out of the surface: the box is projected against it
    float size = 1.0f;     // width and length (m)
    float depth = 0.0f;    // how far it reaches in front of and behind its spot (m); 0 = a quarter of the size
    float angle = 0.0f;    // its turn round the normal (radians)
    glm::vec3 color{1.0f}; // times the texture's colour
    float opacity = 1.0f;
    float roughness = -1.0f; // the surface's roughness under it (0.05 wet, 0.9 dusty); below 0 keeps the surface's
    float life = 0.0f;       // seconds before it starts to fade (0: stays)
    float fade = 1.0f;       // seconds it takes to fade out
};

using DecalId = uint32_t;

// A decal as the lit shaders read it (shaders/lighting_ubo.glsl's DecalData, std430).
struct GpuDecal {
    glm::mat4 worldToDecal; // the box to -1..1 on each axis, z along the normal
    glm::vec4 colorOpacity; // linear tint, opacity (with its fade)
    glm::vec4 params;       // x = texture layer, y = roughness (< 0: keep), zw = 0
    glm::vec4 normal;       // xyz = the decal's normal
};

// The box of a decal: world to -1..1 on each axis, x and y across its face, z along its normal.
glm::mat4 decalWorldToLocal(const Decal& d);
// How much of a decal shows on a surface at `local` (decalWorldToLocal) facing `normalDot` (the
// surface normal against the decal's): none outside the box, fading out toward the ends of its
// depth and on surfaces turned away from it (no stretched smears down walls).
float decalWeight(const glm::vec3& local, float normalDot);
// Its opacity `age` seconds after it was placed: 1 through its life, then down to 0 over `fade`.
float decalFade(const Decal& d, float age);

// The textures made in code, so decals work with no assets: scorch, splat, crack, hole, puddle,
// footprint. RGBA8 sRGB, `size` x `size`, alpha the coverage; empty for an unknown name.
const std::vector<std::string>& decalBuiltIns();
std::vector<uint8_t> decalTexture(const std::string& name, int size);

// Every decal placed and not yet gone: kke::Application keeps one (decals()).
class DecalSet {
public:
    static constexpr size_t kMax = 1024;

    // `layer` is the texture's layer in the decal textures (kke::Application finds it).
    DecalId place(const Decal& d, float now, uint16_t layer);
    bool remove(DecalId id);
    void clear();
    // Drops the ones that have faded out by `now`.
    void update(float now);
    size_t count() const { return m_decals.size(); }

    // The GPU records and, for the cells, each one's bounding sphere (xyz centre, w radius).
    void build(float now, std::vector<GpuDecal>& out, std::vector<glm::vec4>& spheres) const;

private:
    struct Placed {
        DecalId id;
        Decal decal;
        float placedAt;
        uint16_t layer;
    };
    std::vector<Placed> m_decals; // oldest first
    DecalId m_next = 1;
};

} // namespace kke
