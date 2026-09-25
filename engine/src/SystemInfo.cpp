// Platform-specific half of kke/BenchmarkReport.h: machine description and
// process memory. Kept apart from BenchmarkReport.cpp so the pure part
// stays trivially unit-testable.
#include "kke/BenchmarkReport.h"

#include <SDL3/SDL.h>
#include <volk.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#include <sys/resource.h>
#endif
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#define KKE_HAS_CPUID 1
#endif

#ifndef KKE_BUILD_TYPE
#define KKE_BUILD_TYPE "unknown"
#endif

namespace kke {

namespace {

std::string cpuModel() {
#if defined(KKE_HAS_CPUID)
    // The CPUID "brand string" (leaves 0x80000002..4): the same name the
    // OS shows, on every x86 OS, with no platform API needed.
    unsigned int regs[12] = {};
#if defined(_MSC_VER)
    int r[4];
    __cpuid(r, 0x80000000);
    if (static_cast<unsigned>(r[0]) >= 0x80000004u) {
        for (int i = 0; i < 3; ++i) { __cpuid(r, 0x80000002 + i); std::memcpy(regs + i * 4, r, 16); }
    }
#else
    unsigned int a, b, c, d;
    if (__get_cpuid(0x80000000, &a, &b, &c, &d) && a >= 0x80000004u) {
        for (unsigned i = 0; i < 3; ++i) {
            __get_cpuid(0x80000002 + i, &a, &b, &c, &d);
            regs[i * 4 + 0] = a; regs[i * 4 + 1] = b; regs[i * 4 + 2] = c; regs[i * 4 + 3] = d;
        }
    }
#endif
    std::string s(reinterpret_cast<const char*>(regs), sizeof(regs));
    s = s.c_str();
    size_t first = s.find_first_not_of(' ');
    if (first != std::string::npos && !s.empty()) return s.substr(first);
#endif
#if defined(__linux__)
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("model name", 0) == 0 || line.rfind("Model", 0) == 0) {
            size_t colon = line.find(':');
            if (colon != std::string::npos) return line.substr(colon + 2);
        }
    }
#endif
    return "unknown";
}

std::string versionString(uint32_t v) {
    return std::to_string(VK_API_VERSION_MAJOR(v)) + "." + std::to_string(VK_API_VERSION_MINOR(v)) + "." + std::to_string(VK_API_VERSION_PATCH(v));
}

} // namespace

std::string hostNameForFileName() {
#if defined(_WIN32)
    if (const char* n = std::getenv("COMPUTERNAME")) return n;
#else
    char buf[256] = {};
    if (gethostname(buf, sizeof(buf) - 1) == 0 && buf[0]) return buf;
#endif
    return "unknown-host";
}

double peakResidentMemoryMb() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return pmc.PeakWorkingSetSize / (1024.0 * 1024.0);
    return 0.0;
#else
    struct rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0.0;
#if defined(__APPLE__)
    return usage.ru_maxrss / (1024.0 * 1024.0); // bytes on macOS
#else
    return usage.ru_maxrss / 1024.0;            // kilobytes on Linux
#endif
#endif
}

std::vector<BenchmarkReport::KeyValue> collectSystemInfo(void* vkPhysicalDevice) {
    std::vector<BenchmarkReport::KeyValue> info;
    info.emplace_back("os", SDL_GetPlatform());
    info.emplace_back("host", hostNameForFileName());
    info.emplace_back("cpu", cpuModel());
    info.emplace_back("cpu_logical_cores", std::to_string(SDL_GetNumLogicalCPUCores()));
    info.emplace_back("ram_mb", std::to_string(SDL_GetSystemRAM()));
    if (vkPhysicalDevice) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(static_cast<VkPhysicalDevice>(vkPhysicalDevice), &props);
        const char* type = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? "discrete"
                         : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? "integrated"
                         : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU ? "cpu (software)"
                         : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU ? "virtual" : "other";
        info.emplace_back("gpu", props.deviceName);
        info.emplace_back("gpu_type", type);
        info.emplace_back("gpu_driver_version", std::to_string(props.driverVersion));
        info.emplace_back("vulkan_api_version", versionString(props.apiVersion));
    }
    info.emplace_back("build_type", KKE_BUILD_TYPE);
#if defined(__clang__)
    info.emplace_back("compiler", std::string("clang ") + __clang_version__);
#elif defined(__GNUC__)
    info.emplace_back("compiler", std::string("gcc ") + __VERSION__);
#elif defined(_MSC_VER)
    info.emplace_back("compiler", "msvc " + std::to_string(_MSC_VER));
#endif
    return info;
}

} // namespace kke
