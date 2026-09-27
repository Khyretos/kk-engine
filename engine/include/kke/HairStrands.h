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
        // Coils (HairStyle::coil): turns from root to tip at rest, where
        // they start, and this hair's share of coilRadius.
        float coilTurns = 0.0f, coilPhase = 0.0f, coilScale = 1.0f;
    };
    void build(const HairDesc& desc);
    // guides: RigidWorld::hairPositions. head: the head's world matrix now.
    // pixel: the size of a pixel one metre from the camera (2 tan(fov/2) / height).
    void ribbons(const std::vector<glm::vec3>& guides, const glm::mat4& head, const glm::vec3& camera, float pixel,
                 std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
    // What the coils need of the guides now, one per guide vertex: xyz =
    // a direction across the strand, carried along it from the root (the
    // coils turn about it), w = how stretched the strand is there (1 =
    // its rest length). HairRenderer uploads it after the points.
    void frames(const std::vector<glm::vec3>& guides, const glm::mat4& head, std::vector<glm::vec4>& out) const;
    // A coil around a hair's centre line at s (0 root .. 1 tip): the
    // offset from the line and how the offset changes along s. t = the
    // line's direction, frame = frames() at that point of its guide.
    static glm::vec3 coilOffset(const HairStyle& style, const Hair& hair, float s, const glm::vec3& t, const glm::vec4& frame, glm::vec3* change = nullptr);
    // The head as a sphere now (xyz = centre, w = radius; 0 = none): drawn
    // hairs are kept out of it (HairDesc::headCenter, headRadius). Where
    // two guides a hair blends between go round the head on either side,
    // the blend would cut through it.
    glm::vec4 headSphere(const glm::mat4& head) const;
    size_t hairs() const { return m_hairs.size(); }
    const std::vector<Hair>& list() const { return m_hairs; }
    int pointsPerHair() const { return m_points; }  // the follicle to the tip
    int strandVertices() const { return m_strand; } // per guide in RigidWorld::hairPositions
    const HairStyle& style() const { return m_style; }

private:
    std::vector<Hair> m_hairs;
    HairStyle m_style;
    glm::mat4 m_invBind{1.0f};
    glm::vec3 m_headCenter{0.0f};
    float m_headRadius = 0.0f;
    int m_strand = 0, m_points = 0;
    uint32_t m_indexHairs = 0;
    std::vector<glm::vec3> m_line; // scratch: one hair's centre line
    std::vector<float> m_restSegment; // every guide segment's rest length, guide after guide
    std::vector<glm::vec3> m_rootAcross; // per guide: across its first segment, head space
    std::vector<glm::vec4> m_frames;  // scratch for ribbons()
};

} // namespace kke
