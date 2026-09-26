#include "kke/Thumbnails.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace kke {

namespace {
// Bump when thumbnails are framed or lit differently: old cache files
// stop matching and are made again.
constexpr uint32_t kThumbnailVersion = 1;

uint64_t fnv1a(const void* data, size_t n, uint64_t h = 0xcbf29ce484222325ull) {
    const auto* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

std::string envOr(const char* name) {
    const char* v = std::getenv(name);
    return v && *v ? std::string(v) : std::string();
}
} // namespace

std::string defaultThumbnailCacheRoot() {
    if (std::string v = envOr("KKE_THUMBNAIL_CACHE"); !v.empty()) return v;
#ifdef _WIN32
    if (std::string v = envOr("LOCALAPPDATA"); !v.empty()) return v + "\\kk-engine\\thumbnails";
#endif
    if (std::string v = envOr("XDG_CACHE_HOME"); !v.empty()) return v + "/kk-engine/thumbnails";
    if (std::string v = envOr("HOME"); !v.empty()) return v + "/.cache/kk-engine/thumbnails";
    return {};
}

std::string sanitizePackFolder(const std::string& pack) {
    std::string out;
    for (char c : pack) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' ';
        out += ok ? c : '_';
    }
    // Never "", "." or "..": always a real folder of its own.
    if (out.empty() || out.find_first_not_of('_') == std::string::npos) out = "_pack" + out;
    return out;
}

std::string thumbnailCacheFile(const std::string& root, const std::string& pack, const std::string& assetPath) {
    if (root.empty()) return {};
    namespace fs = std::filesystem;
    std::error_code ec;
    const uintmax_t size = fs::file_size(assetPath, ec);
    if (ec) return {};
    const auto mtime = fs::last_write_time(assetPath, ec);
    if (ec) return {};
    const int64_t ticks = static_cast<int64_t>(mtime.time_since_epoch().count());
    uint64_t h = fnv1a(assetPath.data(), assetPath.size());
    h = fnv1a(&size, sizeof(size), h);
    h = fnv1a(&ticks, sizeof(ticks), h);
    h = fnv1a(&kThumbnailVersion, sizeof(kThumbnailVersion), h);
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.png", static_cast<unsigned long long>(h));
    return (fs::path(root) / sanitizePackFolder(pack) / name).string();
}

glm::mat4 thumbnailFraming(const glm::vec3& boundsMin, const glm::vec3& boundsMax) {
    glm::vec3 lo = glm::min(boundsMin, boundsMax), hi = glm::max(boundsMin, boundsMax);
    if (!std::isfinite(lo.x + lo.y + lo.z + hi.x + hi.y + hi.z)) lo = hi = glm::vec3(0.0f);
    const glm::vec3 centre = (lo + hi) * 0.5f;
    const float radius = glm::length(hi - lo) * 0.5f;
    const float s = radius > 1e-6f ? 1.0f / radius : 1.0f;
    return glm::scale(glm::mat4(1.0f), glm::vec3(s)) * glm::translate(glm::mat4(1.0f), -centre);
}

glm::vec3 thumbnailCameraPosition() {
    // Models face +Z (Synty, glTF): from the front, a little right and above.
    constexpr float kFov = glm::radians(30.0f);
    const float distance = 1.0f / std::sin(kFov * 0.5f) * 1.02f; // the unit sphere just fits
    return glm::normalize(glm::vec3(0.55f, 0.45f, 1.0f)) * distance;
}

glm::mat4 thumbnailView() { return glm::lookAt(thumbnailCameraPosition(), glm::vec3(0.0f), glm::vec3(0, 1, 0)); }

glm::mat4 thumbnailProj() {
    glm::mat4 p = glm::perspective(glm::radians(30.0f), 1.0f, 0.1f, 20.0f);
    p[1][1] *= -1.0f;
    return p;
}

ThumbnailSlots::ThumbnailSlots(int capacity) : m_slots(size_t(std::max(1, capacity))) {}

int ThumbnailSlots::find(const std::string& key) const {
    auto it = m_byKey.find(key);
    return it == m_byKey.end() ? -1 : it->second;
}

void ThumbnailSlots::touch(int slot, uint64_t frame) {
    if (slot >= 0 && slot < capacity()) m_slots[size_t(slot)].lastUsed = std::max(m_slots[size_t(slot)].lastUsed, frame);
}

int ThumbnailSlots::acquire(const std::string& key, uint64_t frame, std::string* evicted) {
    if (evicted) evicted->clear();
    if (int s = find(key); s >= 0) {
        touch(s, frame);
        return s;
    }
    int best = -1;
    for (int i = 0; i < capacity(); ++i) {
        const Slot& s = m_slots[size_t(i)];
        if (!s.taken) {
            best = i;
            break;
        }
        if (s.lastUsed >= frame) continue; // on screen now
        if (best < 0 || s.lastUsed < m_slots[size_t(best)].lastUsed) best = i;
    }
    if (best < 0) return -1;
    Slot& s = m_slots[size_t(best)];
    if (s.taken) {
        if (evicted) *evicted = s.key;
        m_byKey.erase(s.key);
    }
    s.key = key;
    s.taken = true;
    s.lastUsed = frame;
    m_byKey[key] = best;
    return best;
}

void ThumbnailSlots::release(const std::string& key) {
    auto it = m_byKey.find(key);
    if (it == m_byKey.end()) return;
    m_slots[size_t(it->second)] = Slot{};
    m_byKey.erase(it);
}

ThumbnailTileRect thumbnailTileRect(int slot, int atlasSize, int tile) {
    ThumbnailTileRect r;
    const int perRow = std::max(1, atlasSize / std::max(1, tile));
    r.x = (slot % perRow) * tile;
    r.y = (slot / perRow) * tile;
    const float inv = 1.0f / float(std::max(1, atlasSize));
    r.uv0 = glm::vec2(float(r.x), float(r.y)) * inv;
    r.uv1 = glm::vec2(float(r.x + tile), float(r.y + tile)) * inv;
    return r;
}

} // namespace kke
