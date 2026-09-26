// FrameRetireQueue: GPU resources of removed objects are kept until the
// frames that may still draw them have completed (the fix for the
// "vkDestroyBuffer ... currently in use by VkCommandBuffer" validation
// error when a breakable split in kke_demo's stress test).

#include "kke/FrameRetireQueue.h"
#include <gtest/gtest.h>

using kke::FrameRetireQueue;

namespace {
struct Tracked {
    explicit Tracked(int* alive) : alive(alive) { ++*alive; }
    ~Tracked() { --*alive; }
    int* alive;
};
} // namespace

TEST(FrameRetireQueue, KeepsResourceUntilItsFrameCompletes) {
    int alive = 0;
    FrameRetireQueue q;
    q.retire(5, std::make_shared<Tracked>(&alive)); // frame 5 may still reference it
    EXPECT_EQ(q.releaseCompleted(4), 0u);
    EXPECT_EQ(alive, 1);
    EXPECT_EQ(q.releaseCompleted(5), 1u);
    EXPECT_EQ(alive, 0);
    EXPECT_EQ(q.pending(), 0u);
}

TEST(FrameRetireQueue, ReleasesOnlyCompletedFramesAndKeepsOrder) {
    int alive = 0;
    FrameRetireQueue q;
    for (uint64_t f = 1; f <= 6; ++f) q.retire(f, std::make_shared<Tracked>(&alive));
    EXPECT_EQ(q.releaseCompleted(3), 3u);
    EXPECT_EQ(alive, 3);
    EXPECT_EQ(q.releaseCompleted(3), 0u); // nothing twice
    EXPECT_EQ(q.releaseCompleted(10), 3u);
    EXPECT_EQ(alive, 0);
}

TEST(FrameRetireQueue, IgnoresNullAndReleasesAllOnShutdown) {
    int alive = 0;
    FrameRetireQueue q;
    q.retire(1, nullptr);
    EXPECT_EQ(q.pending(), 0u);
    q.retire(100, std::make_shared<Tracked>(&alive));
    q.releaseAll();
    EXPECT_EQ(alive, 0);
}

TEST(FrameRetireQueue, TakesOwnershipFromUniquePtr) {
    int alive = 0;
    FrameRetireQueue q;
    auto owned = std::make_unique<Tracked>(&alive);
    q.retire(2, std::shared_ptr<Tracked>(std::move(owned)));
    EXPECT_EQ(owned, nullptr);
    EXPECT_EQ(alive, 1);
    q.releaseCompleted(2);
    EXPECT_EQ(alive, 0);
}
