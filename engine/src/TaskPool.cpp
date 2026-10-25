#include "kke/TaskPool.h"

#include <algorithm>

namespace kke {

TaskPool::TaskPool(int workers) : m_workers(std::max(1, workers)) {
    for (int i = 1; i < m_workers; ++i) m_threads.emplace_back([this] { workerLoop(); });
}

TaskPool::~TaskPool() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
    }
    m_wake.notify_all();
    for (std::thread& t : m_threads) t.join();
}

// Claims tasks until none are left. Every thread of a generation runs this.
void TaskPool::drain(const std::function<void(size_t)>& task, size_t count) {
    for (size_t i = m_next.fetch_add(1, std::memory_order_relaxed); i < count; i = m_next.fetch_add(1, std::memory_order_relaxed)) {
        task(i);
        m_finished.fetch_add(1, std::memory_order_acq_rel);
    }
}

void TaskPool::workerLoop() {
    unsigned seen = 0;
    const std::function<void(size_t)>* task = nullptr;
    size_t count = 0;
    while (true) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [&] { return m_stopping || m_generation != seen; });
            if (m_stopping) return;
            seen = m_generation;
            // Woken after that run() already returned: nothing to do.
            if (!m_task) continue;
            task = m_task;
            count = m_count;
            ++m_busy;
        }
        drain(*task, count);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            --m_busy;
        }
        m_done.notify_one();
    }
}

void TaskPool::run(size_t count, const std::function<void(size_t task)>& task) {
    if (count == 0) return;
    if (m_threads.empty() || count == 1) {
        for (size_t i = 0; i < count; ++i) task(i);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_task = &task;
        m_count = count;
        m_next.store(0, std::memory_order_relaxed);
        m_finished.store(0, std::memory_order_relaxed);
        ++m_generation;
    }
    m_wake.notify_all();
    drain(task, count);
    // Every task done, and no worker still holding a reference to `task`
    // (a late worker can wake after the last task and find none left).
    std::unique_lock<std::mutex> lock(m_mutex);
    m_done.wait(lock, [&] { return m_finished.load(std::memory_order_acquire) == count && m_busy == 0; });
    m_task = nullptr;
    m_count = 0;
}

void TaskPool::forRanges(TaskPool* pool, size_t n, size_t grain, const std::function<void(size_t begin, size_t end)>& body) {
    grain = std::max<size_t>(1, grain);
    if (!pool || pool->workers() <= 1 || n < grain * 2) {
        if (n > 0) body(0, n);
        return;
    }
    const size_t chunks = std::min(n / grain, static_cast<size_t>(pool->workers()) * 4);
    const size_t per = (n + chunks - 1) / chunks;
    pool->run(chunks, [&](size_t c) {
        const size_t b = c * per, e = std::min(n, b + per);
        if (b < e) body(b, e);
    });
}

} // namespace kke
