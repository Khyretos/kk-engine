#pragma once

#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace kke {

// Plain data loaded from a .ktet.json file — the output format written
// by tools/kke_tetrahedralizer (see that tool's own header comment and
// README "Content pipeline: CGAL tetrahedralization"). Deliberately
// has zero CGAL dependency, unlike the tool that produces these files:
// this is exactly the boundary that lets a game's shipped runtime stay
// closed-source despite CGAL (GPL) existing anywhere in this repo at
// all — this loader, and everything downstream of it, only ever reads
// plain floating-point data out of a JSON file.
struct TetMeshData {
    std::vector<glm::vec3> vertices;
    std::vector<std::array<uint32_t, 4>> tets;
};

// Throws std::runtime_error on any failure — missing file, malformed
// JSON, or an internally inconsistent mesh (a tet index referencing a
// vertex that doesn't exist). Matches this engine's existing
// error-handling convention elsewhere (PhysicsModule, GameManifest)
// rather than returning an optional/null and leaving the caller to
// guess why.
TetMeshData loadTetMeshFromFile(const std::string& path);

} // namespace kke
