// kke_benchmark: the benchmark anyone can run (docs/BENCHMARKS.md
// "Benchmark for everyone"). Double-click it: it plays every demo next to
// it for a short while, one after the other, each measuring itself
// (kke/BenchRecorder.h), and writes ONE results file to send back:
//
//   results/kke-benchmark-<date>_<time>.json   everything, for tools and AIs
//   results/kke-benchmark-<date>_<time>.txt    the same, for people
//
// It sits in its own folder (benchmark/) with benchmark_suite.yaml and
// finds the demos one folder up (or next to it).
//
// Each demo runs as its own process, so a demo that crashes, hangs or
// can't start is recorded as such (with what its log said) and the rest
// still run. Nothing is sent anywhere; the person sends the file.
//
//   kke_benchmark               every demo, 20 s each (about 10 minutes)
//   kke_benchmark --quick       fewer demos, 8 s each (about 3 minutes)
//   kke_benchmark --only a,b    only these demos (ids from --list)
//   kke_benchmark --seconds N   measure N s per demo
//   kke_benchmark --list        print the demos and exit
//   kke_benchmark --out DIR     results folder (default: results/ next to this program)
//   kke_benchmark --no-wait     don't wait for Enter at the end
//   kke_benchmark --suite FILE  another suite file than benchmark_suite.yaml next to it
//   kke_benchmark --collect RUN_DIR
//       don't run anything: write the results file from a run another
//       launcher made (Android's): RUN_DIR/reports/<id>.json, RUN_DIR/logs/<id>.log
//       and RUN_DIR/runs.json = [{"id", "status", "wall_s", "exit_code"}, ...].
//       Results go to --out, default RUN_DIR's parent folder.

#include "kke/BenchmarkReport.h"
#include "kke/DataFile.h"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <volk.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <io.h>
#include <windows.h>
#define KKE_ISATTY _isatty
#else
#include <unistd.h>
#define KKE_ISATTY isatty
#endif

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

#if defined(_WIN32)
constexpr const char* kExe = ".exe";
#else
constexpr const char* kExe = "";
#endif

