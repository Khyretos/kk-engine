// The public kke::platform backend (kke/Platform.h): Windows, macOS,
// Linux, and the parts of Android and iOS that follow the POSIX branch.
// A console backend replaces this whole folder; see backend.cmake.
#include "kke/Platform.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <malloc.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <stdlib.h> // arc4random_buf
#elif defined(__linux__)
#if defined(__ANDROID__)
#include <android/log.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <jni.h>
#include <filesystem>
#include <sstream>
#include <vector>
#endif
#include <cerrno>
#include <cstring>
#include <sched.h>
#include <sys/random.h>
#endif

namespace kke::platform {

namespace {

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\0')) s.pop_back();
    return s;
}

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v && *v ? std::string(v) : std::string();
}

void fail(std::string* error, const std::string& what) {
    if (error) *error = what;
}

#if defined(__linux__)
std::string firstLine(const char* path) {
    std::ifstream f(path);
    std::string s;
    if (f && std::getline(f, s)) return trim(s);
    return {};
}
#endif

#if defined(__APPLE__)
std::string hex(const unsigned char* data, size_t size) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 15];
    }
    return out;
}
#endif

} // namespace

const char* backendName() { return "desktop"; }

std::string machineId() {
#if defined(_WIN32)
    char buf[128] = {};
    DWORD size = sizeof(buf);
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", "MachineGuid", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, buf,
                     &size) == ERROR_SUCCESS)
        return trim(buf);
    return {};
#elif defined(__APPLE__)
    uuid_t id{};
    const timespec wait{ 1, 0 };
    if (gethostuuid(id, &wait) != 0) return {};
    return hex(reinterpret_cast<const unsigned char*>(id), sizeof(id));
#elif defined(__linux__)
    for (const char* path : { "/etc/machine-id", "/var/lib/dbus/machine-id" }) {
        if (std::string s = firstLine(path); !s.empty()) return s;
    }
    return {};
#else
    return {};
#endif
}

bool secureRandom(uint8_t* out, size_t size, std::string* error) {
#if defined(_WIN32)
    if (BCryptGenRandom(nullptr, out, static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        fail(error, "BCryptGenRandom failed");
        return false;
    }
    return true;
#elif defined(__APPLE__)
    (void)error;
    arc4random_buf(out, size);
    return true;
#elif defined(__linux__)
    size_t got = 0;
    while (got < size) {
        const ssize_t n = getrandom(out + got, size - got, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            fail(error, std::string("getrandom failed: ") + std::strerror(errno));
            return false;
        }
        got += static_cast<size_t>(n);
    }
    return true;
#else
    (void)out;
    (void)size;
    fail(error, "no secure random source on this platform");
    return false;
#endif
}

unsigned usableCpuCount() {
    unsigned n = std::thread::hardware_concurrency();
#if defined(__linux__)
    // hardware_concurrency() counts every core in the machine, ignoring
    // CPU affinity (taskset, container limits, the min-spec emulation in
    // docs/PERFORMANCE_NOTES.md).
    cpu_set_t affinity;
    if (sched_getaffinity(0, sizeof(affinity), &affinity) == 0) n = static_cast<unsigned>(CPU_COUNT(&affinity));
#endif
    return std::max(1u, n);
}

double peakResidentMemoryMb() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return static_cast<double>(pmc.PeakWorkingSetSize) / (1024.0 * 1024.0);
    return 0.0;
#else
    struct rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0.0;
#if defined(__APPLE__)
    return static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0); // bytes on macOS
#else
    return static_cast<double>(usage.ru_maxrss) / 1024.0;            // kilobytes on Linux
#endif
#endif
}

std::string hostName() {
#if defined(_WIN32)
    if (std::string n = env("COMPUTERNAME"); !n.empty()) return n;
#else
    char buf[256] = {};
    if (gethostname(buf, sizeof(buf) - 1) == 0 && buf[0]) return buf;
#endif
    return "unknown-host";
}

std::string userCacheDir() {
#if defined(__ANDROID__)
    // No HOME or XDG on Android: the app's cache folder, which the OS may
    // clear when space runs low (fine: everything cached here is rebuilt).
    if (const char* dir = SDL_GetAndroidCachePath()) return dir;
#endif
#if defined(_WIN32)
    if (std::string v = env("LOCALAPPDATA"); !v.empty()) return v;
#endif
    if (std::string v = env("XDG_CACHE_HOME"); !v.empty()) return v;
    if (std::string v = env("HOME"); !v.empty()) return v + "/.cache";
    return {};
}

void* alignedAlloc(size_t size, size_t alignment) {
    // std::aligned_alloc wants the size to be a multiple of the alignment.
    const size_t rounded = ((size + alignment - 1) / alignment) * alignment;
#if defined(_WIN32)
    return _aligned_malloc(rounded, alignment); // no std::aligned_alloc in the Windows C runtime
#else
    return std::aligned_alloc(alignment, rounded);
#endif
}

