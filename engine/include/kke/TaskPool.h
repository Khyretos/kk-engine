#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace kke {

// A small fork-join pool for loops that split evenly over particles,
// voxels or vertices (docs/OPTIMIZATION.md rule 8: single-core first,
// threads are a multiplier). run() hands out tasks 0..count-1 to the
// workers and the calling thread, and returns when every task is done.
//
//     kke::TaskPool pool(app.resourceBudget().workerThreads);
//     fluid.setTaskPool(&pool);
//
// `workers` counts the calling thread, so TaskPool(1) starts no thread
// and runs every task inline, in order: the same code path as no pool.
// The threads sleep between run() calls; they are woken once per call,
// so a call costs a few microseconds. Not re-entrant: a task must not
// call run() on the same pool, and only one thread may call run() at a
// time.
class TaskPool {
public:
    explicit TaskPool(int workers);
    ~TaskPool();
    TaskPool(const TaskPool&) = delete;
    TaskPool& operator=(const TaskPool&) = delete;

    int workers() const { return m_workers; }

    void run(size_t count, const std::function<void(size_t task)>& task);

    // Splits [0, n) into at most workers() * 4 ranges of at least `grain`
    // items and calls body(begin, end) for each, in parallel. With a null
    // pool, or n below 2 * grain, it is one inline call body(0, n).
    static void forRanges(TaskPool* pool, size_t n, size_t grain, const std::function<void(size_t begin, size_t end)>& body);

private:
    void workerLoop();
    void drain(const std::function<void(size_t)>& task, size_t count);

    int m_workers = 1;
    std::vector<std::thread> m_threads;
    std::mutex m_mutex;
    std::condition_variable m_wake, m_done;
    const std::function<void(size_t)>* m_task = nullptr;
    size_t m_count = 0;
    std::atomic<size_t> m_next{0};
    std::atomic<size_t> m_finished{0};
    unsigned m_generation = 0;
    int m_busy = 0; // workers inside the current generation
    bool m_stopping = false;
};

} // namespace kke
