#pragma once
// ScriptTerrain: the terrain Lua scripts make with terrain.* (docs/TERRAIN.md).
#include "kke/Terrain.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace kke {

class Application;
class DynamicMeshRenderer;
class RigidWorld;
class ScriptVM;
struct RenderContext;
struct ShadowRenderContext;

// One terrain for the whole game, owned by the script that made it: drawn
// in chunks with distance LOD (kke::TerrainChunks), colliding through a
// static height field in the RigidWorld (so bodies, characters, rays, AI
// ground height and nav.build all see it). Sculpting rebuilds the touched
// chunks at once and the collision a moment after the brush stops.
class ScriptTerrain {
public:
    // Without an app (tests) nothing is drawn; without a world nothing collides.
    ScriptTerrain(Application* app, RigidWorld* world);
    ~ScriptTerrain();
    ScriptTerrain(const ScriptTerrain&) = delete;
    ScriptTerrain& operator=(const ScriptTerrain&) = delete;

    void bind(ScriptVM& vm);
    // Chunk meshes for the camera at `camera`, and the collision when it's due.
    void update(float dt, const glm::vec3& camera);
    void render(const RenderContext& ctx);
    void renderShadow(const ShadowRenderContext& ctx);
    // Takes the terrain away if `source` made it.
    void release(const std::string& source);
    void clear();

    Terrain* terrain() { return m_terrain.get(); }
    const std::string& owner() const { return m_owner; }
    uint32_t collisionBody() const { return m_body; }
    size_t chunksBuilt() const { return m_chunksBuilt; } // meshes made so far (all chunks, all rebuilds)
    size_t chunkCount() const { return m_meshes.size(); }
    size_t drawnLastFrame() const { return m_drawnLastFrame; } // chunks in view last frame

    // Where terrain.create{image = "..."} looks (relative names only), in order.
    std::vector<std::filesystem::path> searchDirs;
    // The largest terrain a script may make (samples per side).
    int maxSamples = 1025;

    // Collision catches up this long after the last brush stroke (and at
    // least this often while one goes on: 4 x the delay).
    float collisionDelay = 0.25f;
    size_t chunkBuildsPerFrame = 8;

private:
    void touched(const Terrain::Rect& r);
    void rebuildCollision();
    Application* m_app;
    RigidWorld* m_world;
    std::unique_ptr<Terrain> m_terrain;
    std::unique_ptr<TerrainChunks> m_chunks;
    std::vector<std::unique_ptr<DynamicMeshRenderer>> m_meshes; // per chunk, made on first build
    std::vector<size_t> m_meshIndices;                          // per chunk: indices uploaded (0 = nothing to draw)
    std::vector<std::pair<glm::vec3, glm::vec3>> m_bounds;      // per chunk: world bounds of its mesh
    size_t m_drawnLastFrame = 0;
    std::string m_owner;
    uint32_t m_body = 0xffffffffu;   // RigidWorld::kNoBody
    bool m_collisionDirty = false;
    float m_sinceEdit = 0.0f, m_dirtyFor = 0.0f;
    size_t m_chunksBuilt = 0;
    size_t m_freshAt = 0; // m_chunksBuilt when the terrain was made
};

} // namespace kke