fs::path pathFromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string utf8(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string localStamp(const char* format) {
    const std::time_t now = std::time(nullptr);
    char buf[64] = "unknown";
    if (const std::tm* tm = std::localtime(&now)) std::strftime(buf, sizeof(buf), format, tm);
    return buf;
}

struct Demo {
    std::string id, exe, title;
    double seconds = 20.0, warmup = 3.0;
    bool synty = false, quick = true;
    std::vector<std::pair<std::string, std::string>> env;
};

struct Suite {
    double loadTimeout = 120.0;
    std::vector<Demo> demos;
};

// benchmark_suite.yaml/.json in `dir`, or `file` when given (--suite).
bool loadSuite(const fs::path& dir, const fs::path& file, Suite& suite, std::string& error) {
    kke::datafile::Loaded loaded;
    if (!file.empty()) {
        loaded.file = file;
        if (!kke::datafile::loadFile(file, loaded.data, &error)) return false;
    } else if (!kke::datafile::load(dir, "benchmark_suite", loaded, &error)) {
        return false;
    }
    const json& j = loaded.data;
    const json defaults = j.value("defaults", json::object());
    const double seconds = defaults.value("seconds", 20.0), warmup = defaults.value("warmup", 3.0);
    suite.loadTimeout = defaults.value("load_timeout", 120.0);
    for (const json& d : j.value("demos", json::array())) {
        Demo demo;
        demo.id = d.value("id", "");
        demo.exe = d.value("exe", demo.id);
        demo.title = d.value("title", demo.id);
        demo.seconds = d.value("seconds", seconds);
        demo.warmup = d.value("warmup", warmup);
        demo.synty = d.value("synty", false);
        demo.quick = d.value("quick", true);
        const json env = d.value("env", json::object());
        for (const auto& [k, v] : env.items()) demo.env.emplace_back(k, v.is_string() ? v.get<std::string>() : v.dump());
        if (demo.id.empty()) {
            error = "a demo in " + utf8(loaded.file) + " has no id";
            return false;
        }
        suite.demos.push_back(std::move(demo));
    }
    if (suite.demos.empty()) {
        error = utf8(loaded.file) + " lists no demos";
        return false;
    }
    return true;
}

// --- Vulkan: is there a driver at all, and which GPUs? -------------------
struct VulkanProbe {
    json info;
    VkPhysicalDevice best = VK_NULL_HANDLE;
    VkInstance instance = VK_NULL_HANDLE;
};

VulkanProbe probeVulkan() {
    VulkanProbe p;
    if (volkInitialize() != VK_SUCCESS) {
        p.info = { { "loader", "missing" },
                   { "problem", "No Vulkan driver was found. Update the graphics driver (NVIDIA, AMD or Intel's own site), "
                                "or the GPU is too old for Vulkan." } };
        return p;
    }
    uint32_t loaderVersion = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion) vkEnumerateInstanceVersion(&loaderVersion);
    p.info["loader"] = std::to_string(VK_API_VERSION_MAJOR(loaderVersion)) + "." + std::to_string(VK_API_VERSION_MINOR(loaderVersion)) + "." +
                       std::to_string(VK_API_VERSION_PATCH(loaderVersion));
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "kke_benchmark";
    app.apiVersion = loaderVersion >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3 : loaderVersion;
    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    if (VkResult r = vkCreateInstance(&ci, nullptr, &p.instance); r != VK_SUCCESS) {
        p.info["problem"] = "Vulkan is installed but refused to start (VkResult " + std::to_string(r) + "). Update the graphics driver.";
        p.instance = VK_NULL_HANDLE;
        return p;
    }
    volkLoadInstance(p.instance);
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(p.instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    if (count) vkEnumeratePhysicalDevices(p.instance, &count, devices.data());
    json gpus = json::array();
    int bestScore = -1;
    bool any13 = false;
    for (VkPhysicalDevice d : devices) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(d, &props);
        const bool v13 = props.apiVersion >= VK_API_VERSION_1_3;
        any13 = any13 || v13;
        json g;
        for (const auto& [k, v] : kke::collectSystemInfo(d))
            if (k.rfind("gpu", 0) == 0 || k == "vulkan_api_version") g[k] = v;
        g["supports_vulkan_1_3"] = v13;
        gpus.push_back(g);
        const int score = (v13 ? 100 : 0) + (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3
                                             : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
                                             : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU ? 0 : 1);
        if (score > bestScore) {
            bestScore = score;
            p.best = d;
        }
    }
    p.info["gpus"] = gpus;
    if (devices.empty()) p.info["problem"] = "Vulkan found no GPU. Update the graphics driver.";
    else if (!any13) p.info["problem"] = "No GPU here supports Vulkan 1.3, which the demos need. Updating the graphics driver often adds it.";
    return p;
}

// --- Reading a demo's log --------------------------------------------------
struct LogLine {
    std::string time;  // "2026-09-27 08:30:12.345" or ""
    std::string level; // "info", "warning", "error", "critical", or "" (not an engine line)
    std::string text;  // the line without the time stamp
};

std::string stripAnsi(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
            i += 2;
            while (i < s.size() && !(s[i] >= '@' && s[i] <= '~')) ++i;
            continue;
        }
        if (s[i] != '\r') out += s[i];
    }
    return out;
}

// "[2026-09-27 08:30:12.345][engine][Physics][Game][warning]: text"
LogLine parseLine(const std::string& raw) {
    LogLine l;
    const std::string s = stripAnsi(raw);
    if (s.size() > 25 && s[0] == '[' && s[24] == ']') {
        l.time = s.substr(1, 23);
        const size_t colon = s.find("]: ");
        const size_t open = colon == std::string::npos ? std::string::npos : s.rfind('[', colon);
        if (open != std::string::npos) l.level = s.substr(open + 1, colon - open - 1);
        l.text = s.substr(25);
    } else {
        l.text = s;
    }
    return l;
}

double secondsOfDay(const std::string& time) {
    int h = 0, m = 0;
    double sec = 0.0;
    if (time.size() < 23 || std::sscanf(time.c_str() + 11, "%d:%d:%lf", &h, &m, &sec) != 3) return -1.0;
    return h * 3600.0 + m * 60.0 + sec;
}

