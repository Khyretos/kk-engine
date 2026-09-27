#include "kke/Log.h"

#include <spdlog/async.h>
#include <spdlog/details/console_globals.h>
#include <spdlog/details/registry.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#if defined(__ANDROID__)
#include <spdlog/sinks/android_sink.h>
#endif

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
void shutdownAtExit() {
    g_exited = true;
    spdlog::shutdown();
}

// Android sends stdout nowhere: there the log goes to logcat, tagged
// "kke" (adb logcat -s kke, docs/ANDROID.md).
std::shared_ptr<spdlog::logger> makeSyncLogger(const std::string& name) {
#if defined(__ANDROID__)
    return spdlog::android_logger_mt(name, "kke");
#else
    return spdlog::stdout_color_mt(name);
#endif
}

std::shared_ptr<spdlog::logger> makeAsyncLogger(const std::string& name) {
#if defined(__ANDROID__)
    return spdlog::create_async_nb<spdlog::sinks::android_sink_mt>(name, std::string("kke"));
#else
    return spdlog::create_async_nb<spdlog::sinks::stdout_color_sink_mt>(name);
#endif
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
        auto logger = makeSyncLogger(moduleName);
        logger->set_pattern(g_pattern);
        return logger;
    }
    registerExitShutdown();
    auto logger = makeAsyncLogger(moduleName);
    logger->set_pattern(g_pattern);
    // The registry gives new loggers the global level (spdlog::set_level);
    // spdlog::get_level() would dereference the default logger, which
    // spdlog::shutdown() drops, so logging after shutdown() crashed here.
    return logger;
}

void shutdown() {
    spdlog::shutdown(); // flushes every registered logger and joins the thread pool
    g_initialized = false;
}

} // namespace kke::log
