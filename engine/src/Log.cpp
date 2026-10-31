#include <atomic>
#include <algorithm>
#include <cstring>
#include <mutex>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <spdlog/async.h>
#include <spdlog/sinks/base_sink.h>
#include "kke/Log.h"

#include <spdlog/async.h>
#include <spdlog/details/console_globals.h>
#include <spdlog/details/registry.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstdio>
#include <cstdlib>

namespace kke::log {

namespace {
// Fallback pattern (no game name yet) in case something logs before
// init() runs — "?" is an honest placeholder, not a silent omission.
std::string g_pattern = "[%Y-%m-%d %H:%M:%S.%e][engine][%n][?]%^[%l]: %v%$";
bool g_initialized = false;
bool g_exited = false;

// The async loggers' worker thread must be joined before the process
// tears down. Application's destructor calls shutdown(), but anything
// that logs without an Application (unit tests, tools) left the thread
// running into exit: on Windows the worker lives in libspdlog.dll, and
// the thread pool's destructor then waits forever on a thread Windows has
// already stopped (kke_tests.exe hung after "[ PASSED ]" in the Release
// workflow, docs/RELEASES.md). An atexit handler runs while threads are
// still alive, on every platform.
//
// atexit handlers and static destructors run in one combined reverse
// order, so the handler must be registered after the statics it uses
// exist: spdlog's registry and the console sinks' mutex are function-local
// statics, and registering before they were built ran the handler after
// the registry was destroyed (heap corruption at exit, "malloc_consolidate():
// unaligned fastbin chunk detected" in the Benchmarks workflow).
// The last lines logged, in a fixed array a crash handler can read without a lock.
constexpr size_t kRingLines = 128, kRingWidth = 256;
struct Ring {
    char lines[kRingLines][kRingWidth] = {};
    std::atomic<unsigned> next{ 0 };
};
Ring g_ring;

class RingSink : public spdlog::sinks::base_sink<std::mutex> {
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);
        size_t n = std::min(formatted.size(), kRingWidth - 1);
        while (n > 0 && (formatted.data()[n - 1] == '\n' || formatted.data()[n - 1] == '\r')) --n;
        char* line = g_ring.lines[g_ring.next.fetch_add(1) % kRingLines];
        line[0] = '\0'; // a reader that arrives now sees an empty line, not half of two
        std::memcpy(line, formatted.data(), n);
        line[n] = '\0';
    }
    void flush_() override {}
};

std::shared_ptr<RingSink> ringSink() {
    static const std::shared_ptr<RingSink> sink = std::make_shared<RingSink>();
    return sink;
}

void shutdownAtExit() {
    g_exited = true;
    spdlog::shutdown();
}

void registerExitShutdown() {
    spdlog::details::registry::instance();
    spdlog::details::console_mutex::mutex();
    static const bool registered = std::atexit(shutdownAtExit) == 0;
    if (!registered) std::fprintf(stderr, "kke::log: could not register the exit-time logger shutdown\n");
}
} // namespace

void init(const std::string& gameName, spdlog::level::level_enum level) {
    if (g_initialized) return;
    g_initialized = true;

    registerExitShutdown();
    spdlog::init_thread_pool(8192, 1);

    // Baked directly into the pattern string since the game name is
    // invariant for the life of the process — see Log.h.
    g_pattern = "[%Y-%m-%d %H:%M:%S.%e][engine][%n][" + gameName + "]%^[%l]: %v%$";
    spdlog::set_pattern(g_pattern);
    spdlog::set_level(level);
    // Warnings/errors flush immediately (you want to see those right
    // away); info/debug batch through the queue for throughput.
    spdlog::flush_on(spdlog::level::warn);
}

std::shared_ptr<spdlog::logger> get(const std::string& moduleName) {
    if (auto existing = spdlog::get(moduleName)) {
        return existing;
    }

    // After the exit-time shutdown (a static destructor logging), no new
    // worker thread: log synchronously.
    if (g_exited) {
        auto logger = spdlog::stdout_color_mt(moduleName);
        logger->set_pattern(g_pattern);
        return logger;
    }
    registerExitShutdown();
    // What create_async_nb does, with a second sink: the ring of recent lines.
    const std::vector<spdlog::sink_ptr> sinks{ std::make_shared<spdlog::sinks::stdout_color_sink_mt>(), ringSink() };
    auto logger = std::make_shared<spdlog::async_logger>(moduleName, sinks.begin(), sinks.end(), spdlog::thread_pool(),
                                                         spdlog::async_overflow_policy::overrun_oldest);
    spdlog::register_logger(logger);
    logger->set_pattern(g_pattern);
    // The registry gives new loggers the global level (spdlog::set_level);
    // spdlog::get_level() would dereference the default logger, which
    // spdlog::shutdown() drops, so logging after shutdown() crashed here.
    return logger;
}

std::vector<std::string> recentLines() {
    std::vector<std::string> out;
    const unsigned next = g_ring.next.load();
    for (size_t k = 0; k < kRingLines; ++k) {
        const char* line = g_ring.lines[(next + k) % kRingLines];
        if (line[0]) out.emplace_back(line);
    }
    return out;
}

void writeRecentLines(int fd) {
    const unsigned next = g_ring.next.load();
    for (size_t k = 0; k < kRingLines; ++k) {
        const char* line = g_ring.lines[(next + k) % kRingLines];
        const size_t n = strnlen(line, kRingWidth);
        if (n == 0) continue;
#ifdef _WIN32
        _write(fd, line, static_cast<unsigned>(n));
        _write(fd, "\n", 1);
#else
        if (write(fd, line, n) < 0) return;
        if (write(fd, "\n", 1) < 0) return;
#endif
    }
}

void shutdown() {
    spdlog::shutdown(); // flushes every registered logger and joins the thread pool
    g_initialized = false;
}

} // namespace kke::log
