#include "kke/TaskPool.h"

#include <gtest/gtest.h>

#include <atomic>
#include <vector>

TEST(TaskPool, RunsEveryTaskOnce) {
    for (int workers : { 1, 2, 4, 8 }) {
        kke::TaskPool pool(workers);
        EXPECT_EQ(pool.workers(), workers);
        // Many short runs back to back: a worker that wakes late must not
        // run a finished call's task, nor miss the next call's.
        for (int round = 0; round < 500; ++round) {
            const size_t count = 1 + round % 37;
            std::vector<std::atomic<int>> hits(count);
            pool.run(count, [&](size_t i) { hits[i].fetch_add(1); });
            for (size_t i = 0; i < count; ++i) ASSERT_EQ(hits[i].load(), 1) << "workers " << workers << ", round " << round;
        }
    }
}

TEST(TaskPool, OneWorkerRunsInlineInOrder) {
    kke::TaskPool pool(0); // clamped to 1: no thread of its own
    EXPECT_EQ(pool.workers(), 1);
    std::vector<size_t> order;
    pool.run(5, [&](size_t i) { order.push_back(i); });
    EXPECT_EQ(order, (std::vector<size_t>{ 0, 1, 2, 3, 4 }));
}

TEST(TaskPool, RangesCoverEverythingOnce) {
    kke::TaskPool pool(4);
    for (size_t n : { size_t(0), size_t(1), size_t(255), size_t(512), size_t(1000), size_t(100003) }) {
        std::vector<std::atomic<int>> hits(n);
        std::atomic<int> calls{ 0 };
        kke::TaskPool::forRanges(&pool, n, 256, [&](size_t b, size_t e) {
            ++calls;
            EXPECT_LT(b, e);
            for (size_t i = b; i < e; ++i) hits[i].fetch_add(1);
        });
        for (size_t i = 0; i < n; ++i) ASSERT_EQ(hits[i].load(), 1) << "n " << n;
        if (n < 512) {
            EXPECT_LE(calls.load(), 1); // too small to split
        }
        if (n >= 100000) {
            EXPECT_EQ(calls.load(), 16); // workers * 4 ranges
        }
    }
    // No pool: one inline call.
    int calls = 0;
    kke::TaskPool::forRanges(nullptr, 5000, 256, [&](size_t b, size_t e) {
        ++calls;
        EXPECT_EQ(b, 0u);
        EXPECT_EQ(e, 5000u);
    });
    EXPECT_EQ(calls, 1);
}