std::vector<LogLine> readLog(const fs::path& file) {
    std::vector<LogLine> lines;
    std::ifstream in(file, std::ios::binary);
    std::string raw;
    while (std::getline(in, raw)) lines.push_back(parseLine(raw));
    return lines;
}

// What an exit code means, in words.
std::string exitMeaning(int code) {
    if (code == 0) return "normal exit";
#if defined(_WIN32)
    switch (static_cast<unsigned>(code)) {
    case 0xC0000005u: return "crashed: access violation (a bug, or a driver fault)";
    case 0xC000001Du: return "crashed: illegal instruction (this CPU lacks an instruction the build uses, most likely AVX2)";
    case 0xC00000FDu: return "crashed: stack overflow";
    case 0xC0000135u: return "could not start: a DLL is missing";
    case 0xC0000142u: return "could not start: a DLL failed to initialise";
    case 0xC0000409u: return "crashed: fast fail / buffer overrun (often an unhandled C++ exception)";
    case 0xC0000374u: return "crashed: heap corruption";
    case 0x80000003u: return "crashed: breakpoint / abort";
    case 0x40010004u: return "killed (the window or process was closed)";
    default: break;
    }
#else
    switch (-code) {
    case 4: return "crashed: SIGILL, illegal instruction (this CPU lacks an instruction the build uses, most likely AVX2)";
    case 6: return "crashed: SIGABRT (abort, often an unhandled C++ exception)";
    case 7: return "crashed: SIGBUS";
    case 8: return "crashed: SIGFPE";
    case 9: return "killed: SIGKILL (out of memory, or killed by the benchmark after hanging)";
    case 11: return "crashed: SIGSEGV (a bug, or a driver fault)";
    case 15: return "stopped: SIGTERM";
    default: break;
    }
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "exited with code %d (0x%08x)", code, static_cast<unsigned>(code));
    return buf;
}

// --- Running one demo ------------------------------------------------------
struct RunResult {
    std::string status; // ok, broken_modules, ended_early, crashed, hung, no_report, missing, failed_to_start
    int exitCode = 0;
    double wallSeconds = 0.0;
};

RunResult runDemo(const Demo& d, const fs::path& exeDir, const fs::path& reportDir, const fs::path& logFile, double loadTimeout) {
    RunResult r;
    const fs::path exe = exeDir / (d.exe + kExe);
    std::error_code ec;
    if (!fs::exists(exe, ec)) {
        r.status = "missing";
        return r;
    }
    SDL_Environment* env = SDL_CreateEnvironment(true);
    auto set = [env](const std::string& k, const std::string& v) { SDL_SetEnvironmentVariable(env, k.c_str(), v.c_str(), true); };
    set("KKE_BENCHMARK", std::to_string(d.seconds));
    set("KKE_BENCH_WARMUP", std::to_string(d.warmup));
    set("KKE_BENCH_DIR", utf8(reportDir));
    set("KKE_BENCH_NAME", d.id);
    set("KKE_BENCH_ITEM", d.id);
    set("KKE_SKIP_INTRO", "1");
    for (const auto& [k, v] : d.env) set(k, v);

    const std::string exeUtf8 = utf8(exe);
    const char* args[] = { exeUtf8.c_str(), nullptr };
    SDL_IOStream* log = SDL_IOFromFile(utf8(logFile).c_str(), "wb");
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<char**>(args));
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    const std::string cwd = utf8(exeDir);
    SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, cwd.c_str());
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    if (log) {
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_REDIRECT);
        SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_POINTER, log);
        SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    } else {
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
    }
    const auto start = std::chrono::steady_clock::now();
    SDL_Process* process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    SDL_DestroyEnvironment(env);
    if (log) SDL_CloseIO(log); // the child has its own handle now
    if (!process) {
        r.status = "failed_to_start";
        std::ofstream(logFile, std::ios::app) << "kke_benchmark: could not start " << exeUtf8 << ": " << SDL_GetError() << "\n";
        return r;
    }

    const double limit = d.warmup + d.seconds + loadTimeout;
    bool exited = false, killed = false;
    int code = 0;
    int lastShown = -1;
    const bool showProgress = KKE_ISATTY(1) != 0; // a live seconds counter, only on a terminal
    while (!exited) {
        exited = SDL_WaitProcess(process, false, &code);
        if (exited) break;
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (showProgress && static_cast<int>(t) != lastShown) {
            lastShown = static_cast<int>(t);
            std::printf("\r    running... %3d s ", lastShown);
            std::fflush(stdout);
        }
        if (t > limit && !killed) {
            SDL_KillProcess(process, false); // ask first
            killed = true;
        }
        if (t > limit + 10.0) {
            SDL_KillProcess(process, true);
            exited = SDL_WaitProcess(process, true, &code);
            break;
        }
        SDL_Delay(100);
    }
    if (showProgress) std::printf("\r                        \r");
    SDL_DestroyProcess(process);
    r.exitCode = code;
    r.wallSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const bool report = fs::exists(reportDir / (d.id + ".json"), ec);
    if (killed) r.status = "hung"; // its report, if any, covers what it managed
    else if (!report) r.status = code != 0 ? "crashed" : "no_report";
    else r.status = code != 0 ? "crashed" : "ok";
    return r;
}

