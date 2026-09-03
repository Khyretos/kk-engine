#include "kke/Log.h"

#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace kke::log {

namespace {
// Fallback pattern (no game name yet) in case something logs before
// init() runs — "?" is an honest placeholder, not a silent omission.
std::string g_pattern = "[%Y-%m-%d %H:%M:%S.%e][engine][%n][?]%^[%l]: %v%$";
bool g_initialized = false;
} // namespace

void init(const std::string& gameName, spdlog::level::level_enum level) {
    if (g_initialized) return;
    g_initialized = true;

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