void alignedFree(void* ptr) {
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

#if defined(__ANDROID__)
namespace {

// The APK's assets/ folder, through the activity's AssetManager. SDL's own
// file functions look in private storage first, which would read the old
// unpacked copy of a file instead of the new one in an updated APK.
class ApkAssets {
public:
    ApkAssets() {
        m_env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
        if (!m_env || !activity) return;
        jclass cls = m_env->GetObjectClass(activity);
        jmethodID getAssets = m_env->GetMethodID(cls, "getAssets", "()Landroid/content/res/AssetManager;");
        jobject assets = getAssets ? m_env->CallObjectMethod(activity, getAssets) : nullptr;
        if (assets) {
            m_assets = m_env->NewGlobalRef(assets); // the manager lives only as long as this reference
            m_manager = AAssetManager_fromJava(m_env, m_assets);
            m_env->DeleteLocalRef(assets);
        }
        m_env->DeleteLocalRef(cls);
        m_env->DeleteLocalRef(activity);
    }
    ~ApkAssets() {
        if (m_assets) m_env->DeleteGlobalRef(m_assets);
    }
    ApkAssets(const ApkAssets&) = delete;
    ApkAssets& operator=(const ApkAssets&) = delete;

    bool ok() const { return m_manager != nullptr; }

    bool read(const std::string& name, std::string& out) const {
        AAsset* a = AAssetManager_open(m_manager, name.c_str(), AASSET_MODE_STREAMING);
        if (!a) return false;
        out.resize(static_cast<size_t>(AAsset_getLength64(a)));
        size_t done = 0;
        while (done < out.size()) {
            const int n = AAsset_read(a, out.data() + done, out.size() - done);
            if (n <= 0) break;
            done += static_cast<size_t>(n);
        }
        AAsset_close(a);
        return done == out.size();
    }

private:
    JNIEnv* m_env = nullptr;
    jobject m_assets = nullptr;
    AAssetManager* m_manager = nullptr;
};

// "kke_bundle.txt" in the APK lists every bundled file, one "size path"
// per line after a first "bundle <build id>" line (android/build_apk.py
// writes it). Once unpacked, the list is kept as "kke_bundle.unpacked":
// the same text means this build is already in place.
std::vector<std::string> bundlePaths(const std::string& manifest) {
    std::vector<std::string> paths;
    std::istringstream in(manifest);
    std::string line;
    std::getline(in, line); // "bundle <build id>"
    while (std::getline(in, line)) {
        const size_t space = line.find(' ');
        if (space != std::string::npos && space + 1 < line.size()) paths.push_back(line.substr(space + 1));
    }
    return paths;
}

} // namespace
#endif

void captureConsoleOutput() {
#if defined(__ANDROID__)
    static std::once_flag once;
    std::call_once(once, [] {
        int fds[2];
        if (pipe(fds) != 0) return;
        setvbuf(stdout, nullptr, _IOLBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[1]);
        std::thread([fd = fds[0]] {
            char buf[1024];
            std::string line;
            ssize_t n;
            while ((n = read(fd, buf, sizeof(buf))) > 0) {
                for (ssize_t i = 0; i < n; ++i) {
                    if (buf[i] != '\n') {
                        line += buf[i];
                        continue;
                    }
                    __android_log_write(ANDROID_LOG_INFO, "kke", line.c_str());
                    line.clear();
                }
            }
        }).detach();
    });
#endif
}

std::string bundledFilesDir(std::string* note) {
#if defined(__ANDROID__)
    namespace fs = std::filesystem;
    auto say = [&](const std::string& s) {
        if (note) *note = s;
    };
    const char* storage = SDL_GetAndroidInternalStoragePath();
    if (!storage) {
        say(std::string("no private storage folder: ") + SDL_GetError());
        return {};
    }
    const fs::path root(storage);
    const ApkAssets apk;
    std::string manifest;
    if (!apk.ok() || !apk.read("kke_bundle.txt", manifest)) {
        say("the APK has no kke_bundle.txt: no game files were unpacked");
        return storage;
    }
    const fs::path stamp = root / "kke_bundle.unpacked";
    std::string previous;
    {
        std::ifstream in(stamp, std::ios::binary);
        previous.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    if (previous == manifest) {
        say("game files already unpacked in " + root.string());
        return storage;
    }

    // Files the previous build had and this one doesn't go; the player's
    // own files (settings, saves) were never in the list and stay.
    std::error_code ec;
    const std::vector<std::string> paths = bundlePaths(manifest);
    for (const std::string& old : bundlePaths(previous)) {
        if (std::find(paths.begin(), paths.end(), old) == paths.end()) fs::remove(root / old, ec);
    }
    fs::remove(stamp, ec); // an interrupted unpack starts over next time
    std::string data;
    for (const std::string& rel : paths) {
        const fs::path target = root / rel;
        fs::create_directories(target.parent_path(), ec);
        const fs::path part = target.string() + ".part";
        bool written = apk.read(rel, data);
        if (written) {
            std::ofstream out(part, std::ios::binary | std::ios::trunc);
            out.write(data.data(), static_cast<std::streamsize>(data.size()));
            written = static_cast<bool>(out);
        }
        if (written) fs::rename(part, target, ec);
        if (!written || ec) {
            fs::remove(part, ec);
            say("could not unpack " + rel + " to " + root.string() + (ec ? ": " + ec.message() : std::string()));
            return storage;
        }
    }
    std::ofstream(stamp, std::ios::binary) << manifest;
    say("unpacked " + std::to_string(paths.size()) + " game files to " + root.string());
    return storage;
#else
    (void)note;
    return {};
#endif
}

DeviceHints deviceHints() {
    DeviceHints h;
    h.os = SDL_GetPlatform();
    h.logicalCores = static_cast<unsigned>(std::max(0, SDL_GetNumLogicalCPUCores()));
    h.systemRamMb = SDL_GetSystemRAM();
    h.steamDeckMode = env("SteamDeck") == "1";
#if defined(__linux__)
    h.productName = firstLine("/sys/devices/virtual/dmi/id/product_name");
    h.vendor = firstLine("/sys/devices/virtual/dmi/id/sys_vendor");
#endif
    return h;
}

} // namespace kke::platform
