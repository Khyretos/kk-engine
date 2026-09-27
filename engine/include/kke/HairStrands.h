#pragma once

#include "kke/Hair.h"
#include "kke/Mesh.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace kke {

// The drawn hairs: hairsPerGuide around every guide, each following its
// guide, leaning towards a neighbouring one and offset from it, so they
// fill the gaps between guides. kke::HairRenderer draws them on the GPU
// (only the guides are uploaded); ribbons() builds the same hairs on the
// CPU as camera-facing ribbons (Vertex: position, colour, normal = the
// hair's direction, uv = (side -1..1, root 0..tip 1)) for tests, export
// or a renderer of your own.
class HairStrands {
public:
    // One drawn hair.
    struct Hair {
        uint32_t guide = 0, other = 0; // its guide and a neighbour it blends towards
        float blend = 0.0f;            // how much of the neighbour
        glm::vec3 offset{0.0f};        // from the guide, head space
        float length = 1.0f;           // fraction of the guide's length
        glm::vec3 color{1.0f};         // linear tint
        float phase = 0.0f;            // frizz
    };
    void build(const HairDesc& desc);
    // guides: RigidWorld::hairPositions. head: the head's world matrix now.
    // pixel: the size of a pixel one metre from the camera (2 tan(fov/2) / height).
    void ribbons(const std::vector<glm::vec3>& guides, const glm::mat4& head, const glm::vec3& camera, float pixel,
                 std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
    size_t hairs() const { return m_hairs.size(); }
    const std::vector<Hair>& list() const { return m_hairs; }
    int pointsPerHair() const { return m_points; }  // the follicle to the tip
    int strandVertices() const { return m_strand; } // per guide in RigidWorld::hairPositions
    const HairStyle& style() const { return m_style; }

private:
    std::vector<Hair> m_hairs;
    HairStyle m_style;
    glm::mat4 m_invBind{1.0f};
    int m_strand = 0, m_points = 0;
    uint32_t m_indexHairs = 0;
    std::vector<glm::vec3> m_line; // scratch: one hair's centre line
};

} // namespace kke
