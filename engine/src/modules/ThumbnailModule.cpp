#include "kke/modules/ThumbnailModule.h"

#include "kke/Application.h"
#include "kke/Buffer.h"
#include "kke/LightingBuffer.h"
#include "kke/Log.h"
#include "kke/VulkanCheck.h"
#include "kke/VulkanDevice.h"
#include "kke/modules/ModelModule.h"

#include <backends/imgui_impl_vulkan.h>
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace kke {

namespace {
constexpr VkDeviceSize kTileBytes = VkDeviceSize(ThumbnailModule::kTile) * ThumbnailModule::kTile * 4;
// A cached picture nobody has looked at for this many frames isn't worth
// a tile any more (it's still on disk).
constexpr uint64_t kStaleFrames = 120;

void swapRedBlue(uint8_t* px, size_t pixels) {
    for (size_t i = 0; i < pixels; ++i) std::swap(px[4 * i], px[4 * i + 2]);
}
} // namespace

ThumbnailModule::ThumbnailModule() : ThumbnailModule(Settings{}) {}
ThumbnailModule::ThumbnailModule(const Settings& s) : settings(s) {}
ThumbnailModule::~ThumbnailModule() {
    // shutdown() normally ran; this only makes sure the thread never outlives us.
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    if (m_worker.joinable()) m_worker.join();
}

std::vector<ModuleDependency> ThumbnailModule::dependencies() const {
    return { { typeid(ModelModule), true, "thumbnails are drawn with its lit pipeline" } };
}

void ThumbnailModule::init(Application& app) {
    m_app = &app;
    m_models = app.getModule<ModelModule>();
    settings.rendersPerFrame = std::max(1, settings.rendersPerFrame);
    settings.uploadsPerFrame = std::max(1, settings.uploadsPerFrame);
    if (settings.cacheRoot.empty()) settings.cacheRoot = defaultThumbnailCacheRoot();
    if (settings.diskCache && settings.cacheRoot.empty())
        log::get(name())->warn("No cache folder (set KKE_THUMBNAIL_CACHE or HOME): thumbnails will be made again every run");
    createGpu();
    if (m_unavailable.empty()) {
        log::get(name())->info("Thumbnails: {} px, {} in the atlas, cache {}", kTile, m_slots.capacity(),
                               settings.diskCache && !settings.cacheRoot.empty() ? settings.cacheRoot : "off");
    }
    m_worker = std::thread([this] { workerLoop(); });
}

// ------------------------------------------------------------------ GPU

void ThumbnailModule::createGpu() {
    Renderer& r = m_app->renderer();
    VulkanDevice& device = m_app->device();
    VkDevice dev = device.device();
    m_colorFormat = r.colorFormat();
    m_depthFormat = r.depthFormat();
    switch (m_colorFormat) {
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_R8G8B8A8_UNORM: m_bgra = false; break;
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM: m_bgra = true; break;
        default:
            m_unavailable = "the window's colour format isn't 8-bit RGBA/BGRA, so thumbnails can't be copied to and from disk";
            log::get(name())->warn("Thumbnails off: {} (format {})", m_unavailable, int(m_colorFormat));
            return;
    }

    // The main pass's attachments and subpass dependency (render passes
    // are only compatible, i.e. share pipelines, when those match); only
    // the colour's final layout differs: it ends ready to be copied.
    std::array<VkAttachmentDescription, 2> a{};
    a[0].format = m_colorFormat;
    a[0].samples = VK_SAMPLE_COUNT_1_BIT;
    a[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    a[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    a[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    a[1].format = m_depthFormat;
    a[1].samples = VK_SAMPLE_COUNT_1_BIT;
    a[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    a[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a[1].stencilLoadOp = r.hasStencil() ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    a[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = static_cast<uint32_t>(a.size());
    info.pAttachments = a.data();
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;
    VK_CHECK(vkCreateRenderPass(dev, &info, nullptr, &m_pass));

    auto makeImage = [&](Image& img, uint32_t size, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect) {
        VkImageCreateInfo ii{};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.extent = { size, size, 1 };
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.format = format;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ii.usage = usage;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        VK_CHECK(vmaCreateImage(device.allocator(), &ii, &ai, &img.image, &img.alloc, nullptr));
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = img.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format;
        vi.subresourceRange = { aspect, 0, 1, 0, 1 };
        VK_CHECK(vkCreateImageView(dev, &vi, nullptr, &img.view));
    };
    makeImage(m_scratchColor, kTile, m_colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    makeImage(m_scratchDepth, kTile, m_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
              VK_IMAGE_ASPECT_DEPTH_BIT | (r.hasStencil() ? VK_IMAGE_ASPECT_STENCIL_BIT : 0));
    makeImage(m_atlas, kAtlas, m_colorFormat, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

    std::array<VkImageView, 2> views = { m_scratchColor.view, m_scratchDepth.view };
    VkFramebufferCreateInfo fb{};
    fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb.renderPass = m_pass;
    fb.attachmentCount = static_cast<uint32_t>(views.size());
    fb.pAttachments = views.data();
    fb.width = kTile;
    fb.height = kTile;
    fb.layers = 1;
    VK_CHECK(vkCreateFramebuffer(dev, &fb, nullptr, &m_framebuffer));

    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 0.0f;
    VK_CHECK(vkCreateSampler(dev, &si, nullptr, &m_sampler));
    m_imguiSet = ImGui_ImplVulkan_AddTexture(m_sampler, m_atlas.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_atlasReady = false;

    // A small studio: a warm key light from the upper left, a cool fill
    // from the right, generous ambient; no shadows (there is no floor).
    Lighting light;
    light.shadowsEnabled = false;
    light.ambientColor = glm::vec3(0.32f, 0.33f, 0.36f);
    light.lights[0].enabled = true;
    light.lights[0].isDirectional = true;
    light.lights[0].direction = glm::normalize(glm::vec3(0.6f, -0.8f, -0.5f));
    light.lights[0].color = glm::vec3(1.0f, 0.97f, 0.9f);
    light.lights[0].intensity = 1.1f;
    light.lights[1].enabled = true;
    light.lights[1].isDirectional = true;
    light.lights[1].direction = glm::normalize(glm::vec3(-0.8f, -0.2f, -0.4f));
    light.lights[1].color = glm::vec3(0.75f, 0.82f, 1.0f);
    light.lights[1].intensity = 0.35f;
    m_lighting = std::make_unique<LightingBuffer>(device);
    m_lighting->update(light, thumbnailCameraPosition(), glm::mat4(1.0f), thumbnailProj() * thumbnailView());

    for (uint32_t f = 0; f < kFrames; ++f) {
        m_readback[f] = std::make_unique<Buffer>(device, kTileBytes * VkDeviceSize(settings.rendersPerFrame), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                 VMA_MEMORY_USAGE_GPU_TO_CPU);
        m_upload[f] = std::make_unique<Buffer>(device, kTileBytes * VkDeviceSize(settings.uploadsPerFrame), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                               VMA_MEMORY_USAGE_CPU_TO_GPU);
        m_pendingReadback[f].clear();
    }
}

void ThumbnailModule::destroyGpu() {
    if (!m_app) return;
    VulkanDevice& device = m_app->device();
    VkDevice dev = device.device();
    if (m_imguiSet) ImGui_ImplVulkan_RemoveTexture(m_imguiSet);
    m_imguiSet = VK_NULL_HANDLE;
    if (m_sampler) vkDestroySampler(dev, m_sampler, nullptr);
    m_sampler = VK_NULL_HANDLE;
    if (m_framebuffer) vkDestroyFramebuffer(dev, m_framebuffer, nullptr);
    m_framebuffer = VK_NULL_HANDLE;
    for (Image* img : { &m_atlas, &m_scratchColor, &m_scratchDepth }) {
        if (img->view) vkDestroyImageView(dev, img->view, nullptr);
        if (img->image) vmaDestroyImage(device.allocator(), img->image, img->alloc);
        *img = Image{};
    }
    if (m_pass) vkDestroyRenderPass(dev, m_pass, nullptr);
    m_pass = VK_NULL_HANDLE;
    m_lighting.reset();
    for (uint32_t f = 0; f < kFrames; ++f) {
        m_readback[f].reset();
        m_upload[f].reset();
    }
    m_atlasReady = false;
}

// ------------------------------------------------------------ requests

ThumbnailModule::View ThumbnailModule::get(const std::string& path, const std::string& pack, const std::function<ModelLoadOptions()>& options) {
    View v;
    if (!m_unavailable.empty()) {
        v.state = State::Failed;
        v.error = &m_unavailable;
        return v;
    }
    Entry& e = m_entries[path];
    e.wanted = m_frame;
    if (e.state == State::Ready) {
        const int slot = m_slots.find(path);
        if (slot >= 0) {
            m_slots.touch(slot, m_frame);
            const ThumbnailTileRect r = thumbnailTileRect(slot, kAtlas, kTile);
            v.state = State::Ready;
            v.texture = reinterpret_cast<ImTextureID>(m_imguiSet);
            v.uv0 = ImVec2(r.uv0.x, r.uv0.y);
            v.uv1 = ImVec2(r.uv1.x, r.uv1.y);
            return v;
        }
        e.state = State::Missing; // its tile went to something else: fetch it again (from disk)
    }
    if (e.state == State::Missing) {
        e.state = State::Working;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.push_back({ path, pack, options ? options() : ModelLoadOptions{} });
        }
        m_wake.notify_one();
    }
    v.state = e.state;
    if (e.state == State::Failed || e.state == State::NoMesh) v.error = &e.error;
    return v;
}

void ThumbnailModule::failed(const std::string& path, const std::string& why) {
    Entry& e = m_entries[path];
    e.state = State::Failed;
    e.error = why;
    log::get(name())->warn("No thumbnail for '{}': {}", path, why);
}

ThumbnailModule::Stats ThumbnailModule::stats() const {
    Stats s;
    for (const auto& [path, e] : m_entries) {
        if (e.state == State::Ready) ++s.ready;
        else if (e.state == State::Working) ++s.working;
        else if (e.state == State::Failed) ++s.failed;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    s.rendered = m_rendered;
    s.fromDisk = m_fromDisk;
    s.written = m_written;
    return s;
}

// --------------------------------------------------------------- worker

void ThumbnailModule::workerLoop() {
    for (;;) {
        const std::string cacheRoot = settings.diskCache ? settings.cacheRoot : std::string();
        Job job;
        WriteJob write;
        bool haveWrite = false;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [&] { return m_stop || !m_queue.empty() || !m_writes.empty(); });
            if (!m_writes.empty()) {
                write = std::move(m_writes.front());
                m_writes.pop_front();
                haveWrite = true;
            } else if (m_stop) {
                return; // every write is done
            } else {
                // Newest request first: what's on screen now.
                job = std::move(m_queue.back());
                m_queue.pop_back();
            }
        }
        if (haveWrite) {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::create_directories(fs::path(write.file).parent_path(), ec);
            if (ec) {
                log::get(name())->warn("Can't create thumbnail cache folder for '{}': {}", write.file, ec.message());
            } else if (!stbi_write_png(write.file.c_str(), kTile, kTile, 4, write.rgba.data(), kTile * 4)) {
                log::get(name())->warn("Can't write thumbnail '{}'", write.file);
            } else {
                std::lock_guard<std::mutex> lock(m_mutex);
                ++m_written;
            }
            continue;
        }

        const std::string& path = job.path;
        Loaded out;
        out.path = path;
        const std::string cacheFile = thumbnailCacheFile(cacheRoot, job.pack, path);
        out.cacheFile = cacheFile;
        bool haveCached = false;
        if (!cacheFile.empty() && std::filesystem::exists(cacheFile)) {
            int w = 0, h = 0, n = 0;
            if (stbi_uc* px = stbi_load(cacheFile.c_str(), &w, &h, &n, 4)) {
                if (w == kTile && h == kTile) {
                    out.rgba.assign(px, px + kTileBytes);
                    haveCached = true;
                } else {
                    log::get(name())->warn("Thumbnail cache '{}' is {}x{}, not {}x{}: making it again", cacheFile, w, h, kTile, kTile);
                }
                stbi_image_free(px);
            } else {
                log::get(name())->warn("Thumbnail cache '{}' can't be read ({}): making it again", cacheFile, stbi_failure_reason());
            }
        }
        if (!haveCached) {
            try {
                job.options.loadAnimations = false; // a picture needs no clips (and they're most of the load time)
                job.options.allowNoMeshes = true;   // an animation-only file isn't an error, just has no picture
                out.model = std::make_unique<ModelData>(loadModel(path, job.options));
            } catch (const std::exception& ex) {
                out.error = ex.what();
            }
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        m_done.push_back(std::move(out));
    }
}

// --------------------------------------------------------------- frame

void ThumbnailModule::prepass(const PrepassContext& ctx) {
    if (!m_unavailable.empty() || !m_pass) return;
    VkCommandBuffer cmd = ctx.cmd;
    const uint32_t f = ctx.frameIndex % kFrames;
    VulkanDevice& device = m_app->device();

    // 1. This frame slot's last readbacks are done (its fence was waited
    //    on): hand them to the worker to save.
    if (!m_pendingReadback[f].empty()) {
        std::vector<uint8_t> all(size_t(kTileBytes) * m_pendingReadback[f].size());
        m_readback[f]->download(all.data(), all.size());
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (const Readback& rb : m_pendingReadback[f]) {
                if (rb.cacheFile.empty()) continue;
                WriteJob job{ rb.cacheFile, std::vector<uint8_t>(all.begin() + ptrdiff_t(kTileBytes * rb.index),
                                                                 all.begin() + ptrdiff_t(kTileBytes * (rb.index + 1))) };
                if (m_bgra) swapRedBlue(job.rgba.data(), size_t(kTile) * kTile);
                m_writes.push_back(std::move(job));
            }
        }
        m_wake.notify_one();
        m_pendingReadback[f].clear();
    }

    // 2. What the worker finished, within this frame's budget.
    std::vector<Loaded> uploads, renders;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::deque<Loaded> keep;
        while (!m_done.empty()) {
            Loaded l = std::move(m_done.front());
            m_done.pop_front();
            if (!l.error.empty() || (l.rgba.empty() && !l.model)) {
                renders.push_back(std::move(l)); // reported below, outside the budget
            } else if (!l.rgba.empty() && int(uploads.size()) < settings.uploadsPerFrame) {
                uploads.push_back(std::move(l));
            } else if (l.model && int(renders.size()) < settings.rendersPerFrame) {
                renders.push_back(std::move(l));
            } else {
                keep.push_back(std::move(l));
            }
        }
        m_done.swap(keep);
    }

    bool atlasInTransfer = false;
    auto atlasToTransfer = [&] {
        if (atlasInTransfer) return;
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.oldLayout = m_atlasReady ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = m_atlas.image;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        // After last frame's ImGui reads of the atlas.
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        if (!m_atlasReady) {
            VkClearColorValue clear{};
            VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            vkCmdClearColorImage(cmd, m_atlas.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
            VkMemoryBarrier mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
            m_atlasReady = true;
        }
        atlasInTransfer = true;
    };
    auto tileFor = [&](const std::string& path) -> int {
        std::string evicted;
        const int slot = m_slots.acquire(path, m_frame, &evicted);
        if (!evicted.empty()) {
            auto it = m_entries.find(evicted);
            if (it != m_entries.end() && it->second.state == State::Ready) it->second.state = State::Missing;
        }
        return slot;
    };
    auto requeue = [&](Loaded&& l) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_done.push_front(std::move(l));
    };

    // 3. Cached pictures: into the upload buffer, then into their tiles.
    std::vector<uint8_t> staging;
    std::vector<VkBufferImageCopy> copies;
    for (Loaded& l : uploads) {
        Entry& e = m_entries[l.path];
        if (m_frame > e.wanted + kStaleFrames) { // scrolled away long ago: leave it on disk
            e.state = State::Missing;
            continue;
        }
        const int slot = tileFor(l.path);
        if (slot < 0) { requeue(std::move(l)); continue; }
        const uint32_t index = uint32_t(copies.size());
        staging.resize(size_t(kTileBytes) * (index + 1));
        std::memcpy(staging.data() + size_t(kTileBytes) * index, l.rgba.data(), size_t(kTileBytes));
        if (m_bgra) swapRedBlue(staging.data() + size_t(kTileBytes) * index, size_t(kTile) * kTile);
        const ThumbnailTileRect r = thumbnailTileRect(slot, kAtlas, kTile);
        VkBufferImageCopy c{};
        c.bufferOffset = kTileBytes * index;
        c.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        c.imageOffset = { r.x, r.y, 0 };
        c.imageExtent = { uint32_t(kTile), uint32_t(kTile), 1 };
        copies.push_back(c);
        e.state = State::Ready;
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_fromDisk;
    }
    if (!copies.empty()) {
        m_upload[f]->upload(staging.data(), staging.size());
        atlasToTransfer();
        vkCmdCopyBufferToImage(cmd, m_upload[f]->handle(), m_atlas.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, uint32_t(copies.size()), copies.data());
    }

    // 4. Models: render each into the scratch target, copy it to its tile
    //    and to the readback buffer (saved to disk two frames from now).
    const Application& app = *m_app;
    for (Loaded& l : renders) {
        if (!l.error.empty() || !l.model) {
            failed(l.path, l.error.empty() ? "nothing to draw" : l.error);
            continue;
        }
        if (l.model->meshes.empty() || l.model->triangleCount() == 0) {
            Entry& e = m_entries[l.path];
            e.state = State::NoMesh;
            e.error = "the file has no mesh (an animation?)";
            continue;
        }
        const int slot = tileFor(l.path);
        if (slot < 0) { requeue(std::move(l)); continue; }
        const glm::mat4 framing = thumbnailFraming(l.model->boundsMin, l.model->boundsMax);
        const ModelModule::ModelId id = m_models->add(std::move(*l.model), "thumbnail:" + l.path, true);
        if (!id) {
            m_slots.release(l.path);
            failed(l.path, "the model couldn't be uploaded");
            continue;
        }

        // The scratch target was last copied from (this frame or the one
        // before): finish that before drawing over it.
        VkMemoryBarrier mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
        std::array<VkClearValue, 2> clears{};
        clears[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        clears[1].depthStencil = { 1.0f, 0 };
        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = m_pass;
        rp.framebuffer = m_framebuffer;
        rp.renderArea = { { 0, 0 }, { uint32_t(kTile), uint32_t(kTile) } };
        rp.clearValueCount = uint32_t(clears.size());
        rp.pClearValues = clears.data();
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport viewport{ 0.0f, 0.0f, float(kTile), float(kTile), 0.0f, 1.0f };
        VkRect2D scissor{ { 0, 0 }, { uint32_t(kTile), uint32_t(kTile) } };
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        m_models->drawStandalone(cmd, id, framing, m_lighting->descriptorSet(), app.shadowMapDescriptorSet(), app.defaultTextureDescriptorSet());
        vkCmdEndRenderPass(cmd);
        m_models->unload(id); // its buffers are freed once this frame is done

        VkMemoryBarrier toCopy{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &toCopy, 0, nullptr, 0, nullptr);
        atlasToTransfer();
        const ThumbnailTileRect r = thumbnailTileRect(slot, kAtlas, kTile);
        VkImageCopy ic{};
        ic.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        ic.dstSubresource = ic.srcSubresource;
        ic.dstOffset = { r.x, r.y, 0 };
        ic.extent = { uint32_t(kTile), uint32_t(kTile), 1 };
        vkCmdCopyImage(cmd, m_scratchColor.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_atlas.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ic);
        const uint32_t index = uint32_t(m_pendingReadback[f].size());
        VkBufferImageCopy bc{};
        bc.bufferOffset = kTileBytes * index;
        bc.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        bc.imageExtent = { uint32_t(kTile), uint32_t(kTile), 1 };
        vkCmdCopyImageToBuffer(cmd, m_scratchColor.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_readback[f]->handle(), 1, &bc);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_rendered;
        }
        m_pendingReadback[f].push_back({ l.path, l.cacheFile, index });
        m_entries[l.path].state = State::Ready;
    }
    if (!m_pendingReadback[f].empty()) {
        VkMemoryBarrier toHost{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &toHost, 0, nullptr, 0, nullptr);
    }
    if (atlasInTransfer) {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = m_atlas.image;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    }
    (void)device;
}

void ThumbnailModule::shutdown() {
    if (m_app && m_pass) {
        // Save what was rendered but not yet read back.
        vkDeviceWaitIdle(m_app->device().device());
        for (uint32_t f = 0; f < kFrames; ++f) {
            if (m_pendingReadback[f].empty()) continue;
            std::vector<uint8_t> all(size_t(kTileBytes) * m_pendingReadback[f].size());
            m_readback[f]->download(all.data(), all.size());
            std::lock_guard<std::mutex> lock(m_mutex);
            for (const Readback& rb : m_pendingReadback[f]) {
                if (rb.cacheFile.empty()) continue;
                WriteJob job{ rb.cacheFile, std::vector<uint8_t>(all.begin() + ptrdiff_t(kTileBytes * rb.index),
                                                                 all.begin() + ptrdiff_t(kTileBytes * (rb.index + 1))) };
                if (m_bgra) swapRedBlue(job.rgba.data(), size_t(kTile) * kTile);
                m_writes.push_back(std::move(job));
            }
            m_pendingReadback[f].clear();
        }
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
        m_queue.clear();
    }
    m_wake.notify_all();
    if (m_worker.joinable()) m_worker.join(); // after the last PNG is written
    if (m_pass) {
        vkDeviceWaitIdle(m_app->device().device());
        destroyGpu();
    }
}

} // namespace kke
