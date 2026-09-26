// kke_server: a headless KKE game server (docs/SERVER_HOSTING.md).
//
//   ./kke_server                       server.json next to it, or defaults
//   ./kke_server --name "Kees's world" --password hunter2
//   docker compose -f docker/server/docker-compose.yml up -d
//
// Settings, later winning: defaults, server.json (or --config FILE),
// KKE_SERVER_* environment variables, flags. Console commands on stdin
// ("help"). Ctrl+C or SIGTERM (docker stop) stops it cleanly.

#include "kke/AssetCatalog.h"
#include "kke/net/EnetTransport.h"
#include "kke/server/DedicatedServer.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_signalled{ false };
extern "C" void onSignal(int) { g_signalled = true; }

std::string stamp() {
    const std::time_t t = std::time(nullptr);
    char buf[32];
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

std::mutex g_out;
void say(const char* level, const std::string& line) {
    std::lock_guard<std::mutex> lock(g_out);
    std::cout << stamp() << " " << level << " " << line << std::endl;
}

// Lines typed on stdin, for the main loop. With no terminal (Docker
// without -it) stdin ends at once, and the server just runs on.
class Console {
public:
    Console() {
        std::thread([this] {
            std::string line;
            while (std::getline(std::cin, line)) {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_lines.push_back(line);
            }
        }).detach(); // getline can't be interrupted; the process ending ends it
    }
    bool next(std::string& line) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_lines.empty()) return false;
        line = std::move(m_lines.front());
        m_lines.pop_front();
        return true;
    }

private:
    std::mutex m_mutex;
    std::deque<std::string> m_lines;
};

} // namespace

int main(int argc, char** argv) {
    using namespace kke::server;
    const std::vector<std::string> args(argv + 1, argv + argc);
    for (const std::string& a : args)
        if (a == "--help" || a == "-h") {
            std::cout << ServerConfig::usage();
            return 0;
        }

    std::string configPath = "server.json";
    for (size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == "--config") configPath = args[i + 1];
    if (const char* v = std::getenv("KKE_SERVER_CONFIG"); v && *v) configPath = v;

    ServerConfig config;
    std::vector<std::string> errors;
    const bool explicitConfig = configPath != "server.json";
    if (explicitConfig && !std::filesystem::exists(configPath)) errors.push_back(configPath + ": no such file");
    config.loadFile(configPath, errors);
    config.applyEnv([](const char* key) { return std::getenv(key); }, errors);
    config.applyArgs(args, errors);
    config.validate(errors);
    if (!errors.empty()) {
        for (const std::string& e : errors) say("error", e);
        say("error", "not started: fix the settings above (--help lists them)");
        return 2;
    }

    const std::string exeDir = std::filesystem::path(argv[0]).parent_path().string();
    const std::string assets = kke::findAssetFolder("assets/synty", { "KKE_ASSETS_DIR", "KKE_SYNTY_DIR" }, exeDir);

    kke::net::EnetTransport transport;
    DedicatedServer server(config, transport, assets);
    server.log = [](const std::string& s) { say("info", s); };
    server.warn = [](const std::string& s) { say("warn", s); };
    say("info", "kke_server starting: " + config.describe());
    if (!server.start(errors)) {
        for (const std::string& e : errors) say("error", e);
        say("error", "not started");
        return 1;
    }
    if (config.hasGameSocket()) say("info", "ready: players join on UDP port " + std::to_string(config.port) + " (type help for commands)");

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    Console console;

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    const auto tick = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1.0 / config.tickRate));
    auto next = start;
    while (!g_signalled && !server.stopRequested()) {
        std::string line;
        while (console.next(line)) {
            const std::string out = server.command(line);
            if (!out.empty()) say("info", out);
        }
        server.update(std::chrono::duration<double>(clock::now() - start).count());
        next += tick;
        const auto now = clock::now();
        if (next < now) next = now; // fell behind (a slow disk, a paused VM): don't race to catch up
        std::this_thread::sleep_until(next);
    }
    if (g_signalled) say("info", "stopping (signal)");
    server.stop("the server is stopping");
    say("info", "stopped; files saved in " + config.saveDir);
    std::fflush(stdout);
    // The console thread may be blocked reading stdin: leave without joining it.
    std::_Exit(0);
}
