// The public kke::platform backend (kke/Platform.h): Windows, macOS,
// Linux, and the parts of Android and iOS that follow the POSIX branch.
// A console backend replaces this whole folder; see backend.cmake.
#include "kke/Platform.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
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
