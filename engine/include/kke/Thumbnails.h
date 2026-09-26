#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace kke {

// The pure parts of asset thumbnails (kke::ThumbnailModule draws them):
// where each one is cached on disk, how a model is framed, and which atlas
// tile holds which thumbnail. Tests: tests/test_thumbnails.cpp.

// Thumbnails are renders of licensed packs: they live in the user's cache
// folder, never in the repository. KKE_THUMBNAIL_CACHE, else
// $XDG_CACHE_HOME/kk-engine/thumbnails, else ~/.cache/kk-engine/thumbnails
// (%LOCALAPPDATA%\kk-engine\thumbnails on Windows). Empty if none of those
// exist (then thumbnails are made but not kept).
std::string defaultThumbnailCacheRoot();

// The cache file for one asset: <root>/<pack>/<hash>.png. The hash covers
// the asset's path, size and modification time and the thumbnail format
// version, so an edited or replaced file gets a new thumbnail and an
// engine that frames them differently doesn't reuse old ones. Empty when
// `root` is empty or the asset file can't be read.
std::string thumbnailCacheFile(const std::string& root, const std::string& pack, const std::string& assetPath);

// A pack name made safe as one folder name (no separators, no "..").
std::string sanitizePackFolder(const std::string& pack);

// The model matrix that centres a model of these bounds at the origin and
// scales it into the unit sphere, and the fixed camera that sees the unit
// sphere from the front, a little right and above (a 3/4 view, like a
// shop catalogue). proj has Vulkan's Y flip applied.
glm::mat4 thumbnailFraming(const glm::vec3& boundsMin, const glm::vec3& boundsMax);
glm::mat4 thumbnailView();
glm::mat4 thumbnailProj();
glm::vec3 thumbnailCameraPosition();

// Which atlas tile holds which thumbnail. A fixed number of tiles; when
// they're all taken, the one used longest ago is reused, but never one
// used this frame (it's on screen). Keys are asset paths.
class ThumbnailSlots {
public:
    explicit ThumbnailSlots(int capacity);
    int capacity() const { return int(m_slots.size()); }
    // The tile holding `key`, or -1.
    int find(const std::string& key) const;
    // Marks the tile as used in `frame` (keeps it from being reused).
    void touch(int slot, uint64_t frame);
    // A tile for `key`: its own if it has one, else a free one, else the
    // least recently used one from before `frame` (whose old key is
    // returned in `evicted`). -1 if every tile is in use this frame.
    int acquire(const std::string& key, uint64_t frame, std::string* evicted = nullptr);
    void release(const std::string& key);
    size_t used() const { return m_byKey.size(); }

private:
    struct Slot {
        std::string key;
        uint64_t lastUsed = 0;
        bool taken = false;
    };
    std::vector<Slot> m_slots;
    std::unordered_map<std::string, int> m_byKey;
};

// Where tile `slot` is in an atlas of `atlasSize` px with `tile` px tiles
// laid out row by row: pixel offset, and UVs for ImGui.
struct ThumbnailTileRect {
    int x = 0, y = 0;
    glm::vec2 uv0{0.0f}, uv1{0.0f};
};
ThumbnailTileRect thumbnailTileRect(int slot, int atlasSize, int tile);

} // namespace kke
