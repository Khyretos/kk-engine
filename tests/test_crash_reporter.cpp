#include "kke/CrashReporter.h"
#include "kke/Log.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>

#if defined(__linux__)
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#endif

// Crash reports (docs/CRASH_REPORTS.md): the recent log lines they carry, the
// files they leave, and the player's choice about sending them. The signal
// path itself is tested by letting a child process crash.

namespace fs = std::filesystem;

namespace {

fs::path freshFolder(const char* name) {
    const fs::path dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

bool logHas(const std::string& needle) {
    for (const std::string& l : kke::log::recentLines())
        if (l.find(needle) != std::string::npos) return true;
    return false;
}

} // namespace

TEST(CrashReporter, TheLastLogLinesAreKeptForTheReport) {
    kke::log::init("crash-test");
    auto logger = kke::log::get("CrashTest");
    logger->info("ring marker {}", 12345);
    logger->flush();
    for (int i = 0; i < 200 && !logHas("ring marker 12345"); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_TRUE(logHas("ring marker 12345"));
    for (const std::string& l : kke::log::recentLines()) {
        EXPECT_LT(l.size(), 256u);
        EXPECT_EQ(l.find('\n'), std::string::npos);
    }
    // More than the ring holds: the oldest go, the newest stay.
    for (int i = 0; i < 300; ++i) logger->info("flood {}", i);
    logger->flush();
    for (int i = 0; i < 300 && !logHas("flood 299"); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_TRUE(logHas("flood 299"));
    EXPECT_FALSE(logHas("ring marker 12345"));
    EXPECT_LE(kke::log::recentLines().size(), 128u);
}

TEST(CrashReporter, ReportsAreListedAndSentOnlyWithTheOptInAndAServer) {
    const fs::path dir = freshFolder("kke_crash_list");
    std::ofstream(dir / "crash-100-1.txt") << "first";
    std::ofstream(dir / "crash-200-2.txt") << "second";
    std::ofstream(dir / "notes.txt") << "not a report";
    EXPECT_EQ(kke::crash::reports(dir.string()).size(), 2u);
    EXPECT_EQ(kke::crash::pending(dir.string()).size(), 2u);
    EXPECT_EQ(kke::crash::reports(dir.string()).front(), (dir / "crash-100-1.txt").string()); // oldest first
    EXPECT_TRUE(kke::crash::reports((dir / "missing").string()).empty());

    std::vector<std::string> received;
    kke::crash::setUploader([&](const std::string&, const std::string& text) {
        received.push_back(text);
        return true;
    });
    kke::crash::setOptIn(false);
    EXPECT_FALSE(kke::crash::optedIn());
    EXPECT_EQ(kke::crash::sendPending(dir.string()), 0); // the player has not said yes
    EXPECT_TRUE(received.empty());

    kke::crash::setOptIn(true);
    EXPECT_EQ(kke::crash::sendPending(dir.string()), 2);
    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(received[0], "first");
    EXPECT_TRUE(kke::crash::pending(dir.string()).empty());
    EXPECT_EQ(kke::crash::reports(dir.string()).size(), 2u); // kept, so the player can still read them
    EXPECT_EQ(kke::crash::sendPending(dir.string()), 0);     // each goes once

    // A server that refuses: the report stays pending for the next time.
    std::ofstream(dir / "crash-300-3.txt") << "third";
    kke::crash::setUploader([](const std::string&, const std::string&) { return false; });
    EXPECT_EQ(kke::crash::sendPending(dir.string()), 0);
    EXPECT_EQ(kke::crash::pending(dir.string()).size(), 1u);

    // No server at all: nothing to send to, even with the opt-in.
    kke::crash::setUploader({});
    EXPECT_EQ(kke::crash::sendPending(dir.string()), 0);
    kke::crash::setOptIn(false);
    fs::remove_all(dir);
}

TEST(CrashReporter, InstallNeedsAFolderItCanMake) {
    kke::crash::Options o;
    o.folder = "/proc/definitely/not/writable/here";
    o.app = "test";
    EXPECT_FALSE(kke::crash::install(o));
    EXPECT_FALSE(kke::crash::installed());
}

#if defined(__linux__)

namespace {

// Runs `crash` in a child that has the reporter installed, and returns the child's wait status.
template <class F>
int crashInChild(const fs::path& dir, F crash) {
    kke::log::init("crash-test");
    kke::log::get("CrashTest")->info("before the crash {}", 777);
    kke::log::get("CrashTest")->flush();
    for (int i = 0; i < 200 && !logHas("before the crash 777"); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const pid_t pid = fork();
    if (pid == 0) {
        kke::crash::Options o;
        o.folder = dir.string();
        o.app = "Crash Test Game";
        o.version = "kke test";
        if (!kke::crash::install(o)) _exit(90);
        kke::crash::setContext("scene", "level 3");
        kke::crash::setContext("scene", "level 4"); // replaces
        kke::crash::setContext("gpu", "Test GPU");
        crash();
        _exit(91); // not reached
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return status;
}

} // namespace

TEST(CrashReporter, ABadPointerLeavesAReportAndStillKillsTheProcess) {
    const fs::path dir = freshFolder("kke_crash_segv");
    const int status = crashInChild(dir, [] {
        volatile int* p = nullptr;
        *p = 1;
    });
    ASSERT_TRUE(WIFSIGNALED(status)) << "the child must still die by the signal";
    EXPECT_EQ(WTERMSIG(status), SIGSEGV);
    const auto files = kke::crash::reports(dir.string());
    ASSERT_EQ(files.size(), 1u);
    const std::string text = slurp(files[0]);
    EXPECT_NE(text.find("kke crash report"), std::string::npos);
    EXPECT_NE(text.find("app: Crash Test Game"), std::string::npos);
    EXPECT_NE(text.find("engine: kke test"), std::string::npos);
    EXPECT_NE(text.find("reason: SIGSEGV (invalid memory access) at 0x0"), std::string::npos);
    EXPECT_NE(text.find("scene: level 4"), std::string::npos);
    EXPECT_EQ(text.find("level 3"), std::string::npos); // replaced
    EXPECT_NE(text.find("gpu: Test GPU"), std::string::npos);
    EXPECT_NE(text.find("-- backtrace"), std::string::npos);
    EXPECT_NE(text.find("-- last log lines --"), std::string::npos);
    EXPECT_NE(text.find("before the crash 777"), std::string::npos);
    fs::remove_all(dir);
}

TEST(CrashReporter, AnAbortAndAnUncaughtExceptionAreReportedWithTheirReason) {
    const fs::path abortDir = freshFolder("kke_crash_abort");
    int status = crashInChild(abortDir, [] { std::abort(); });
    ASSERT_TRUE(WIFSIGNALED(status));
    EXPECT_EQ(WTERMSIG(status), SIGABRT);
    ASSERT_EQ(kke::crash::reports(abortDir.string()).size(), 1u);
    EXPECT_NE(slurp(kke::crash::reports(abortDir.string())[0]).find("reason: SIGABRT (aborted)"), std::string::npos);
    fs::remove_all(abortDir);

    const fs::path throwDir = freshFolder("kke_crash_throw");
    // From a bare thread: on the test's own thread gtest's try/catch would catch it, and nothing would be uncaught.
    status = crashInChild(throwDir, [] {
        std::thread t([] { throw std::runtime_error("level file is broken"); });
        t.join();
    });
    ASSERT_TRUE(WIFSIGNALED(status));
    const auto files = kke::crash::reports(throwDir.string());
    ASSERT_GE(files.size(), 1u);
    EXPECT_NE(slurp(files[0]).find("uncaught exception: level file is broken"), std::string::npos);
    fs::remove_all(throwDir);
}

#endif
