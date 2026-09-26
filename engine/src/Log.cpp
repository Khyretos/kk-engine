#include "kke/Log.h"

#include <spdlog/async.h>
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
void shutdownAtExit() {
    g_exited = true;
    spdlog::shutdown();
}

void registerExitShutdown() {
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
    auto logger = spdlog::create_async_nb<spdlog::sinks::stdout_color_sink_mt>(moduleName);
    logger->set_pattern(g_pattern);
    logger->set_level(spdlog::get_level());
    return logger;
}

void shutdown() {
    spdlog::shutdown(); // flushes every registered logger and joins the thread pool
    g_initialized = false;
}

} // namespace kke::log
