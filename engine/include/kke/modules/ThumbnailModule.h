#pragma once

#include "kke/ModelAsset.h"
#include "kke/Module.h"
#include "kke/Renderer.h"
#include "kke/Thumbnails.h"

#include <imgui.h>
#include <volk.h>
#include <vk_mem_alloc.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace kke {

class Buffer;
class LightingBuffer;
class ModelModule;

// Pictures of models for asset browsers (the sandbox's Assets panel).
//
// Each asset is rendered once, lit and framed by its bounds, into a
// 128 px offscreen colour + depth target with the main pass's own
// attachments (so ModelModule's lit pipeline draws there unchanged), then
// copied into one 2048 px atlas that ImGui shows directly
// (ImGui_ImplVulkan_AddTexture). The render is also read back and saved
// as a PNG in the user's cache folder, per pack (kke::thumbnailCacheFile):
// the next time the pack opens, thumbnails come from disk, not a render.
//
// Nothing stalls a frame: files are read and decoded (models with ufbx,
// cached PNGs with stb_image) on a worker thread, PNGs are written there
// too, and each frame renders at most `rendersPerFrame` models and copies
// at most `uploadsPerFrame` cached pictures. Only what an asset browser
// asks for is made, the most recently asked for first, so scrolling a
// 3,000-asset pack fills in what's on screen. The atlas holds 256; what
// scrolled away longest ago gives up its tile.
class ThumbnailModule : public Module {
public:
    static constexpr int kTile = 128;
    static constexpr int kAtlas = 2048;

    struct Settings {
        int rendersPerFrame = 2;
        int uploadsPerFrame = 16;
        std::string cacheRoot;   // empty: kke::defaultThumbnailCacheRoot()
        bool diskCache = true;
    };
    ThumbnailModule();
    explicit ThumbnailModule(const Settings& settings);
    ~ThumbnailModule() override;

    const char* name() const override { return "Thumbnails"; }
    std::vector<ModuleDependency> dependencies() const override;
    void init(Application& app) override;
    void prepass(const PrepassContext& ctx) override;
    void frameEnd() override { ++m_frame; }
    void shutdown() override;

    // NoMesh: the file loads but has nothing to draw (an animation-only
    // FBX): not an error, the browser just shows it has no picture.
    enum class State { Missing, Working, Ready, Failed, NoMesh };
    struct View {
        State state = State::Missing;
        ImTextureID texture = 0;
        ImVec2 uv0, uv1;
        const std::string* error = nullptr; // Failed: why (also logged once); NoMesh: what it is
    };
    // The thumbnail of the model at `path` (from `pack`) as it stands now;
    // asks for it if it isn't there (then `options` is called once for how
    // to load it). Call it for what's on screen each frame.
    View get(const std::string& path, const std::string& pack, const std::function<ModelLoadOptions()>& options);

    // Why thumbnails can't be made at all (no GPU format for them), or empty.
    const std::string& unavailable() const { return m_unavailable; }
    struct Stats { size_t ready = 0, working = 0, failed = 0, rendered = 0, fromDisk = 0, written = 0; };
    Stats stats() const;

    Settings settings;

private:
    struct Entry {
        State state = State::Missing;
        uint64_t wanted = 0;           // frame last asked for
        std::string error;
    };
    // Work coming back from the worker thread.
    struct Loaded {
        std::string path;
        std::vector<uint8_t> rgba;     // a cached picture (kTile x kTile), or
        std::unique_ptr<ModelData> model; // a model to render
        std::string cacheFile;         // where its picture is (or goes) on disk
        std::string error;
    };
    struct Job { std::string path, pack; ModelLoadOptions options; };
    struct Readback { std::string path, cacheFile; uint32_t index; };
    struct WriteJob { std::string file; std::vector<uint8_t> rgba; };

    void workerLoop();
    void createGpu();
    void destroyGpu();
    void failed(const std::string& path, const std::string& why);

    Application* m_app = nullptr;
    ModelModule* m_models = nullptr;
    std::string m_unavailable;
    uint64_t m_frame = 1;
    bool m_bgra = false;
    std::unordered_map<std::string, Entry> m_entries;
    ThumbnailSlots m_slots{(kAtlas / kTile) * (kAtlas / kTile)};

    // Worker thread: loads and decodes, writes PNGs.
    std::thread m_worker;
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stop = false;
    std::vector<Job> m_queue;           // to load (entry state Working); the worker never touches m_entries
    std::deque<Loaded> m_done;
    std::deque<WriteJob> m_writes;
    size_t m_rendered = 0, m_fromDisk = 0, m_written = 0;

    // GPU.
    VkRenderPass m_pass = VK_NULL_HANDLE;
    VkFormat m_colorFormat = VK_FORMAT_UNDEFINED, m_depthFormat = VK_FORMAT_UNDEFINED;
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation alloc = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    Image m_atlas, m_scratchColor, m_scratchDepth;
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkDescriptorSet m_imguiSet = VK_NULL_HANDLE;
    bool m_atlasReady = false;         // cleared and in shader-read layout
    std::unique_ptr<LightingBuffer> m_lighting;
    static constexpr uint32_t kFrames = Renderer::kMaxFramesInFlight;
    std::unique_ptr<Buffer> m_readback[kFrames];
    std::vector<Readback> m_pendingReadback[kFrames];
    std::unique_ptr<Buffer> m_upload[kFrames];
};

} // namespace kke
