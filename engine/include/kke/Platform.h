#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// The operating-system services the engine needs that SDL3 does not
// already cover, behind one small interface (docs/PLATFORMS.md).
//
// SDL3 already abstracts windows, input, gamepads, audio devices, threads,
// timers and file paths on every platform it supports, including the
// console ports that licensed developers get from Microsoft, Sony and
// Nintendo. What is left is this list: things the engine used to ask the
// OS for directly, with #ifdef _WIN32 / __APPLE__ / __linux__ scattered
// across License.cpp, PackSeal.cpp, SystemInfo.cpp, ResourceGovernor.cpp,
// Thumbnails.cpp and PhysicsModule.cpp.
//
// One backend implements all of it. The public one, for Windows, macOS,
// Linux, Android and iOS, is engine/src/platform/desktop/. A developer with
// a console licence writes their own backend in a private folder (the SDKs
// are under NDA, so it can never be in this public repository) and points
// CMake at it:
//
//   cmake -DKKE_PLATFORM_BACKEND_DIR=/path/to/private/kke-platform-xyz ...
//
// The folder holds a backend.cmake that sets KKE_PLATFORM_BACKEND_SOURCES
// (and any extra libraries in KKE_PLATFORM_BACKEND_LIBRARIES); see
// engine/src/platform/desktop/backend.cmake for the template. Nothing else
// in the engine changes.
//
// Every function here is safe to call from any thread and never throws.
namespace kke::platform {

// Short name of the compiled-in backend ("desktop", or whatever a private
// backend returns). Written into benchmark reports and the startup log.
const char* backendName();

// A stable identifier for this device, or "" when the platform has none
// the engine may read. kke::license hashes it before use; never log it.
std::string machineId();

// Cryptographically secure random bytes from the OS. False (and *error
// set, when given) only if the OS refused.
bool secureRandom(uint8_t* out, size_t size, std::string* error = nullptr);

// Cores this process may actually run on: CPU affinity and container
// limits where the OS reports them, not just what the machine has. >= 1.
unsigned usableCpuCount();

// Peak resident memory of this process in MiB, 0 if unknown.
double peakResidentMemoryMb();

// The device's network name, "unknown-host" if there is none.
std::string hostName();

// Where per-user caches go (thumbnails and similar data that can be
// rebuilt), without a trailing separator, "" if the platform has none.
// Linux: $XDG_CACHE_HOME or ~/.cache. Windows: %LOCALAPPDATA%.
std::string userCacheDir();

// Where the game's own files (shaders/, assets/, ui/, ...) are when they
// don't sit next to the executable, "" when they do (every desktop).
// Android keeps them inside the APK, where the engine can't open them by
// path: the first start after an install or update unpacks them into the
// app's private storage and returns that folder (docs/ANDROID.md). On a
// problem *note (when given) says what went wrong; otherwise it says what
// was done, for the log. Call it from the main thread, before anything
// opens a file.
std::string bundledFilesDir(std::string* note = nullptr);

// Android discards stdout and stderr: from this call on, their lines go
// to logcat (tag "kke"), so a game's "Fatal error: ..." is still seen.
// Does nothing elsewhere. Safe to call more than once.
void captureConsoleOutput();

// Aligned heap memory; free it with alignedFree(), never free().
void* alignedAlloc(size_t size, size_t alignment);
void alignedFree(void* ptr);

// What kke/HardwareTarget.h looks at to recognise the device it runs on.
// A backend fills in what it can and leaves the rest empty.
struct DeviceHints {
    std::string os;          // SDL_GetPlatform(): "Linux", "Windows", "Android", "iOS", ...
    std::string productName; // DMI product name on PCs ("Jupiter" = Steam Deck LCD, "Galileo" = OLED)
    std::string vendor;      // DMI system vendor ("Valve", "ASUSTeK COMPUTER INC.", ...)
    bool steamDeckMode = false; // Steam sets SteamDeck=1 in Game Mode
    unsigned logicalCores = 0;
    int systemRamMb = 0;
};
DeviceHints deviceHints();

} // namespace kke::platform
