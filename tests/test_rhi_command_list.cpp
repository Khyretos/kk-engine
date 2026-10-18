// The RHI's command list (docs/plans/rhi.md): Vulkan's handles survive the
// trip through RHI handles, and the interface can be implemented without
// Vulkan (as the OpenGL ES 3 backend will), recording what a draw says.

#include "kke/Pipeline.h"
#include "kke/rhi/CommandList.h"
#include "kke/rhi/VulkanCommandList.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace kke;

namespace {

// A backend that writes down each call, the way a test or a GL backend sees them.
class RecordingList final : public rhi::CommandList {
public:
    std::vector<std::string> calls;
    void bindPipeline(const Pipeline&) override { calls.push_back("pipeline"); }
    void bindGroups(const Pipeline&, uint32_t firstSet, std::span<const rhi::BindGroup> groups) override {
        calls.push_back("groups " + std::to_string(firstSet) + "+" + std::to_string(groups.size()));
    }
    void pushConstants(const Pipeline&, uint32_t stages, uint32_t, uint32_t size, const void*) override {
        calls.push_back("push " + std::to_string(stages) + " " + std::to_string(size));
    }
    void bindVertexBuffer(uint32_t slot, rhi::BufferRef b, uint64_t) override {
        calls.push_back("vertex " + std::to_string(slot) + " " + std::to_string(b.handle));
    }
    void bindIndexBuffer(rhi::BufferRef b, uint64_t, rhi::IndexType t) override {
        calls.push_back(std::string("index ") + std::to_string(b.handle) + (t == rhi::IndexType::U32 ? " u32" : " u16"));
    }
    void draw(uint32_t v, uint32_t i, uint32_t, uint32_t) override { calls.push_back("draw " + std::to_string(v) + "x" + std::to_string(i)); }
    void drawIndexed(uint32_t n, uint32_t i, uint32_t, int32_t, uint32_t) override {
        calls.push_back("drawIndexed " + std::to_string(n) + "x" + std::to_string(i));
    }
    void setViewport(float, float, float w, float h) override { calls.push_back("viewport " + std::to_string(int(w)) + "x" + std::to_string(int(h))); }
    void setScissor(int32_t, int32_t, uint32_t w, uint32_t h) override { calls.push_back("scissor " + std::to_string(w) + "x" + std::to_string(h)); }
};

} // namespace

TEST(RhiCommandList, VulkanHandlesSurviveTheTrip) {
    const VkDescriptorSet set = rhi::VulkanCommandList::fromHandle<VkDescriptorSet>(0x1234abcdull);
    EXPECT_EQ(rhi::VulkanCommandList::group(set).handle, 0x1234abcdull);
    const VkBuffer buffer = rhi::VulkanCommandList::fromHandle<VkBuffer>(0xfeedull);
    EXPECT_EQ(rhi::VulkanCommandList::buffer(buffer).handle, 0xfeedull);
    EXPECT_EQ(rhi::VulkanCommandList::toHandle(VkDescriptorSet{}), 0u);
}

TEST(RhiCommandList, ABackendSeesTheCallsInOrder) {
    RecordingList list;
    rhi::CommandList& l = list;
    // The recording backend never looks at the pipeline: a stand-in object is enough.
    alignas(Pipeline) unsigned char storage[sizeof(Pipeline)] = {};
    const Pipeline* none = reinterpret_cast<const Pipeline*>(storage);
    const rhi::BindGroup groups[2] = { { 1 }, { 2 } };
    l.setViewport(0, 0, 640, 360);
    l.setScissor(0, 0, 640, 360);
    l.bindPipeline(*none);
    l.bindGroups(*none, 0, groups);
    l.pushConstants(*none, rhi::kVertex | rhi::kFragment, 0, 96, nullptr);
    l.bindVertexBuffer(0, { 7 }, 0);
    l.bindIndexBuffer({ 8 }, 0, rhi::IndexType::U32);
    l.drawIndexed(36, 2, 0, 0, 0);
    l.draw(6, 1, 0, 0);
    const std::vector<std::string> expected{ "viewport 640x360", "scissor 640x360", "pipeline", "groups 0+2", "push 3 96",
                                             "vertex 0 7", "index 8 u32", "drawIndexed 36x2", "draw 6x1" };
    EXPECT_EQ(list.calls, expected);
}