// --- The text summary ------------------------------------------------------
std::string pad(std::string s, size_t n) {
    if (s.size() > n) s = s.substr(0, n - 1) + "~";
    return s + std::string(n - s.size(), ' ');
}
std::string num(const json& v, int decimals = 1) {
    if (!v.is_number()) return "-";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v.get<double>());
    return buf;
}

std::string textSummary(const json& all) {
    std::ostringstream t;
    const json& sys = all["system"];
    auto s = [&sys](const char* k) { return sys.contains(k) ? sys[k].get<std::string>() : std::string("unknown"); };
    t << "Kreative Kompas Engine benchmark, " << all["started"].get<std::string>() << "\n";
    t << "Engine " << s("engine_version") << " (" << s("engine_commit") << "), " << s("build_type") << " build\n\n";
    t << "Machine\n";
    t << "  OS       " << s("os_version") << "\n";
    t << "  CPU      " << s("cpu") << ", " << s("cpu_logical_cores") << " threads\n";
    t << "  RAM      " << s("ram_mb") << " MB\n";
    t << "  GPU      " << s("gpu") << " (" << s("gpu_type") << ", " << s("gpu_memory_mb") << " MB)\n";
    t << "  Driver   " << s("gpu_driver") << ", Vulkan " << s("vulkan_api_version") << "\n";
    t << "  Display  " << s("display") << "\n";
    if (all["vulkan"].contains("problem")) t << "\n  PROBLEM: " << all["vulkan"]["problem"].get<std::string>() << "\n";
    t << "\n";
    t << pad("Demo", 44) << pad("Result", 16) << pad("FPS", 8) << pad("1% low", 8) << pad("p99 ms", 8) << pad("Hitches", 9) << "Warnings\n";
    t << std::string(100, '-') << "\n";
    for (const json& d : all["demos"]) {
        const json& r = d.contains("report") ? d["report"] : json::object();
        const json sum = r.value("summary", json::object());
        std::string result = d["status"].get<std::string>();
        if (result == "ok" && sum.contains("verdict")) result = sum["verdict"].get<std::string>();
        std::string hitches = sum.contains("hitches") ? std::to_string(sum["hitches"]["count"].get<int>()) : "-";
        t << pad(d["title"].get<std::string>(), 44) << pad(result, 16) << pad(num(sum.value("fps_avg", json())), 8)
          << pad(num(sum.value("fps_1pct_low", json())), 8)
          << pad(sum.contains("frame_ms") && sum["frame_ms"].is_object() ? num(sum["frame_ms"]["p99"]) : "-", 8) << pad(hitches, 9)
          << d["log"]["warnings"].get<int>() + d["log"]["errors"].get<int>() << "\n";
    }
    t << "\nResults: smooth = the slowest 1% of frames still >= 55 fps, playable >= 30, struggles >= 15, too slow below.\n";
    t << "The demos run uncapped (no vsync) to show what this machine can do.\n";

    bool header = false;
    for (const json& d : all["demos"]) {
        const std::string st = d["status"].get<std::string>();
        const bool bad = st != "ok";
        const json& r = d.contains("report") ? d["report"] : json::object();
        const json hitches = r.value("hitches", json::array());
        if (!bad && hitches.empty() && d["log"]["errors"].get<int>() == 0) continue;
        if (!header) {
            t << "\nDetails\n";
            header = true;
        }
        t << "\n* " << d["title"].get<std::string>() << ": " << st;
        if (d.contains("exit")) t << " (" << d["exit"].get<std::string>() << ")";
        t << "\n";
        for (const auto& b : r.value("broken_modules", json::array())) t << "    broken: " << b.get<std::string>() << "\n";
        int shown = 0;
        for (const json& h : hitches) {
            if (++shown > 5) {
                t << "    ... " << hitches.size() - 5 << " more hitch(es) in the .json\n";
                break;
            }
            t << "    hitch at " << num(h["t_s"]) << " s: " << num(h["frame_ms"]) << " ms (usually " << num(h["median_ms"]) << "), "
              << h["cause"].get<std::string>() << "\n";
        }
        int errs = 0;
        for (const auto& e : d["log"]["messages"]) {
            if (e["level"] != "error" && e["level"] != "critical") continue;
            if (++errs > 5) break;
            t << "    log: " << e["text"].get<std::string>() << "\n";
        }
        if (bad)
            for (const auto& l : d["log"].value("tail", json::array())) t << "    | " << l.get<std::string>() << "\n";
    }
    t << "\nPlease send the file " << all["results_file"].get<std::string>() << " (the .json next to this one) back.\n";
    return t.str();
}

