#pragma once

#include "kke/TetMeshAsset.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <unordered_set>
#include <vector>

namespace kke {

// The bookkeeping of a breakable object with pre-baked pieces (see
// kke/VoronoiFracture.h), with no physics engine in it: which tets touch
// which, where the piece borders are, how strong each border tet is,
// which borders have broken, and which pieces still hold together.
// PhysicsModule feeds it FEMFX stresses and spawns/removes bodies from
// its answers; tools/physics_lab does the same headlessly; the unit
// tests drive it with made-up stresses. (This is the "connection graph"
// of NVIDIA Blast / Chaos geometry collections.)
class BreakGraph {
public:
    BreakGraph() = default;
    // `strength`: per-tet multiplier on the threshold (empty = 1).
    BreakGraph(const TetMeshData& mesh, std::vector<uint32_t> chunkOfTet, std::vector<float> strength);

    size_t tetCount() const { return m_chunk.size(); }
    uint32_t chunkOf(uint32_t tet) const { return m_chunk[tet]; }
    uint32_t neighbour(uint32_t tet, int face) const { return m_neighbour[tet][face]; } // UINT32_MAX = outside
    bool isBorderTet(uint32_t tet) const { return m_border[tet] != 0; }
    // Bit f set: face f of the tet was on the outside of the whole object.
    uint8_t originalExterior(uint32_t tet) const { return m_exterior[tet]; }

    // Thresholds: base x strength, plus `restFactor` x the stress each
    // tet carried at rest (settle-then-arm, BUG-043). `rest` empty = 0.
    void arm(float baseThreshold, const std::vector<float>& rest = {}, float restFactor = 1.25f);
    float threshold(uint32_t tet) const { return m_threshold[tet]; }

    // A tet reported `stress` this step. If over its threshold, every
    // border it has with a neighbour in the same body (`sameBody`) breaks.
    // Returns true if a border broke that wasn't broken before.
    bool report(uint32_t tet, float stress, const std::function<bool(uint32_t)>& sameBody);

    bool isBroken(uint32_t chunkA, uint32_t chunkB) const;
    size_t brokenBorderCount() const { return m_broken.size(); }

    // Splits a body's tets into groups that still hold together (joined
    // by an unbroken border, or in the same piece). Groups are ordered by
    // their smallest piece id: the same on every machine.
    std::vector<std::vector<uint32_t>> groups(const std::vector<uint32_t>& bodyTets, const std::function<bool(uint32_t)>& sameBody) const;

private:
    static uint64_t key(uint32_t a, uint32_t b);
    std::vector<uint32_t> m_chunk;
    std::vector<std::array<uint32_t, 4>> m_neighbour;
    std::vector<uint8_t> m_border, m_exterior;
    std::vector<float> m_strength, m_threshold;
    std::unordered_set<uint64_t> m_broken;
};

} // namespace kke
