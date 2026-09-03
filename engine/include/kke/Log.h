#pragma once

#include <spdlog/spdlog.h>
#include <memory>
#include <string>

namespace kke::log {

// Call once, at Application startup (Application's constructor already
// does this using the title you pass it — see Application.cpp), before
// any module logs anything. gameName is baked directly into every
// logger's output pattern (see get() below) as a literal string, not a
// per-message lookup — it's invariant for the life of the process, so
// there's no need to make every single log call pay for that.
void init(const std::string& gameName, spdlog::level::level_enum level = spdlog::level::info);

// Returns a cached async, non-blocking logger for the given module name,
// creating it on first use. Format: [timestamp][engine][module][game
// name][level]: message, with the level and message colored by
// severity in a real terminal (color codes are still written to a piped
///redirected stream, e.g. under Xvfb — a viewer that doesn't interpret
// ANSI just shows the raw escape codes, which is expected, not a bug).
//
// Every logger created this way shares ONE background thread and ONE
// bounded queue (set up in init()) — this is what makes logging
// non-blocking: a log call pushes a formatted message onto that queue
// and returns immediately; the background thread does the actual
// stdout write. Under sustained overload the queue drops the OLDEST
// buffered message rather than ever blocking the calling thread
// (spdlog::async_overflow_policy::overrun_oldest) — you can lose a log
// line under extreme spam, but you will never lose a frame waiting on
// one.
std::shared_ptr<spdlog::logger> get(const std::string& moduleName);

// Flushes every logger's queued messages and joins the background
// thread. Call before process exit — Application's destructor already
// does this — since async logging means a message can still be sitting
// unwritten in the queue when main() returns otherwise.
void shutdown();

} // namespace kke::log