bool writable(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path probe = dir / ".write_test";
    {
        std::ofstream f(probe);
        if (!(f << "x")) return false;
    }
    fs::remove(probe, ec);
    return true;
}

} // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8); // paths and GPU names print as UTF-8
#endif
    bool quick = false, list = false, wait = KKE_ISATTY(0) != 0, openFolder = true;
    double secondsOverride = -1.0;
    std::set<std::string> only;
    std::string outArg, suiteArg, collectArg;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--quick") quick = true;
        else if (a == "--list") list = true;
        else if (a == "--no-wait") wait = false;
        else if (a == "--no-open") openFolder = false;
        else if (a == "--seconds") secondsOverride = std::atof(next().c_str());
        else if (a == "--out") outArg = next();
        else if (a == "--suite") suiteArg = next();
        else if (a == "--collect") collectArg = next();
        else if (a == "--only") {
            std::stringstream ss(next());
            for (std::string id; std::getline(ss, id, ',');)
                if (!id.empty()) only.insert(id);
        } else if (a == "--help" || a == "-h") {
            std::printf("kke_benchmark [--quick] [--only a,b] [--seconds N] [--out DIR] [--list] [--no-wait] [--no-open]\n"
                        "Runs every demo next to this program for a short while and writes one results file.\n");
            return 0;
        } else {
            std::fprintf(stderr, "kke_benchmark: unknown option '%s' (try --help)\n", a.c_str());
            return 2;
        }
    }
    auto finish = [&](int code) {
        if (wait) {
            std::printf("\nPress Enter to close this window.\n");
            std::fflush(stdout);
            for (int c = std::getchar(); c != '\n' && c != EOF; c = std::getchar()) {
            }
        }
        return code;
    };

    const char* base = SDL_GetBasePath();
    const fs::path exeDir = base ? pathFromUtf8(base) : fs::current_path();
    // The demos: one folder up in a download (benchmark/ has its own
    // folder), or right here.
    std::error_code dirEc;
    const fs::path demoDir = fs::is_directory(exeDir / "shaders", dirEc) ? exeDir : (exeDir / "..").lexically_normal();
    Suite suite;
    std::string error;
    if (!loadSuite(exeDir, suiteArg.empty() ? fs::path() : pathFromUtf8(suiteArg), suite, error)) {
        std::fprintf(stderr, "kke_benchmark: %s\n(benchmark_suite.yaml must be next to this program, or use --suite)\n", error.c_str());
        return finish(1);
    }
    std::vector<Demo> demos;
    for (Demo d : suite.demos) {
        if (!only.empty() ? !only.count(d.id) : (quick && !d.quick)) continue;
        if (quick) {
            d.seconds = std::min(d.seconds, 8.0);
            d.warmup = std::min(d.warmup, 2.0);
        }
        if (secondsOverride > 0.0) d.seconds = secondsOverride;
        demos.push_back(d);
    }
    if (list) {
        for (const Demo& d : suite.demos) std::printf("%-18s %s%s\n", d.id.c_str(), d.title.c_str(), d.quick ? "" : "  (not in --quick)");
        return 0;
    }
    if (demos.empty()) {
        std::fprintf(stderr, "kke_benchmark: no demo matches (see --list)\n");
        return finish(1);
    }

    // Where results go: next to the program, or the user's app data folder
    // when that isn't writable (e.g. unpacked under Program Files).
    const bool collecting = !collectArg.empty();
    const fs::path collectDir = collecting ? pathFromUtf8(collectArg) : fs::path();
    fs::path outDir = !outArg.empty() ? pathFromUtf8(outArg) : collecting ? (collectDir / "..").lexically_normal() : exeDir / "results";
    if (!writable(outDir)) {
        char* pref = SDL_GetPrefPath("Kreative Kompas", "KKE Benchmark");
        if (pref) {
            outDir = pathFromUtf8(pref) / "results";
            SDL_free(pref);
        }
        if (!writable(outDir)) {
            std::fprintf(stderr, "kke_benchmark: cannot write results anywhere (tried %s)\n", utf8(outDir).c_str());
            return finish(1);
        }
    }
    const std::string stamp = kke::timestampForFileName();
    const fs::path runDir = collecting ? collectDir : outDir / ("run_" + stamp);
    // --collect: what the other launcher saw of each demo.
    std::map<std::string, RunResult> collected;
    if (collecting) {
        json runs;
        std::string runsError;
        if (!kke::datafile::loadFile(runDir / "runs.json", runs, &runsError) || !runs.is_array()) {
            std::fprintf(stderr, "kke_benchmark: --collect needs %s: %s\n", utf8(runDir / "runs.json").c_str(), runsError.c_str());
            return finish(1);
        }
        for (const json& r : runs) {
            // Launchers write what they know; a missing or null field keeps its default.
            if (!r.is_object() || !r.contains("id") || !r["id"].is_string()) continue;
            RunResult rr;
            if (r.contains("status") && r["status"].is_string()) rr.status = r["status"].get<std::string>();
            else rr.status = "no_report";
            if (r.contains("exit_code") && r["exit_code"].is_number_integer()) rr.exitCode = r["exit_code"].get<int>();
            if (r.contains("wall_s") && r["wall_s"].is_number()) rr.wallSeconds = r["wall_s"].get<double>();
            collected[r["id"].get<std::string>()] = rr;
        }
    }
    const fs::path reportDir = runDir / "reports", logDir = runDir / "logs";
    std::error_code ec;
    fs::create_directories(reportDir, ec);
    fs::create_directories(logDir, ec);

    // Synty packs are never shipped; a person who owns them can point
    // KKE_ASSETS_DIR at them (docs/BENCHMARKS.md).
    const char* assetsEnv = SDL_getenv("KKE_ASSETS_DIR"); // UTF-8, unlike std::getenv on Windows
    const bool artFound = (assetsEnv && *assetsEnv && fs::is_directory(pathFromUtf8(assetsEnv), ec)) || fs::is_directory(demoDir / "assets" / "synty", ec);

    double total = 0.0;
    for (const Demo& d : demos) total += d.warmup + d.seconds + 5.0;
    std::printf("Kreative Kompas Engine benchmark\n\n");
    if (!collecting) std::printf("%zu demos, about %d minutes. Each opens its own window and plays by itself:\n", demos.size(), static_cast<int>(total / 60.0 + 0.99));
    if (!collecting) std::printf("please don't touch the mouse or keyboard, and keep other programs closed.\n");
    std::printf("Results go to %s\n\n", utf8(outDir).c_str());

    SDL_Init(SDL_INIT_VIDEO); // only for the display's size and refresh rate; fine if it fails
    VulkanProbe vk = probeVulkan();
    json all;
    all["format"] = "kke-benchmark/1";
    all["started"] = localStamp("%Y-%m-%d %H:%M:%S");
    json system = json::object();
    for (const auto& [k, v] : kke::collectSystemInfo(vk.best)) system[k] = v;
    all["system"] = system;
    all["vulkan"] = vk.info;
    if (vk.instance) vkDestroyInstance(vk.instance, nullptr);
    all["run"] = { { "quick", quick }, { "demos", demos.size() }, { "synty_art", artFound ? "found" : "not found: those demos use stand-in art" } };
    if (vk.info.contains("problem")) std::printf("Warning: %s\n\n", vk.info["problem"].get<std::string>().c_str());

    const auto runStart = std::chrono::steady_clock::now();
    json results = json::array();
    int ok = 0, missing = 0;
    for (size_t i = 0; i < demos.size(); ++i) {
        const Demo& d = demos[i];
        std::printf("[%zu/%zu] %s\n", i + 1, demos.size(), d.title.c_str());
        std::fflush(stdout);
        const fs::path logFile = logDir / (d.id + ".log");
        RunResult r;
        if (!collecting) {
            r = runDemo(d, demoDir, reportDir, logFile, suite.loadTimeout);
        } else if (auto it = collected.find(d.id); it != collected.end()) {
            r = it->second;
            // A report with a clean exit is "ok" whatever the launcher guessed.
            if (r.status == "no_report" && fs::exists(reportDir / (d.id + ".json"), ec)) r.status = "ok";
        } else {
            r.status = "missing";
        }

        json out;
        out["id"] = d.id;
        out["exe"] = d.exe;
        out["title"] = d.title;
        out["status"] = r.status;
        out["wall_s"] = std::round(r.wallSeconds * 10.0) / 10.0;
        if (r.status != "missing") {
            out["exit_code"] = r.exitCode;
            out["exit"] = exitMeaning(r.exitCode);
        }
        json envOut = json::object();
        for (const auto& [k, v] : d.env) envOut[k] = v;
        out["env"] = envOut;
        if (d.synty) out["art"] = artFound ? "synty" : "stand-in (no Synty packs)";

        // The demo's own report: its system block is the launcher's, so
        // only what differs (e.g. the GPU it picked) is kept.
        json report;
        if (fs::exists(reportDir / (d.id + ".json"), ec) && kke::datafile::loadFile(reportDir / (d.id + ".json"), report)) {
            json differs = json::object();
            const json reportSystem = report.value("system", json::object());
            for (const auto& [k, v] : reportSystem.items())
                if (!system.contains(k) || system[k] != v) differs[k] = v;
            report.erase("system");
            if (!differs.empty()) report["system_differs"] = differs;
            if (r.status == "ok" && !report.value("broken_modules", json::array()).empty()) out["status"] = "broken_modules";
            const double measured = report["summary"].value("seconds", 0.0);
            // Against what the demo was asked to measure (its own record of it).
            const double wanted = report.value("config", json::object()).value("measure_s", d.seconds);
            if (out["status"] == "ok" && measured < wanted * 0.5) out["status"] = "ended_early";
        }

        // The log: warnings and errors (each distinct text once, with a
        // count), the lines around each hitch, and the tail when it failed.
        const std::vector<LogLine> lines = readLog(logFile);
        int warnings = 0, errors = 0;
        std::map<std::string, int> seen;
        json messages = json::array();
        for (const LogLine& l : lines) {
            const bool isWarn = l.level == "warning", isErr = l.level == "error" || l.level == "critical";
            const bool fatal = l.text.find("Fatal error") != std::string::npos;
            if (!isWarn && !isErr && !fatal) continue;
            (isWarn ? warnings : errors)++;
            // Dedupe on the text after the "[engine][module][game][level]: " prefix.
            const size_t colon = l.text.find("]: ");
            std::string key = colon == std::string::npos ? l.text : l.text.substr(colon + 3);
            if (seen[key]++ == 0 && messages.size() < 60) messages.push_back({ { "level", isWarn ? "warning" : l.level.empty() ? "error" : l.level }, { "text", l.text } });
        }
        for (json& m : messages) {
            const std::string text = m["text"].get<std::string>();
            const size_t colon = text.find("]: ");
            m["count"] = seen[colon == std::string::npos ? text : text.substr(colon + 3)];
        }
        json logOut = { { "file", utf8(fs::relative(logFile, outDir, ec)) }, { "lines", lines.size() }, { "warnings", warnings }, { "errors", errors }, { "messages", messages } };
        if (out["status"] != "ok") {
            json tail = json::array();
            for (size_t k = lines.size() > 80 ? lines.size() - 80 : 0; k < lines.size(); ++k) tail.push_back(lines[k].time.empty() ? lines[k].text : "[" + lines[k].time + "]" + lines[k].text);
            logOut["tail"] = tail;
        }
        out["log"] = logOut;
        if (report.contains("hitches")) {
            for (json& h : report["hitches"]) {
                const double at = secondsOfDay(h.value("time", ""));
                json nearby = json::array();
                for (const LogLine& l : lines) {
                    const double lt = secondsOfDay(l.time);
                    if (at >= 0.0 && lt >= 0.0 && std::abs(lt - at) <= 1.0 && nearby.size() < 8) nearby.push_back(l.text);
                }
                h["log_near"] = nearby;
            }
        }
        if (!report.is_null()) out["report"] = report;

        // One line per demo while it runs.
        const std::string st = out["status"].get<std::string>();
        if (st == "ok") {
            ++ok;
            const json& s = report["summary"];
            std::printf("    %s: %.0f fps, slowest 1%% %.0f fps, %d hitch(es)%s\n", s["verdict"].get<std::string>().c_str(), s["fps_avg"].get<double>(),
                        s["fps_1pct_low"].get<double>(), s["hitches"]["count"].get<int>(),
                        errors ? (", " + std::to_string(errors) + " error(s) in the log").c_str() : "");
        } else if (st == "missing") {
            ++missing;
            std::printf("    not in this download (%s%s not found), skipped\n", d.exe.c_str(), kExe);
        } else {
            std::printf("    %s%s\n", st.c_str(), out.contains("exit") ? (": " + out["exit"].get<std::string>()).c_str() : "");
        }
        std::fflush(stdout);
        results.push_back(out);
    }
    all["demos"] = results;
    all["finished"] = localStamp("%Y-%m-%d %H:%M:%S");
    all["duration_s"] = std::round(std::chrono::duration<double>(std::chrono::steady_clock::now() - runStart).count());
    json counts = json::object();
    for (const json& d : results) counts[d["status"].get<std::string>()] = counts.value(d["status"].get<std::string>(), 0) + 1;
    all["status_counts"] = counts;

    const std::string name = "kke-benchmark-" + stamp;
    all["results_file"] = name + ".json";
    const fs::path jsonFile = outDir / (name + ".json"), textFile = outDir / (name + ".txt");
    {
        std::ofstream out(jsonFile, std::ios::binary);
        out << all.dump(1) << "\n";
    }
    const std::string text = textSummary(all);
    {
        std::ofstream out(textFile, std::ios::binary);
        out << text;
    }
    std::printf("\n%s\n", text.c_str());
    std::printf("Done: %d of %zu demos ran fine.\nResults: %s\n", ok, demos.size(), utf8(jsonFile).c_str());
    if (openFolder) {
#if defined(_WIN32)
        std::string url = "file:///" + utf8(outDir);
        std::replace(url.begin(), url.end(), '\\', '/');
#else
        const std::string url = "file://" + utf8(outDir);
#endif
        SDL_OpenURL(url.c_str());
    }
    SDL_Quit();
    return finish(ok + missing == static_cast<int>(demos.size()) ? 0 : 3);
}
