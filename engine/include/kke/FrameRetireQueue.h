#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace kke {

// Holds GPU resources (vertex buffers of a removed object, say) until no
// command buffer can still be using them. Freeing a buffer the moment its
// owner goes away is wrong while frames are in flight: the last one or two
// submitted frames may still read it ("vkDestroyBuffer(): can't be called
// on VkBuffer ... currently in use by VkCommandBuffer").
//
// Frame numbers are the renderer's count of recorded frames. A resource
// retired while frame N is the newest recorded one can be referenced by
// frames up to N, so it is released once frame N has completed on the GPU.
// Pure bookkeeping (no Vulkan), so it is unit-tested in tests/.
class FrameRetireQueue {
public:
    void retire(uint64_t newestRecordedFrame, std::shared_ptr<void> resource) {
        if (resource) m_entries.push_back({ newestRecordedFrame, std::move(resource) });
    }
    // Releases everything retired while frame <= completedFrame was the
    // newest recorded one. Returns how many resources were released.
    size_t releaseCompleted(uint64_t completedFrame) {
        size_t kept = 0, released = 0;
        for (Entry& e : m_entries) {
            if (e.frame <= completedFrame) {
                e.resource.reset();
                ++released;
            } else {
                m_entries[kept++] = std::move(e);
            }
        }
        m_entries.resize(kept);
        return released;
    }
    // Only once the device is idle.
    void releaseAll() { m_entries.clear(); }
    size_t pending() const { return m_entries.size(); }

private:
    struct Entry {
        uint64_t frame;
        std::shared_ptr<void> resource;
    };
    std::vector<Entry> m_entries;
};

} // namespace kke
