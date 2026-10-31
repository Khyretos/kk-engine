#include "kke/CrashReporter.h"

#include "kke/Log.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <system_error>

#if defined(__unix__) || defined(__APPLE__)
#define KKE_CRASH_POSIX 1
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__GLIBC__) || defined(__APPLE__)
#define KKE_CRASH_BACKTRACE 1
#include <execinfo.h>
#endif
#endif

namespace kke::crash {

namespace {

std::mutex g_mutex;
std::atomic<bool> g_installed{ false };
std::atomic<bool> g_reported{ false };
std::atomic<bool> g_optIn{ false };
Uploader g_uploader;

#ifdef KKE_CRASH_POSIX

constexpr size_t kPathMax = 480;
constexpr int kContextMax = 16;
char g_folder[kPathMax];
char g_app[128];
char g_version[64];
struct ContextSlot {
    char key[32];
    char value[96];
};
ContextSlot g_context[kContextMax];
std::atomic<int> g_contextCount{ 0 };
alignas(16) char g_altStack[64 * 1024];

struct sigaction g_old[32];
const int kSignals[] = { SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS };
std::terminate_handler g_oldTerminate = nullptr;

// Every helper below uses only write(2) and arithmetic: the handler may run with the heap locked.
void put(int fd, const char* s) {
    const size_t n = strlen(s);
    if (n && write(fd, s, n) < 0) return;
}

void putNumber(int fd, unsigned long long v, int base) {
    char buf[32];
    int i = 31;
    buf[i] = '\0';
    if (v == 0) buf[--i] = '0';
    while (v > 0 && i > 0) {
        buf[--i] = "0123456789abcdef"[v % static_cast<unsigned>(base)];
        v /= static_cast<unsigned>(base);
    }
    put(fd, buf + i);
}

const char* signalName(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV (invalid memory access)";
    case SIGABRT: return "SIGABRT (aborted)";
    case SIGFPE: return "SIGFPE (arithmetic error)";
    case SIGILL: return "SIGILL (illegal instruction)";
    case SIGBUS: return "SIGBUS (bad memory access)";
    default: return "unknown signal";
    }
}

void appendText(char* dst, size_t cap, const char* text) {
    size_t len = strlen(dst);
    for (const char* p = text; *p && len + 1 < cap; ++p, ++len) dst[len] = *p;
    dst[len] = '\0';
}

void appendNumber(char* dst, size_t cap, unsigned long long v) {
    char buf[24];
    int i = 23;
    buf[i] = '\0';
    if (v == 0) buf[--i] = '0';
    while (v > 0 && i > 0) {
        buf[--i] = static_cast<char>('0' + v % 10);
        v /= 10;
    }
    appendText(dst, cap, buf + i);
}

// The report: reason, then who/when/where.
void writeReport(const char* reason, unsigned long long address, bool hasAddress) {
    // One crash, one report: an uncaught exception ends in abort(), whose SIGABRT must not write a second one over it.
    if (g_reported.exchange(true)) return;
    char path[kPathMax + 64];
    path[0] = '\0';
    appendText(path, sizeof(path), g_folder);
    appendText(path, sizeof(path), "/crash-");
    appendNumber(path, sizeof(path), static_cast<unsigned long long>(time(nullptr)));
    appendText(path, sizeof(path), "-");
    appendNumber(path, sizeof(path), static_cast<unsigned long long>(getpid()));
    appendText(path, sizeof(path), ".txt");
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) fd = 2; // no folder: stderr, so it is not lost
    put(fd, "kke crash report\n");
    put(fd, "app: ");
    put(fd, g_app);
    put(fd, "\nengine: ");
    put(fd, g_version);
    put(fd, "\nreason: ");
    put(fd, reason);
    if (hasAddress) {
        put(fd, " at 0x");
        putNumber(fd, address, 16);
    }
    put(fd, "\ntime: ");
    putNumber(fd, static_cast<unsigned long long>(time(nullptr)), 10);
    put(fd, " (unix seconds)\npid: ");
    putNumber(fd, static_cast<unsigned long long>(getpid()), 10);
    put(fd, "\n");
    const int n = std::min(g_contextCount.load(), kContextMax);
    for (int i = 0; i < n; ++i) {
        put(fd, g_context[i].key);
        put(fd, ": ");
        put(fd, g_context[i].value);
        put(fd, "\n");
    }
    put(fd, "\n-- backtrace (most recent call first; use addr2line -e <binary> to read the addresses) --\n");
#ifdef KKE_CRASH_BACKTRACE
    void* frames[64];
    const int count = backtrace(frames, 64);
    backtrace_symbols_fd(frames, count, fd);
#else
    put(fd, "(this platform has no backtrace)\n");
#endif
    put(fd, "\n-- last log lines --\n");
    log::writeRecentLines(fd);
    if (fd != 2) close(fd);
}

