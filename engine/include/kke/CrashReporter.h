#pragma once

#include <functional>
#include <string>
#include <vector>

namespace kke::crash {

// Crash reports (docs/CRASH_REPORTS.md). When the game dies by a signal (a bad
// pointer, an abort, a divide by zero) or an exception nobody caught, a text
// file is written to the user's crash folder: what happened, where (a
// backtrace), what the engine was doing (the context a module set, the last
// log lines). It never leaves the machine by itself: sending is the player's
// choice (EngineSettings::privacy.sendCrashReports, off), and the engine ships
// no server: a game with one gives setUploader() the way to reach it.
//
// POSIX only for now (Linux, macOS, Android): the handler uses only
// async-signal-safe calls (open, write, backtrace_symbols_fd) and runs on its
// own stack, so a stack overflow is reported too. install() returns false on
// other platforms.
struct Options {
    std::string folder;   // where reports go (created if missing)
    std::string app;      // "My Game"
    std::string version = "kke 0.1";
};

// The game's crash folder: crashes/ in the user data folder (the working folder when there is none).
std::string defaultFolder();

// Installs the handlers (SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS, std::terminate).
// False when the platform has no support, the folder can't be made, or it is
// already installed.
bool install(const Options& options);
bool installed();
// Puts the old handlers back (tests).
void uninstall();

// Something the report should say ("gpu" = "AMD Radeon", "language" = "nl",
// "scene" = "level 3"): a small table, replaced by key, readable by the handler
// without a lock. Keys cut at 31 bytes, values at 95; 16 at most.
void setContext(const std::string& key, const std::string& value);

// ---- what to do with them afterwards
// Report files in the folder, oldest first.
std::vector<std::string> reports(const std::string& folder);
// Those not yet sent (no ".sent" beside them).
std::vector<std::string> pending(const std::string& folder);

// The player's choice (settings). Nothing is ever sent while it is false.
void setOptIn(bool sendReports);
bool optedIn();
// How a game's server takes a report: true when it did. The report's text is passed in.
using Uploader = std::function<bool(const std::string& path, const std::string& contents)>;
void setUploader(Uploader uploader);
// Sends the pending reports of `folder` if the player opted in and an uploader is set; a sent report
// is renamed "<name>.sent" (kept, so the player can still read it). Returns how many were sent.
int sendPending(const std::string& folder);

} // namespace kke::crash
