#pragma once

#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace kke {

// A tetrahedral mesh as plain data, and the .ktet.json file format to
// store one. Produced at runtime by kke::voxelizeToTets (VoxelTets.h);
// the file format is kept for asset cooking — saving pre-built tet
// meshes so props don't need voxelizing at load time. (The offline CGAL
// tool that used to write these files was removed; see README.)
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