void onSignal(int sig, siginfo_t* info, void*) {
    writeReport(signalName(sig), reinterpret_cast<unsigned long long>(info ? info->si_addr : nullptr), info && (sig == SIGSEGV || sig == SIGBUS));
    // Back to the default action, and the signal again: the process dies as it would have (a core dump, the exit status).
    signal(sig, SIG_DFL);
    raise(sig);
}

void onTerminate() {
    // An exception nobody caught. What() is read in a try block; this is not the signal path.
    std::string why = "std::terminate";
    if (const std::exception_ptr e = std::current_exception()) {
        try {
            std::rethrow_exception(e);
        } catch (const std::exception& ex) {
            why = std::string("uncaught exception: ") + ex.what();
        } catch (...) {
            why = "uncaught exception of an unknown type";
        }
    }
    writeReport(why.c_str(), 0, false);
    if (g_oldTerminate) g_oldTerminate();
    std::abort();
}

#endif // KKE_CRASH_POSIX

} // namespace

std::string defaultFolder() {
    std::string folder = "crashes";
    if (char* pref = SDL_GetPrefPath("KreativeKompas", "crashes")) {
        folder = pref;
        SDL_free(pref);
    }
    return folder;
}

bool install(const Options& options) {
#ifdef KKE_CRASH_POSIX
    std::lock_guard lock(g_mutex);
    if (g_installed) return false;
    std::error_code ec;
    std::filesystem::create_directories(options.folder, ec);
    if (ec || options.folder.size() >= kPathMax) return false;
    std::string folder = options.folder;
    while (folder.size() > 1 && folder.back() == '/') folder.pop_back();
    std::snprintf(g_folder, sizeof(g_folder), "%s", folder.c_str());
    std::snprintf(g_app, sizeof(g_app), "%s", options.app.c_str());
    std::snprintf(g_version, sizeof(g_version), "%s", options.version.c_str());

#ifdef KKE_CRASH_BACKTRACE
    // The first backtrace() call loads libgcc, which allocates: do it now, not in the handler.
    void* warm[4];
    backtrace(warm, 4);
#endif
    stack_t stack{};
    stack.ss_sp = g_altStack;
    stack.ss_size = sizeof(g_altStack);
    sigaltstack(&stack, nullptr);

    struct sigaction action{};
    action.sa_sigaction = onSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    for (int sig : kSignals) sigaction(sig, &action, &g_old[sig]);
    g_oldTerminate = std::set_terminate(onTerminate);
    g_installed = true;
    return true;
#else
    (void)options;
    return false;
#endif
}

bool installed() { return g_installed; }

void uninstall() {
#ifdef KKE_CRASH_POSIX
    std::lock_guard lock(g_mutex);
    if (!g_installed) return;
    for (int sig : kSignals) sigaction(sig, &g_old[sig], nullptr);
    std::set_terminate(g_oldTerminate);
    g_contextCount = 0;
    g_reported = false;
    g_installed = false;
#endif
}

void setContext(const std::string& key, const std::string& value) {
#ifdef KKE_CRASH_POSIX
    std::lock_guard lock(g_mutex);
    const int n = std::min(g_contextCount.load(), kContextMax);
    for (int i = 0; i < n; ++i)
        if (std::strncmp(g_context[i].key, key.c_str(), 31) == 0) {
            std::snprintf(g_context[i].value, sizeof(g_context[i].value), "%s", value.c_str());
            return;
        }
    if (n >= kContextMax) return;
    std::snprintf(g_context[n].key, sizeof(g_context[n].key), "%s", key.c_str());
    std::snprintf(g_context[n].value, sizeof(g_context[n].value), "%s", value.c_str());
    g_contextCount = n + 1; // the handler sees the slot only once it is whole
#else
    (void)key;
    (void)value;
#endif
}

std::vector<std::string> reports(const std::string& folder) {
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(folder, ec))
        if (e.is_regular_file() && e.path().extension() == ".txt" && e.path().filename().string().rfind("crash-", 0) == 0) out.push_back(e.path().string());
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> pending(const std::string& folder) {
    std::vector<std::string> out;
    for (const std::string& r : reports(folder))
        if (!std::filesystem::exists(r + ".sent")) out.push_back(r);
    return out;
}

void setOptIn(bool sendReports) { g_optIn = sendReports; }
bool optedIn() { return g_optIn; }

void setUploader(Uploader uploader) {
    std::lock_guard lock(g_mutex);
    g_uploader = std::move(uploader);
}

int sendPending(const std::string& folder) {
    Uploader uploader;
    {
        std::lock_guard lock(g_mutex);
        uploader = g_uploader;
    }
    if (!g_optIn || !uploader) return 0; // nothing leaves without the player's yes and a server to send to
    int sent = 0;
    for (const std::string& path : pending(folder)) {
        std::ifstream in(path, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        if (!uploader(path, text.str())) continue;
        std::ofstream(path + ".sent") << "sent\n";
        ++sent;
    }
    return sent;
}

} // namespace kke::crash
